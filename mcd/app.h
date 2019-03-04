#pragma once
#include "network/http.h"
#include "view.h"

BEGIN_NAMESPACE_MCD

class UiString : public std::string
{
public:
	typedef UiString Self;

	UiString(ConStrRef str) : std::string(str) {}

	Self& operator +(const UiBinding<std::string>& str)
	{
		append(str.get());
		return *this;
	}
};

typedef UiString _S;

class App : public ViewState
{
private:
	bool onQuit() override
	{
		return window.ask("Quit?", false);
	}

	void onSelectFolder() override
	{
		std::string newFolder = window.browseForFolder();
		if (newFolder.size()) {
			uiSavingPath = newFolder;
			m_preFilePath.clear();
		}
	}

	void onRevealFolder() override
	{
		if (m_preFilePath.size())
			browseToFile(m_preFilePath);
		else if (uiSavingPath.get().size())
			window.revealPath(uiSavingPath);
	}

	void onDownload() override
	{
		setState(State::Waiting);

		m_asyncController.start(
			Promise([&](AbortSignal* abort) {
				return startDownload(abort);
			})
			.onFinish(std::bind(
				&App::onDownloadFinish, this, _1))
		);
	}

	void onDownloadFinish(Result r)
	{
		if (r.ok()) {
			setState(State::Complete);
			return;
		}

		remove(m_preFilePath.c_str());
		m_preFilePath.clear();

		if (r.is(InternalError::userAbort)) {
			setState(State::Aborted);
		}
		else {
			setState(State::Failed);
			showError(r);
		}
	}

	void onAbort() override
	{
		setState(State::Aborting);
		m_asyncController.abort();
	}

	HttpConfig userConfig()
	{
		HttpConfig config;
		config.setConnectTimeout(5);

		if (uiChkProxyServer)
			config.setHttpProxy(uiProxyServer);

		if (uiChkUserAgent) {
			uiUserAgent = encodeUri(trim(uiUserAgent));
			if (uiUserAgent.get().size())
				config.addHeader(_S("User-Agent: ") + uiUserAgent);
		}

		if (uiChkCookie) {
			uiCookie = encodeUri(trim(uiCookie));
			if (uiCookie.get().size())
				config.addHeader(_S("Cookie: ") + uiCookie);
		}

		return config;
	}

	void showError(const Result& r)
	{
		if (r.ok())
			return;

		std::string msg("Error: ");
		msg += resultString(r);
		window.error(msg);
	}

	static Result checkUrlSupportRange(ConStrRef url,
		const HttpConfig& config, AbortSignal* abort)
	{
		_must_not(config.hasHeader("Range"));
		HttpConfig config_(config);
		config_.addHeader("Range: bytes=0-");

		HttpGetRequest http;
		AbortSignal::Guard asg(abort, [&]() {
			http.abort();
		});

		_call(http.init(config_));
		_call(http.open(url));

		if (http.statusCode() != 206)
			return Result("http", http.statusCode());

		bool support = http.headers().has("Content-Range");
		_should(support, url, http);
		_must_or_return(RequireError::httpSupportRange, support, url);

		return {};
	}

	Result startDownload(AbortSignal* abort)
	{
		ConStrRef url = uiUrl;
		int connNum = uiConnNum;
		HttpConfig config = userConfig();

		_must(inRange(connNum, 1, 50), connNum);
		if (connNum > 1)
			_call(checkUrlSupportRange(url, config, abort));

		return doDownloadStuff(url, config, connNum, abort);
	}

	std::string renameFilePath(const std::string& path, int number)
	{
		std::stringstream ss;
		ss << " (" << number << ")";
		std::string number_ = ss.str();
		clear(&ss);

		std::cmatch result;
		std::regex rule(R"((.*)\.([^\.]*)$)");
		if (std::regex_search(path.c_str(), result, rule)) {
			ss << result[1] << number_;
			std::string suffix = result[2].str();
			if (suffix.size())
				ss << "." << result[2];
		}
		else {
			ss << path << number_;
		}

		return ss.str();
	}

	Result buildSavingPath(std::string* savingPath)
	{
		std::string path = uiSavingPath.get();
		path += "\\";
		path += baseName(uiUrl);

		if (!fileExists(path)) {
			*savingPath = path;
			return {};
		}

		std::string path_;
		for (int i : range(1, 200)) {
			path_ = renameFilePath(path, i);
			if (!fileExists(path_)) {
				*savingPath = path_;
				return {};
			}
		}

		_must(false, path_);
		return {};
	}

	Result doDownloadStuff(ConStrRef url, const HttpConfig& config,
		int connNum, AbortSignal* abort)
	{
		std::string path;
		_call(buildSavingPath(&path));

		//std::ofstream ofs(path);
		//_must(ofs.good());
		//ofs.close();

		m_preFilePath = path;

		HttpGetRequest http;
		AbortSignal::Guard asg(abort, [&]() {
			http.abort();
		});

		_call(http.init(config));
		_call(http.open(url));
		setState(State::Working);

		class DownloadFileWriter : public HttpResponseBase
		{
		public:
			Result init(ConStrRef path, int64_t pos)
			{
				m_file.open(path, std::ios::binary);
				m_file.seekp(pos);
				_must_or_return(InternalError::ioError, m_file.good());
				return {};
			}

			virtual Result write(const BinaryData& data) override
			{
				m_file.write((char*)data.buffer, data.size);
				_must_or_return(InternalError::ioError, m_file.good());
				return HttpResponseBase::write(data);
			}

			void close()
			{
				m_file.close();
			}

		private:
			std::ofstream m_file;
		};

		DownloadFileWriter writer;
		_call(writer.init(path, 0));


		___flag = true;
		std::thread([&, this]() {
			while (___flag) {
				auto t = writer.sizeDone();
				std::stringstream ss;
				ss << ">>> " << (t / 1024.0);
				uiStatusText = ss.str();

				if (writer.sizeTotal() != (DownloadFileWriter::SizeType)-1) {
					double xx = (double)t / writer.sizeTotal() * 1000;
					uiProgress = { {0, (int)xx} };
				}

				sleep(0.8);
			}
		}).detach();

		Result r = http.HttpRequest::save(&writer);
		___flag = false;
		return r;
	}

private:
	bool ___flag = true;

	std::string m_preFilePath;
	AsyncController m_asyncController;
};

END_NAMESPACE_MCD
