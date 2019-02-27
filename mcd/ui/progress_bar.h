#pragma once
#include "../infra/guard.h"

BEGIN_NAMESPACE_MCD

namespace GuiRandomProgress {

class Palette
{
public:
	static Palette& get()
	{
		static Palette obj;
		return obj;
	}

	HBRUSH border() const { return m_border.get(); }
	HBRUSH background() const { return m_background.get(); }
	HBRUSH done() const { return m_done.get(); }

private:
	Palette()
	{
		m_border = CreateSolidBrush(RGB(85, 123, 21));
		m_background = CreateSolidBrush(RGB(205, 244, 181));
		m_done = CreateSolidBrush(RGB(113, 164, 28));
	}

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
	Painter(HDC hdc)
	{
		m_hdc = hdc;
	}

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
		auto& p = Palette::get();

		mem.fillRect({}, size, p.background());
		mem.fillRect({50, 0}, {130, size.height()}, p.done());
		mem.fillRect({132, 0}, {140, size.height()}, p.done());
		mem.fillRect({200, 0}, {500, size.height()}, p.done());
		mem.copyToDst({1, 1});
	}

private:
	void drawBorderByPoints(std::initializer_list<POINT> points)
	{
		HBRUSH color = Palette::get().border();
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
};

class Control
{
public:
	static PCWSTR className()
	{
		return L"RandomProgressBar";
	}

	static Control& get()
	{
		static Control obj;
		return obj;
	}

private:
	Control()
	{
		WNDCLASS wc = { 0 };
		wc.style = CS_GLOBALCLASS | CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = ctrlProc;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.lpszClassName = className();
		RegisterClass(&wc);
	}

	~Control()
	{
		UnregisterClass(className(), NULL);
	}

	static LRESULT CALLBACK ctrlProc(
		HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg) {
		case WM_PAINT:
			customPaint(hwnd);
			return 0;
		}

		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	static void customPaint(HWND hwnd)
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		Painter painter(hdc);

		RECT rect = {0};
		GetClientRect(hwnd, &rect);
		painter.drawBorder(rect);
		painter.drawBackground(rect);

		EndPaint(hwnd, &ps);
	}
};

} // namespace GuiRandomProgress

END_NAMESPACE_MCD
