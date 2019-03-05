#pragma once
#include "network/http.h"
#include "view.h"

BEGIN_NAMESPACE_MCD

struct AppTaskParam
{
	std::string url;
	std::string filePath;
	HttpConfig config;
};

class AppDownloadWorker
{
public:
	AppDownloadWorker(const AppTaskParam& param,
		Range<int64_t> range, ParallelFileWriter* writer) :
		m_taskParam(param), m_range(range), m_writer(writer) {}

	void abort()
	{
		m_signal.trigger();
	}

	int64_t sizeDone() const
	{
		return m_writer.sizeDone();
	}

	const Range<int64_t>& range() const
	{
		return m_range;
	}

	Result run()
	{
		rebuildRange();
		HttpGetRequest http;
		AbortSignal::Guard asg(&m_signal, [&]() {
			http.abort();
		});

		HttpConfig config(m_taskParam.config);
		addRangeHeader(&config);

		_call(http.init(config));
		_call(http.open(m_taskParam.url));
		_equal_or_return_http_error(http, 206);
		_call(ckeckContentRange(http));

		m_writer.init(m_curRange.first);
		return http.saveResponse(&m_writer);
	}

private:
	void rebuildRange()
	{
		m_curRange = m_range;
		m_curRange.first += m_writer.sizeDone();
		m_curRange.second -= 1;
		assert(m_curRange.second >= m_curRange.first);
	}

	void addRangeHeader(HttpConfig* config)
	{
		std::stringstream ss;
		ss << "Range: bytes=" << m_curRange.first
			<< "-" << (m_curRange.second);

		config->addHeader(ss.str());
	}

	Result ckeckContentRange(const HttpGetRequest& http)
	{
		auto invalidInput = InternalError::invalidInput;

		std::string cr = http.headers().firstValue("Content-Range");
		_must_or_return(invalidInput, cr.size());

		std::cmatch result;
		std::regex rule(R"((\d+)-(\d+))");
		bool valid = std::regex_search(cr.c_str(), result, rule);
		_must_or_return(invalidInput, valid, cr);

		int64_t begin = 0;
		int64_t end = 0;
		toNumber(result[1].str(), &begin);
		toNumber(result[2].str(), &end);

		_must_or_return(invalidInput, m_curRange.first == begin);
		_must_or_return(invalidInput, m_curRange.second == end);

		return {};
	}

	Range<int64_t> m_range; // [a, b)
	Range<int64_t> m_curRange; // [a, b]
	AppTaskParam m_taskParam;
	AbortSignal m_signal;
	HttpProxyWriter m_writer;
};

class AppDownloadContractor
{
public:
	typedef std::function<void()> HeartbeatFn;

	void onHeartbeat(HeartbeatFn fn)
	{
		m_heartbeat = fn;
	}

	Result start(const AppTaskParam& param,
		int64_t totalSize, int connNum)
	{
		_must((bool)m_heartbeat);
		_call(m_writer.init(param.filePath));

		m_totalSize = totalSize;
		std::vector<Range<int64_t>> tasks;
		assignTask(totalSize, connNum, &tasks);

		std::vector<std::thread> threads;
		createTaskThreads(param, tasks, &threads);

		bool alive = true;
		std::thread ui([&, this]() {
			updateUi(&alive);
		});

		join(&threads);
		alive = false;
		ui.join();
		return {};
	}

	void abort()
	{
		for (auto& i : m_workers) {
			i->abort();
		}
	}

	std::string statusText()
	{
		double speed = curSpeed();
		double progress = sizeDone() / (double)m_totalSize;

		std::stringstream ss;
		ss.precision(2);
		ss << std::fixed << (progress * 100) << "% Got, "
			<< formattedDataSize((int64_t)speed) << "/s.";

		return ss.str();
	}

	void getRanges(std::vector<Range<int>> *range, int scaleTo)
	{
		range->clear();
		double rate = scaleTo / (double)m_totalSize;

		for (auto& i : m_workers) {
			int64_t left = i->range().first;
			int64_t right = left + i->sizeDone();
			int iLeft = (int)(left * rate);
			int iRight = (int)(right * rate);
			range->emplace_back(iLeft, iRight);
		}
	}

private:
	void join(std::vector<std::thread>* threads)
	{
		for (auto& i : *threads)
			i.join();
	}

	void createTaskThreads(const AppTaskParam& param,
		const std::vector<Range<int64_t>>& tasks,
		std::vector<std::thread>* threads)
	{
		for (auto i : tasks) {
			auto worker = new AppDownloadWorker(param, i, &m_writer);
			m_workers.emplace_back(worker);
			threads->emplace_back([worker]() {
				worker->run();
			});
		}
	}

