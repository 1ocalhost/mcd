#pragma once
#include "network/http.h"
#include "ui/kit.h"
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

class App : public View
{
private:
	bool onQuit() override
	{
		return window.ask("Quit?", false);
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
		comState.update(UiState::Working);

		m_asyncController.start(
			Promise([&](AbortSignal* abort) {
				return startDownload(abort);
			})
			.onFinish([&](Result r) {
				comState.update(UiState::Ready);
				showError(r);
			})
		);
	}

	void onAbort() override
	{
		comState.update(UiState::Aborting);
		m_asyncController.abort();
	}

	HttpConfig userConfig()
	{
		HttpConfig config;

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
		if (r.ok() || r.is(InternalError::userAbort))
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

		return doDownloadStuff(url, connNum, abort);
	}

	std::string renameFilePath(const std::string& path, int number)
	{
		std::stringstream ss;
		ss << "(" << number << ")";
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

	Result doDownloadStuff(ConStrRef url, int connNum, AbortSignal* abort)
	{
		std::string path;
		_call(buildSavingPath(&path));

		std::ofstream ofs;
		ofs.open(path, std::ios::binary);
		_must(ofs.good());

		m_preFilePath = path;

		//std::string fileName = baseName(uiUrl);
		//uiSavingPath.get() + fileName

		//std::ifstream infile(path);
		//return infile.good();

		//HttpGetRequest http;
		//AbortSignal::Guard asg(abort, [&]() {
		//	http.abort();
		//});

		//_call(http.init());
		//_call(http.open(url));

		//class HttpResponseXXX : public HttpResponseBase
		//{
		//public:
		//	virtual Result write(const BinaryData& data) override
		//	{
		//		long now = std::time(nullptr);
		//		bool toExit = now - m_time < 1;
		//		m_time = now;

		//		if (toExit)
		//			return HttpResponseBase::write(data);

		//		SizeType t = sizeDone();
		//		std::stringstream ss;
		//		ss << ">>> " << (t/1024.0) /*<< (char*)data.buffer*/ << "\n";
		//		OutputDebugStringA(ss.str().c_str());

		//		return HttpResponseBase::write(data);
		//	}

		//	long m_time = 0; 
		//};

		//HttpResponseXXX xxx;
		//return http.HttpRequest::save(&xxx);

		uiProgress = { {0, 100}, {101, 200}, {500, 550}, {700, 720} };
		return {};
	}

private:
	std::string m_preFilePath;
	AsyncController m_asyncController;
};

END_NAMESPACE_MCD
