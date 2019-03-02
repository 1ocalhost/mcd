#pragma once
#include "../infra/guard.h"
#include <atlbase.h>
#include <atlstdthunk.h>

BEGIN_NAMESPACE_MCD

namespace GuiRandomProgress {

class Palette
{
public:
	Palette()
	{
		m_border = CreateSolidBrush(RGB(85, 123, 21));
		m_background = CreateSolidBrush(RGB(205, 244, 181));
		m_done = CreateSolidBrush(RGB(113, 164, 28));
	}

	HBRUSH border() const { return m_border.get(); }
	HBRUSH background() const { return m_background.get(); }
	HBRUSH done() const { return m_done.get(); }

private:
	ResGuard::GdiBrush m_border;
	ResGuard::GdiBrush m_background;
	ResGuard::GdiBrush m_done;
};

class MemoryDC
{
public:
	MemoryDC(HDC dst, Size size) : m_dst(dst), m_size(size)
	{
		m_bmp = CreateCompatibleBitmap(
			dst, size.width(), size.height());
		m_dc = CreateCompatibleDC(dst);
		SelectObject(m_dc.get(), m_bmp.get());
	}

	void fillRect(Point start, Size size, HBRUSH brush)
	{
		RECT rect;
		rect.left = start.x();
		rect.top = start.y();
		rect.right = size.width();
		rect.bottom = size.height();
		FillRect(m_dc.get(), &rect, brush);
	}

	void copyToDst(Point start)
	{
		BitBlt(m_dst, start.x(), start.y(),
			m_size.width(), m_size.height(),
			m_dc.get(), 0, 0, SRCCOPY);
	}

private:
	HDC m_dst;
	Size m_size;
	ResGuard::GdiBitmap m_bmp;
	ResGuard::GdiDeleteDc m_dc;
};

class Painter
{
public:
	Painter(HDC hdc, const Palette& palette) :
		m_hdc(hdc), m_palette(palette) {}

	void drawBorder(const RECT& rect)
	{
		LONG right = rect.right - 1;
		LONG bottom = rect.bottom - 1;
		drawBorderByPoints({
			{0, 0},
			{right, 0},
			{right, bottom},
			{0, bottom}
		});
	}

	void drawBackground(const RECT& rect)
	{
		Size size = { rect.right - 2, rect.bottom - 2 };
		MemoryDC mem(m_hdc, size);

		mem.fillRect({}, size, m_palette.background());
		mem.fillRect({50, 0}, {130, size.height()}, m_palette.done());
		mem.fillRect({132, 0}, {140, size.height()}, m_palette.done());
		mem.fillRect({200, 0}, {500, size.height()}, m_palette.done());
		mem.copyToDst({1, 1});
	}

private:
	void drawBorderByPoints(std::initializer_list<POINT> points)
	{
		HBRUSH color = m_palette.border();
		auto it = points.begin();
		POINT pre = *it;
		for (++it; it != points.end(); ++it) {
			drawLine(pre, *it, color);
			pre = *it;
		}

		drawLine(pre, *points.begin(), color);
	}

	void drawLine(POINT a, POINT b, HBRUSH color)
	{
		if (a.y == b.y) {
			MaxMinValue<LONG> v = { a.x, b.x };
			RECT rect = { v.min() + 1, a.y, v.max(), a.y + 1 };
			FillRect(m_hdc, &rect, color);
		}
		else if (a.x == b.x) {
			MaxMinValue<LONG> v = { a.y, b.y };
			RECT rect = { a.x, v.min() + 1, a.x + 1, v.max() };
			FillRect(m_hdc, &rect, color);
		}
	}

	HDC m_hdc;
	const Palette& m_palette;
};

class ControlClass
{
public:
	static PCWSTR name()
	{
		return L"RandomProgressBar";
	}

	DEF_SINGLETON_METHOD()

private:
	ControlClass()
	{
		WNDCLASS wc = { 0 };
		wc.style = CS_GLOBALCLASS | CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = DefWindowProc;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.lpszClassName = name();
		RegisterClass(&wc);
	}

	~ControlClass()
	{
		UnregisterClass(name(), NULL);
	}
};

class Control
{
public:
	Control()
	{
		m_thunk.Init((DWORD_PTR)_windowProc, this);
	}

	void attach(HWND hwnd)
	{
		assert(hwnd);
		if (!hwnd)
			return;

		m_hwnd = hwnd;
		SetWindowLongPtr(hwnd, GWLP_WNDPROC,
			(LONG_PTR)m_thunk.GetCodeAddress());
	}

private:
	static LRESULT _windowProc(
		Control* that, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		return that->windowProc(uMsg, wParam, lParam);
	}

	LRESULT windowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg) {
		case WM_PAINT:
			customPaint(m_hwnd);
			return 0;
		}

		return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
	}

	void customPaint(HWND hwnd)
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		Painter painter(hdc, m_palette);

		RECT rect = {0};
		GetClientRect(hwnd, &rect);
		painter.drawBorder(rect);
		painter.drawBackground(rect);

		EndPaint(hwnd, &ps);
	}

	ATL::CStdCallThunk m_thunk;
	HWND m_hwnd = NULL;
	Palette m_palette;
};

} // namespace GuiRandomProgress

END_NAMESPACE_MCD
