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


inline std::string errorString(DWORD code, PCWSTR module = L"")
{
	if (code == 0)
		return {};

	const bool fromSystem = (module[0] == '\0');
	DWORD flag = fromSystem
		? FORMAT_MESSAGE_FROM_SYSTEM : FORMAT_MESSAGE_FROM_HMODULE;

	LPCVOID source = fromSystem ? NULL : GetModuleHandle(module);
	if (!fromSystem && !source) {
		assert(0);
		return {};
	}

	flag |= FORMAT_MESSAGE_ALLOCATE_BUFFER;
	flag |= FORMAT_MESSAGE_IGNORE_INSERTS;

	PSTR message = NULL; // will be in ASCII safely
	DWORD size = FormatMessageA(
		flag,
		source, code, NULL, (PSTR)&message, 0, NULL);

	std::string result(message, (size_t)size);
	LocalFree(message);

	trimRight(&result);
	if (result.size() && *result.rbegin() == '.')
		result.pop_back();

	return result;
}

class AbortSignal
{
public:
	typedef std::function<void()> AbortFn;

	class Guard
	{
	public:
		Guard(AbortSignal* signal, AbortFn fn) :
			m_signal(signal)
		{
			*m_signal = fn;
		}

		~Guard()
		{
			*m_signal = AbortFn();
		}

	private:
		AbortSignal* m_signal;
	};

	void trigger()
	{
		MutexGuard guard(&m_mutex);
		m_didAborted = true;
		if (m_abortFn)
			m_abortFn();
	}

	bool didAborted() const { return m_didAborted; }

	void clear()
	{
		m_didAborted = false;
		m_abortFn = {};
	}

private:
	void operator= (AbortFn fn)
	{
		MutexGuard guard(&m_mutex);
		m_abortFn = fn;
		if (fn && m_didAborted)
			fn();
	}

	AbortFn m_abortFn;
	bool m_didAborted = false;
	Mutex m_mutex;
};

class Promise
{
public:
	typedef std::function<Result(AbortSignal*)> JobFn;
	typedef std::function<void(Result)> FinishFn;

	Promise() {}

	Promise(JobFn job)
	{
		m_job = job;
	}

	Promise& onFinish(FinishFn fn)
	{
		m_onFinish = fn;
		return *this;
	}

	operator bool()
	{
		return (bool)m_job;
	}

	void run(AbortSignal* signal)
	{
		std::thread([this, signal]() {
			Result r = m_job(signal);
			if (signal->didAborted())
				r = InternalError::userAbort();

			m_onFinish(r);
			m_job = {};
			signal->clear();
		}).detach();
	}

private:
	JobFn m_job;
	FinishFn m_onFinish;
};

class AsyncController
{
public:
	void start(Promise job)
	{
		assert(!m_job);
		m_job = job;
		m_job.run(&m_signal);
	}

	void abort()
	{
		assert(m_job);
		m_signal.trigger();
	}

private:
	Promise m_job;
	AbortSignal m_signal;
};


class App : public View
{
private:
	bool onQuit() override
	{
		return comUtil.ask("Quit?", false);
	}

	void onDownload() override
	{
		uiUrl = encodeUri(trim(uiUrl));
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

		std::stringstream ss;
		ss << "Error: " << r.space() << "." << r.code();

		if (r.space() == http_api::resultSpace()) {
			std::string msg = errorString(r.code(), L"winhttp.dll");
			if (msg.size())
				ss << " (" << msg << ")";
		}

		comUtil.error(ss.str());
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

		//DoDownloadStuff(url, connNum);
		comUtil.info("OK");
		return {};
	}

private:
	AsyncController m_asyncController;
};

END_NAMESPACE_MCD