	void assignTask(int64_t amount, int partNum,
		std::vector<Range<int64_t>> *r)
	{
		int64_t step = (amount / partNum) + 1;
		for (int64_t i = 0; i < amount; i += step) {
			int64_t begin = i;
			int64_t end = i + step;
			if (end > amount)
				end = amount;

			r->push_back(Range<int64_t>(begin, end));
		}
	}

	void updateUi(const bool* alive)
	{
		const double kCheckInteval = 0.2;
		const double kUiInteval = 0.8;

		const int max = (int)round(kUiInteval / kCheckInteval);
		int n = 0;
		while (*alive) {
			sleep(kCheckInteval);
			++n;
			n %= max;

			if (n == (max - 1))
				m_heartbeat();
		}
	}

	int64_t sizeDone()
	{
		int64_t done = 0;
		for (auto& i : m_workers) {
			done += i->sizeDone();
		}

		return done;
	}

	double curSpeed()
	{
		int64_t done = sizeDone();
		time_t now = ::time(nullptr);
		return m_tachometer.touch({done, now});
	}

	int m_speedTimes = 0;
	int64_t m_totalSize = 0;
	Tachometer<int64_t> m_tachometer;
	Guard::PtrSet<AppDownloadWorker> m_workers;
	ParallelFileWriter m_writer;
	HeartbeatFn m_heartbeat;
};

class App : public ViewState
{
private:
	static const int kMaxConn = 20;

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

		uiUrl = encodeUri(trim(uiUrl));
		if (uiSavingPath.get().empty()) {
			window.info("Please select a folder to save the file.");
			return;
		}

		if (uiConnNum > kMaxConn)
			uiConnNum = kMaxConn;

		if (uiConnNum <= 0)
			uiConnNum = 1;

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
		HttpConfig config_(config);
		config_.addHeader("Range: bytes=0-");

		HttpGetRequest http;
		AbortSignal::Guard asg(abort, [&]() {
			http.abort();
		});

		_call(http.init(config_));
		_call(http.open(url));
		_equal_or_return_http_error(http, 206);

		bool support = http.headers().has("Content-Range");
		_should(support, url, http);
		_must_or_return(RequireError::httpSupportRange, support, url);

		return {};
	}

	static Result checkUrlContentLength(ConStrRef url,
		const HttpConfig& config, AbortSignal* abort,
		int64_t* content)
	{
		HttpGetRequest http;
		AbortSignal::Guard asg(abort, [&]() {
			http.abort();
		});

		_call(http.init(config));
		_call(http.open(url));
		_equal_or_return_http_error(http, 200);

		auto& cl = http.headers().contentLength();
		_must_or_return(RequireError::httpSupportContentLenth,
			cl.didSet(), url);

		*content = cl;
		return {};
	}

	Result startDownload(AbortSignal* abort)
	{
		int connNum = uiConnNum;
		_must(inRange(connNum, 1, kMaxConn + 1), connNum);

		ConStrRef url = uiUrl;
		HttpConfig config = userConfig();
		_must_not(config.hasHeader("Range"));

		int64_t contentLength = 0;
		_call(checkUrlContentLength(url, config,
			abort, &contentLength));

		if (contentLength < KB(1)) {
			connNum = 1;
			uiConnNum = 1;
		}

		if (connNum > 1)
			_call(checkUrlSupportRange(url, config, abort));

		std::string filePath;
		_call(buildSavingPath(&filePath));
		m_preFilePath = filePath;

		AppTaskParam param;
		param.url = url;
		param.filePath = filePath;
		param.config = config;

		return doDownloadStuff(param, contentLength, connNum, abort);
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
		path += safeFileNameFromUri(uiUrl);

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

	Result doDownloadStuff(const AppTaskParam& param,
		int64_t contentLength,
		int connNum, AbortSignal* abort)
	{
		setState(State::Working);
		AppDownloadContractor contractor;
		AbortSignal::Guard g(abort, [&]() {
			contractor.abort();
		});

		std::stringstream ss;
		TimePassed tp;
		RPC::Model model;
		contractor.onHeartbeat([&, this]() {
			clear(&ss);
			ss << contractor.statusText()
				<< " (" << tp.get() << "s)";
			uiStatusText = ss.str();

			model.clear();
			contractor.getRanges(&model, RPC::kMaxRange);
			uiProgress = model;
		});

		return contractor.start(param, contentLength, connNum);
	}

private:
	std::string m_preFilePath;
	AsyncController m_asyncController;
};

END_NAMESPACE_MCD
