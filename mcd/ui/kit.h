#pragma once
#include "../infra/guard.h"
#include <ShlObj_core.h> // ILCreateFromPath

BEGIN_NAMESPACE_MCD

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

	PWSTR message = NULL;
	DWORD size = FormatMessage(flag, source,
		code, NULL, (PWSTR)&message, 0, NULL);

	std::string result(u16to8(message), (size_t)size);
	LocalFree(message);

	trimRight(&result);
	if (result.size() && *result.rbegin() == '.')
		result.pop_back();

	return result;
}

inline std::string resultString(Result r)
{
	std::stringstream ss;
	ss << r.space() << "." << r.code();

	if (r.space() == http_api::resultSpace()) {
		PCWSTR module = (inRange(r.code(), 12000, 13000)
			? L"winhttp.dll" : L"");

		std::string msg = errorString(r.code(), module);
		if (msg.size())
			ss << " (" << msg << ")";
	}

	return ss.str();
}

inline Rect curScreenRect()
{
	POINT cursorPos;
	GetCursorPos(&cursorPos);

	HMONITOR monitor = MonitorFromPoint(
		cursorPos, MONITOR_DEFAULTTONEAREST);

	MONITORINFOEX mix;
	mix.cbSize = sizeof(mix);
	if (!GetMonitorInfo(monitor, (LPMONITORINFO)&mix)) {
		_should(false);
		mix.rcMonitor = {0, 0,
			GetSystemMetrics(SM_CXSCREEN),
			GetSystemMetrics(SM_CYSCREEN)};
	}

	return mix.rcMonitor;
}

inline void browseToFile(ConStrRef path)
{
	ITEMIDLIST *pidl = ILCreateFromPath(u8to16(path));
	if (pidl) {
		SHOpenFolderAndSelectItems(pidl, 0, 0, 0);
		SHBrowseForFolder(pidl);
		ILFree(pidl);
	}
}

inline bool fileExists(ConStrRef path) {
	struct _stat s;
	return (_wstat(u8to16(path), &s) == 0);
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
		mcd::Guard::Mutex g(&m_mutex);
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
		mcd::Guard::Mutex g(&m_mutex);
		m_abortFn = fn;
		if (fn && m_didAborted)
			fn();
	}

	AbortFn m_abortFn;
	bool m_didAborted = false;
	std::mutex m_mutex;
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

END_NAMESPACE_MCD
