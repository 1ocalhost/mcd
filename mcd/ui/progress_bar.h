#pragma once
#include "../infra/guard.h"

BEGIN_NAMESPACE_MCD

class GuiRandomProgressCtrl
{
public:
	typedef GuiRandomProgressCtrl SelfType;

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

	static PCWSTR className()
	{
		return L"GuiRandomProgressCtrl";
	}

	static GuiRandomProgressCtrl& get()
	{
		static GuiRandomProgressCtrl ctrl;
		return ctrl;
	}

	const Palette& palette() const
	{
		return m_palette;
	}

private:
	GuiRandomProgressCtrl()
	{
		WNDCLASS wc = { 0 };
		wc.style = CS_GLOBALCLASS | CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = ctrlProc;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.lpszClassName = className();
		RegisterClass(&wc);
	}

	~GuiRandomProgressCtrl()
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
		class Painter
		{
		public:
			Painter(HDC hdc)
			{
				m_hdc = hdc;
			}

			void drawBorder(std::initializer_list<POINT> points)
			{
				HBRUSH color = SelfType::get().palette().border();
				auto it = points.begin();
				POINT pre = *it;
				for (++it; it != points.end(); ++it) {
					drawLine(pre, *it, color);
					pre = *it;
				}

				drawLine(pre, *points.begin(), color);
			}

			void drawBackground(RECT rect)
			{
				/////////////////////////////
				LONG width = rect.right - 2;
				LONG height = rect.bottom - 2;

				HBITMAP bmp = CreateCompatibleBitmap(
					m_hdc, width, height); // DeleteObject
				HDC hdc = CreateCompatibleDC(m_hdc); // DeleteObject
				SelectObject(hdc, bmp);

				auto& palette = SelfType::get().palette();

				//rect.left += 1;
				//rect.top += 1;
				rect.right = width;
				rect.bottom = height;
				FillRect(hdc, &rect, palette.background());


				rect.left = 50;
				rect.right = 100;
				FillRect(hdc, &rect, palette.done());

				rect.left = 140;
				rect.right = 170;
				FillRect(hdc, &rect, palette.done());


				BitBlt(m_hdc, 1, 1, width, height, hdc, 0, 0, SRCCOPY);
			}

		private:
			void drawLine(POINT a, POINT b, HBRUSH color)
			{
				if (a.y == b.y) {
					MaxMinValue<LONG> v = {a.x, b.x};
					RECT rect = {v.min()+1, a.y, v.max(), a.y+1};
					FillRect(m_hdc, &rect, color);
				}
				else if (a.x == b.x) {
					MaxMinValue<LONG> v = { a.y, b.y };
					RECT rect = {a.x, v.min()+1, a.x+1, v.max()};
					FillRect(m_hdc, &rect, color);
				}
			}

			HDC m_hdc;
		};


		PAINTSTRUCT ps;
		Painter painter(BeginPaint(hwnd, &ps));

		RECT rect;
		GetClientRect(hwnd, &rect);

		LONG right = rect.right - 1;
		LONG bottom = rect.bottom - 1;
		painter.drawBorder({
			{0, 0},
			{right, 0},
			{right, bottom},
			{0, bottom}
		});

		painter.drawBackground(rect);

		EndPaint(hwnd, &ps);
	}

	Palette m_palette;
};

END_NAMESPACE_MCD
