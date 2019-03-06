#pragma once
#include "control.h"

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

	if (size == 0)
		return {};

	std::string result = u16to8(message);
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
	LPITEMIDLIST pidl = ILCreateFromPath(u8to16(path));
	if (pidl) {
		SHOpenFolderAndSelectItems(pidl, 0, 0, 0);
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

class WaitingAnimation
{
public:
	typedef UiBinding<std::string> Binding;
	typedef std::function<void(Binding*)> ClearFn;

	void init(Binding* text, ClearFn clear)
	{
		m_uiText = text;
		m_clearFn = clear;
	}

	~WaitingAnimation()
	{
		stop();
	}

	void play()
	{
		if (!m_uiText || !m_clearFn) {
			assert(0);
			return;
		}

		m_worker = new std::thread();
		*m_worker = std::thread([this](std::thread* self) {
			onWaiting(true);

			do {
				onWaiting(false);
				sleep(0.5);
			} while (m_worker == self);

			onWaiting(true);
			delete self;
		}, m_worker);
		m_worker->detach();
	}

	void stop()
	{
		m_worker = nullptr;
	}

private:
	void onWaiting(bool toClear)
	{
		if (toClear) {
			m_clearFn(m_uiText);
			return;
		}

		char ch = '.';
		if (m_uiText->get().size())
			ch = m_uiText->get().front();

		if (ch == '-')
			*m_uiText = "\\ ...";
		else if (ch == '\\')
			*m_uiText = "/ ...";
		else
			*m_uiText = "- ...";
	}

	ClearFn m_clearFn;
	Binding* m_uiText = nullptr;
	std::thread* m_worker = nullptr;
};

inline void _formatDataSizeImpl(int64_t num, double* v, int* m, int* p)
{
	constexpr int kilo = 1024;
	double& value = *v;
	int& magnitude = *m;
	int& precision = *p;

	while (num >= (kilo * kilo)) {
		num /= kilo;
		++magnitude;
	}

	if (num >= kilo || !magnitude) {
		value = (double)num / kilo;
	}
	else {
		value = (double)num;
		--magnitude;
	}

	if (value > 1000) { // 1001MB -> 1GB
		value = 1.0;
		precision = 0;
		++magnitude;
	}
}

inline void _formatDataSizeStream(int64_t num,
	std::stringstream* ss, bool toRound)
{
	if (num < 1000) {
		*ss << num << "bytes";
		return;
	}

	double value = 0;
	int magnitude = 0;
	int precision = 2;
	_formatDataSizeImpl(num, &value,
		&magnitude, &precision);

	char units[] = "KMGTPEZ";
	const int kMaxMagnitude = ARRAYSIZE(units) - 1;
	if (magnitude > kMaxMagnitude)
		return;

	if (toRound) {
		*ss << (int)round(value);
	}
	else {
		ss->precision(precision);
		*ss << std::fixed << value;
	}

	*ss << units[magnitude] << "iB";
}

inline std::string formattedDataSize(int64_t num, bool toRound)
{
	std::stringstream ss;
	_formatDataSizeStream(num, &ss, toRound);
	return ss.str();
}

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

inline std::string safeFileNameFromUri(ConStrRef uri)
{
	std::string name = baseName(uri);

	[](std::string* s) {
		auto pos = s->find('?');
		if (pos != std::string::npos)
			s->resize(pos);
	}(&name);

	std::replace_if(name.begin(), name.end(), [](char c) {
		return inArray(c,
			{ '<', '>', ':', '"', '/', '\\', '|', '?', '*' });
	}, '_');

	return name;
}

template <class T>
class Tachometer
{
public:
	struct Data
	{
		T size = 0;
		time_t time = 0;
	};

	static const size_t kNumTokeep = 3;

	Tachometer()
	{
		m_preData.push(Data());
	}

	double touch(const Data& d)
	{
		const auto& pre = m_preData.front();

		double interval = std::max(
			0.1, (double)(d.time - pre.time));
		double speed = (d.size - pre.size) / interval;

		m_preData.push(d);
		if (m_preData.size() > kNumTokeep)
			m_preData.pop();

		return speed;
	}

private:
	std::queue<Data> m_preData;
};

class TimePassed
{
public:
	TimePassed()
	{
		m_start = time(nullptr);
	}

	time_t get() const
	{
		return time(nullptr) - m_start;
	}

private:
	time_t m_start;
};

inline Result parseHttpRange(ConStrRef str,
	std::array<int64_t, 3>* result)
{
	std::cmatch matched;
	std::regex rule(R"((\d+)-(\d+)/(\d+))");
	bool valid = std::regex_search(str.c_str(), matched, rule);
	_must_or_return(InternalError::invalidInput, valid, str);

	for (int i : range(3))
		toNumber(matched[i + 1].str(), &(*result)[i]);

	return {};
}

END_NAMESPACE_MCD
