#pragma once
#include "ward.h"
#include <winhttp.h>

BEGIN_NAMESPACE_MCD

namespace Guard {

template <class T>
using GuardImpl = std::unique_ptr<
	typename std::remove_pointer<T>::type,
	std::function<void(T)>
>;

struct WinHttp : public GuardImpl<HINTERNET>
{
	WinHttp(HINTERNET h = NULL) :
		GuardImpl<HINTERNET>(h, WinHttpCloseHandle) {}
};

template <class T>
struct DeleteObjectGuard : public GuardImpl<T>
{
	DeleteObjectGuard(T h = NULL) :
		GuardImpl<T>(h, DeleteObject) {}
};

typedef DeleteObjectGuard<HFONT> GdiFont;
typedef DeleteObjectGuard<HBRUSH> GdiBrush;
typedef DeleteObjectGuard<HBITMAP> GdiBitmap;
typedef DeleteObjectGuard<HDC> GdiDeleteDc; // or GdiReleaseDc?

class GdiReleaseDc
{
public:
	GdiReleaseDc(HWND hwnd) : m_hwnd(hwnd)
	{
		m_hdc = GetDC(hwnd);
	}

	HDC get() const
	{
		return m_hdc;
	}

	~GdiReleaseDc()
	{
		ReleaseDC(m_hwnd, m_hdc);
	}

private:
	HWND m_hwnd;
	HDC m_hdc;
};

class Mutex: public std::lock_guard<std::mutex>
{
public:
	Mutex(std::mutex* m) : lock_guard(*m) {}
};

template <class T>
using PtrSet = std::vector<std::unique_ptr<T>>;

} // namespace Guard

END_NAMESPACE_MCD
