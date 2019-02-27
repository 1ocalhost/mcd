#pragma once
#include "ward.h"
#include <winhttp.h>

BEGIN_NAMESPACE_MCD

namespace ResGuard {

template <class HandleType>
using WinBoolDeleter = BOOL(*)(HandleType);

template <class T, class D>
using GuardImpl = std::unique_ptr<typename std::remove_pointer<T>::type, D>;

template <class T>
using WinBoolDeleterGuard = GuardImpl<T, WinBoolDeleter<T>>;

struct WinHttpCloseHandleGuard : public WinBoolDeleterGuard<HINTERNET>
{
	WinHttpCloseHandleGuard(HINTERNET h = NULL) :
		WinBoolDeleterGuard<HINTERNET>(h, WinHttpCloseHandle) {}
};

typedef WinHttpCloseHandleGuard WinHttp;

template <class T>
struct DeleteObjectGuard : public WinBoolDeleterGuard<T>
{
	DeleteObjectGuard(T h = NULL) :
		WinBoolDeleterGuard<T>(h, deleteObject) {}

	static BOOL deleteObject(T h)
	{
		return DeleteObject(h);
	}
};

typedef DeleteObjectGuard<HFONT> GdiFont;
typedef DeleteObjectGuard<HBRUSH> GdiBrush;

struct DeleteDcGuard : public WinBoolDeleterGuard<HDC>
{
	DeleteDcGuard(HDC h) :
		WinBoolDeleterGuard<HDC>(h, DeleteDC) {}
};

typedef DeleteDcGuard GdiDc;


} // namespace ResGuard

END_NAMESPACE_MCD
