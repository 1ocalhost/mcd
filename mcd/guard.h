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

	void operator= (HINTERNET handle)
	{
		this->reset(handle);
	}

	static void deleter(HINTERNET handle)
	{
		if (handle)
			WinHttpCloseHandle(handle);
	}
};

template <class T>
class GdiObj : public ResGuardBase<T>
{
public:
	GdiObj(T handle = NULL) :
		ResGuardBase<T>(handle, deleter) {}

	void operator= (T handle)
	{
		this->reset(handle);
	}

	static void deleter(T handle)
	{
		if (handle)
			DeleteObject(handle);
	}
};

typedef GdiObj<HFONT> GdiFont;
typedef GdiObj<HBRUSH> GdiBrush;

} // namespace ResGuard

} // namespace guard

using namespace guard;
