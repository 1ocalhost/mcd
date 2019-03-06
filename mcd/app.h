#pragma once
#include "network/http.h"
#include "view.h"

BEGIN_NAMESPACE_MCD

struct AppTaskParam
{
	std::string url;
	std::string filePath;
	HttpConfig config;
	int64_t totalSize = 0;
	int64_t granularity = 0;
	int connNum = 0;
};

class AppTaskList
{
public:
	typedef Range<int64_t> Task;

	void spawn(const AppTaskParam& param)
	{
		int64_t amount = param.totalSize;
		int64_t step = param.granularity;

		for (int64_t i = 0; i < amount; i += step) {
			int64_t begin = i;
			int64_t end = i + step;
			if (end > amount)
				end = amount;

			m_tasks.push(Range<int64_t>(begin, end));
		}
	}

	bool get(Task* task)
	{
		Guard::Mutex lock(&m_mutex);
		if (m_tasks.empty())
			return false;

		*task = m_tasks.front();
		m_tasks.pop();
		return true;
	}

private:
	std::queue<Task> m_tasks;
	std::mutex m_mutex;
};

class AppDownloadWorker : public std::thread
{
public:
	typedef std::function<bool(Result)> AskRetry;
	typedef std::vector<Range<int64_t>> Ranges;

	AppDownloadWorker(
		const AppTaskParam& param,
		AppTaskList* list,
		ParallelFileWriter* writer,
		AskRetry askRetry) :
		m_taskParam(param),
		m_taskList(list),
		m_writer(writer),
		m_askRetry(askRetry)
	{
		assert(m_askRetry);
		thread::operator= (thread(
			std::bind(&AppDownloadWorker::run, this)
		));
	}

	void abort()
	{
		m_signal.trigger();
	}

	int64_t sizeDone() const
	{
		return m_preSizeDone + m_writer.sizeDone();
	}

	Range<int64_t> curRange() const
	{
		int64_t left = m_range.first;
		int64_t right = left + m_writer.sizeDone();
		return Range<int64_t>(left, right);
	}

	const Ranges& preRanges() const
	{
		return m_preRanges;
	}

	int waitingTimes() const
	{
		return m_waitingTimes;
	}

	void resetWaitingTimes()
	{
		m_waitingTimes = 0;
	}

private:
	void run()
	{
		for (;;) {
			AppTaskList::Task task;
			if (!m_taskList->get(&task))
				return;

			m_range = task;
			Result r = work();

			if (r.failed()) {
				if (!doRetry(r))
					return;
			}
		}
	}

	bool wait(int times)
	{
		int seconds = (int)pow(2, times);
		m_waitingTimes = seconds * 2;

		for (; m_waitingTimes > 0; --m_waitingTimes) {
			if (m_signal.didAborted())
				return false;

			sleep(0.5);
		}

		return true;
	}

	bool doRetry(Result r)
	{
		int timesTried = 0;
		for (;;) {
			if (timesTried < 8)
				++timesTried;

			if (!wait(timesTried))
				return false;

			if (m_askRetry(r)) {
				r = work();
				if (r.ok())
					return true;
			}
			else {
				return false;
			}
		}
	}

	Result work()
	{
		auto taskComplete = [this]() {
			int64_t taskSize = m_range.second - m_range.first;
			return m_writer.sizeDone() == taskSize;
		};

		Result r = workImpl();
		if (r.ok() || taskComplete()) {
			m_preRanges.push_back(m_range);
			m_preSizeDone += m_writer.sizeDone();
			m_writer.clear();
		}

		return r;
	}

	Result workImpl()
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

		std::array<int64_t, 3> range;
		_call(parseHttpRange(cr, &range));
		_must_or_return(invalidInput, m_curRange.first == range[0]);
		_must_or_return(invalidInput, m_curRange.second == range[1]);

