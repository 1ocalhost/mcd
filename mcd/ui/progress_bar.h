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
	Guard::GdiBrush m_border;
	Guard::GdiBrush m_background;
	Guard::GdiBrush m_done;
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

	void fillRect(RECT rect, HBRUSH brush)
	{
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
	Guard::GdiBitmap m_bmp;
	Guard::GdiDeleteDc m_dc;
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

typedef std::vector<Range<>> Model;
constexpr int kMaxRange = 1000;

class Painter
{
public:
	Painter(HDC hdc, const Palette& palette, const Model* model) :
		m_hdc(hdc), m_palette(palette), m_model(model) {}

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

	void drawContent(const RECT& rect)
	{
		Size s = {rect.right - 2, rect.bottom - 2};
		MemoryDC dc(m_hdc, s);

		dc.fillRect({0, 0, s.width(), s.height()},
			m_palette.background());
		drawModel(&dc, s);
		dc.copyToDst({1, 1});
	}

private:
	void drawModel(MemoryDC* dc, Size s)
	{
		const int bottom = s.height();
		const double rate = s.width() / (double)kMaxRange;

		if (!m_model)
			return;

		for (auto i : *m_model) {
			if (i.first > kMaxRange || i.second > kMaxRange) {
				assert(0);
				break;
			}

			int left = (int)(i.first * rate);
			int right = (int)(i.second * rate);
			dc->fillRect({left, 0, right, bottom},
				m_palette.done());
		}
	}

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
	const Model* m_model = nullptr;
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

	void update(const Model& m)
	{
		m_model = &m;

		if (m_hwnd)
			InvalidateRect(m_hwnd, NULL, TRUE);
	}

private:
	static LRESULT CALLBACK _windowProc(
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
		Painter painter(hdc, m_palette, m_model);

		RECT rect = {0};
		GetClientRect(hwnd, &rect);
		painter.drawBorder(rect);
		painter.drawContent(rect);

		EndPaint(hwnd, &ps);
	}

	ATL::CStdCallThunk m_thunk;
	HWND m_hwnd = NULL;
	Palette m_palette;
	const Model* m_model = nullptr;
};

} // namespace GuiRandomProgress

END_NAMESPACE_MCD
