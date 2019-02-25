#pragma once
#include "ward.h"
#include <winhttp.h>


namespace guard {

namespace ResGuard {

template <class T>
class ResGuardBase
{
public:
	typedef std::function<void(T)> Deleter;

	ResGuardBase(T handle, Deleter deleter) :
		m_resOwned(false), m_deleter(deleter)
	{
		reset(handle);
	}

	void release()
	{
		if (m_resOwned)
		{
			m_deleter(m_handle);
			m_resOwned = false;
		}
	}

	void reset(T handle)
	{
		release();
		m_handle = handle;
		m_resOwned = true;
	}

	~ResGuardBase()
	{
		release();
	}

	T take()
	{
		m_resOwned = false;
		return m_handle;
	}

	T get() const { return m_handle; }

	operator T() const { return get(); }

private:
	T m_handle;
	Deleter m_deleter;
	bool m_resOwned;
};


class WinHttp : public ResGuardBase<HINTERNET>
{
public:
	WinHttp(HINTERNET handle) :
		ResGuardBase<HINTERNET>(handle, deleter) {}

	static void deleter(HINTERNET handle)
	{
		if (handle)
			WinHttpCloseHandle(handle);
	}
};

class Font : public ResGuardBase<HFONT>
{
public:
	Font(HFONT handle = NULL) :
		ResGuardBase<HFONT>(handle, deleter) {}

	static void deleter(HINTERNET handle)
	{
		if (handle)
			DeleteObject(handle);
	}
};

} // namespace ResGuard

} // namespace guard

using namespace guard;