		return {};
	}

	Range<int64_t> m_range; // [a, b)
	Range<int64_t> m_curRange; // [a, b]

	AppTaskList* m_taskList;
	AppTaskParam m_taskParam;
	HttpProxyWriter m_writer;

	AbortSignal m_signal;
	AskRetry m_askRetry;
	std::atomic_int m_waitingTimes = 0;

	std::atomic_int64_t m_preSizeDone = 0;
	Ranges m_preRanges;
};

class AppDownloadContractor
{
public:
	typedef AppDownloadContractor Self;
	typedef std::function<void()> HeartbeatFn;

	void onHeartbeat(HeartbeatFn fn)
	{
		m_heartbeat = fn;
	}

	Result start(const AppTaskParam& param)
	{
		_call(init(param));

		bool alive = true;
		std::thread ui([&, this]() {
			updateUi(&alive);
		});

		for (auto& i : m_workers)
			i->join();

		alive = false;
		ui.join();

		if (m_userAborted)
			return InternalError::userAbort();

		return m_result;
	}

	void abort()
	{
		m_userAborted = true;
		abortAllWorkers();
	}

	std::string statusText()
	{
		double speed = curSpeed();
		double progress = sizeDone() / totalSize();
		std::string speedData = formattedDataSize((int64_t)speed, true);

		size_t speedDataLen = speedData.size();
		if (speedDataLen > m_speedDataMaxLen)
			m_speedDataMaxLen = speedDataLen;

		std::stringstream ss;
		ss.precision(2);
		ss << std::fixed;
		ss << formattedDataSize(m_taskParam.totalSize, false);
		ss << " (" << (progress * 100) << "%), ";

		size_t filledWidth = m_speedDataMaxLen - speedDataLen;
		if (filledWidth > 100) {
			assert(0);
			return {};
		}

		ss << std::string(filledWidth, ' ');
		ss << speedData << "/s.";
		return ss.str();
	}

	void getRanges(std::vector<Range<int>> *range, int scaleTo)
	{
		range->clear();
		double rate = scaleTo / totalSize();

		auto addRange = [=](Range<int64_t> r) {
			int left = (int)(r.first * rate);
			int right = (int)(r.second * rate);
			range->emplace_back(left, right);
		};

		for (auto& w : m_workers) {
			for (auto& pr : w->preRanges())
				addRange(pr);

			addRange(w->curRange());
		}
	}

	bool hasWorkerWait() const
	{
		for (auto& i : m_workers)
			if (i->waitingTimes() > 0)
				return true;

		return false;
	}

	void resetWorkerWait()
	{
		for (auto& i : m_workers)
			i->resetWaitingTimes();
	}

private:
	Result init(const AppTaskParam& param)
	{
		_must(m_heartbeat);

		m_taskParam = param;
		m_taskList.spawn(param);
		_call(m_writer.init(param.filePath));

		for (auto i : range(m_taskParam.connNum)) {
			UNUSED(i);
			m_workers.emplace_back(
				new AppDownloadWorker(
					param, &m_taskList, &m_writer,
					std::bind(&Self::askRetry, this, _1)
				)
			);
		}

		return {};
	}

	void abortAllWorkers()
	{
		m_writer.abort();
		for (auto& i : m_workers)
			i->abort();
	}

	bool askRetry(Result r)
	{
		Guard::Mutex lock(&m_mutex);

		if (m_userAborted)
			return false;

		if (r.space() == http_api::resultSpace()) {
			if (inArray(r.code(), {
				ERROR_WINHTTP_TIMEOUT,
				ERROR_WINHTTP_CANNOT_CONNECT
			}))
				return true;
		}

		if (m_result.ok())
			m_result = r;

		abortAllWorkers();
		return false;
	}

	double totalSize()
	{
		return (double)m_taskParam.totalSize;
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
		for (auto& i : m_workers)
			done += i->sizeDone();

		return done;
	}

	double curSpeed()
	{
		int64_t done = sizeDone();
		time_t now = ::time(nullptr);
		return m_tachometer.touch({done, now});
	}

	Result m_result;
	bool m_userAborted = false;
	std::mutex m_mutex;

	AppTaskParam m_taskParam;
	AppTaskList m_taskList;
	Guard::PtrSet<AppDownloadWorker> m_workers;

	int m_speedTimes = 0;
	size_t m_speedDataMaxLen = 0;
	Tachometer<int64_t> m_tachometer;

	ParallelFileWriter m_writer;
	HeartbeatFn m_heartbeat;
};

class App : public ViewState
{
private:
	static const int kMaxConn = 100;

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
		uiUrl = encodeUri(trim(uiUrl));
		if (uiSavingPath.get().empty()) {
			window.info("Please select a folder to save the file.");
			return;
		}

		if (uiConnNum > kMaxConn) {
			std::stringstream ss;
			ss << "The maximum limit of connection number is ";
			ss << kMaxConn << ".\n(your input: " << uiConnNum.get() << ")";
			window.error(ss.str());
			return;
		}

		if (uiConnNum <= 0)
			uiConnNum = 1;

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

	void onRetryNow() override
	{
		uiRetryNow = "";
		m_toResetWorkerWait = true;
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

	static Result checkUrlSupportRange(
		int64_t* totalSize, ConStrRef url,
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

		_must_or_return(RequireError::httpSupportRange,
			http.statusCode() == 206, url);

		_must_or_return(InternalError::invalidInput,
			http.headers().has("Content-Range"), url);

		std::array<int64_t, 3> range;
		parseHttpRange(http.headers()
			.firstValue("Content-Range"), &range);

		*totalSize = range[2];
		return {};
	}

	Result startDownload(AbortSignal* abort)
	{
		int connNum = uiConnNum;
		_must(inRange(connNum, 1, kMaxConn + 1), connNum);

		ConStrRef url = uiUrl;
		HttpConfig config = userConfig();
		_must_not(config.hasHeader("Range"));

		int64_t totalSize = 0;
		_call(checkUrlSupportRange(&totalSize, url, config, abort));
		if (totalSize < KB(1)) {
			connNum = 1;
			uiConnNum = 1;
		}

		AppTaskParam param;
		_call(getTaskParam(&param, totalSize));
		return doDownloadStuff(param, abort);
	}

	Result getTaskParam(AppTaskParam* param, int64_t totalSize)
	{
		std::string filePath;
		_call(buildSavingPath(&filePath));
		m_preFilePath = filePath;

		param->url = uiUrl;
		param->filePath = filePath;
		param->config = userConfig();
		param->totalSize = totalSize;
		param->granularity = granularity(uiConnNum, totalSize);
		param->connNum = uiConnNum;

		return {};
	}

	int64_t granularity(int connNum, int64_t totalSize)
	{
		const int64_t& t = totalSize;
		int64_t g = 0;

		std::map<TaskGranularity, int> map_ = {
			{TaskGranularity::Conn_x1, 1},
			{TaskGranularity::Conn_x2, 2},
			{TaskGranularity::Conn_x3, 3},
			{TaskGranularity::Conn_x5, 5},
			{TaskGranularity::Conn_x10, 10},
			{TaskGranularity::Conn_x20, 20},
		};

		TaskGranularity tg = uiGranularity.get();
		if (map_.count(tg) > 0)
			g = t / (map_.at(tg) * connNum);
		else
			assert(0);

		g += 1; // for round
		if (g < KB(1))
			g = std::min<int64_t>(KB(1), totalSize);

		if (g > totalSize)
			g = totalSize;

		return g;
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
		_must(path.size());
		if (path.back() != '\\')
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
		AbortSignal* abort)
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

			if (m_toResetWorkerWait) {
				uiRetryNow = "";
				m_toResetWorkerWait = false;
			}
			else {
				uiRetryNow = contractor.hasWorkerWait()
					? "Retry Now" : "";
			}
		});

		return contractor.start(param);
	}

private:
	std::atomic_bool m_toResetWorkerWait = false;
	std::string m_preFilePath;
	AsyncController m_asyncController;
};

END_NAMESPACE_MCD
