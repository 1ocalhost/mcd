#pragma once
#include "progress_bar.h"

#pragma warning(push)
#pragma warning(disable: 4091)
#include <Shlobj.h> // SHBrowseForFolder
#pragma warning(pop)


BEGIN_NAMESPACE_MCD

class WindowBase : public InterfaceClass
{
public:
	WindowBase(HWND h = NULL) { setHwnd(h); }

	HWND hwnd() const { return m_hwnd; }
	void setHwnd(HWND hwnd) { m_hwnd = hwnd; }

	WindowBase* parent() const { return m_parent; }
	void setParent(WindowBase* parent) { m_parent = parent; }

	HFONT uiFont() const { return m_uiFont; }
	void setUiFont(HFONT font) { m_uiFont = font; }

	Size calcTextSize(ConStrRef text) const
	{
		if (parent())
			return parent()->calcTextSize(text);

		assert(hwnd() && uiFont());

		if (!hwnd())
			return {};

		if (!uiFont())
			return {};

		Guard::GdiReleaseDc dc(hwnd());
		SelectObject(dc.get(), uiFont());

		RECT rect = { 0 };
		auto text_ = u8to16(text);
		DrawText(dc.get(), text_, (int)text_.size(), &rect,
			DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);

		return { rect.right, rect.bottom };
	}

	std::string guiText() const
	{
		const int kSize = KB(4);
		std::unique_ptr<WCHAR> guard(new WCHAR[kSize]());
		GetWindowText(hwnd(), guard.get(), kSize);
		return u16to8(guard.get());
	}

	void setGuiText(ConStrRef text)
	{
		SetWindowText(hwnd(), u8to16(text));
	}

	LRESULT sendMessage(UINT msg, WPARAM wParam = 0, LPARAM lParam = 0)
	{
		return SendMessage(hwnd(), msg, wParam, lParam);
	}

	bool enabled() const
	{
		return Bool(IsWindowEnabled(hwnd()));
	}

	void setEnabled(bool enabled = true)
	{
		EnableWindow(hwnd(), Bool(enabled));
	}

	LONG_PTR windowLong(int index) const
	{
		return GetWindowLongPtr(hwnd(), index);
	}

	LONG_PTR setWindowLong(int index, LONG_PTR value)
	{
		return SetWindowLongPtr(hwnd(), index, value);
	}

	Size windowSize() const
	{
		RECT r = {0};
		GetWindowRect(hwnd(), &r);
		return {r.right - r.left, r.bottom - r.top};
	}

	Rect windowRect() const
	{
		RECT r = {0};
		GetWindowRect(hwnd(), &r);
		return r;
	}

	Size clientSize() const
	{
		RECT rect = { 0 };
		GetClientRect(hwnd(), &rect);
		return { rect.right, rect.bottom };
	}

	void setClientSize(Size size)
	{
		RECT rect = size.toRect();
		DWORD style = (DWORD)windowLong(GWL_STYLE);
		AdjustWindowRect(&rect, style, FALSE);

		Rect newRect = rect;
		SetWindowPos(hwnd(), NULL, 0, 0,
			newRect.width(), newRect.height(),
			SWP_NOZORDER | SWP_NOMOVE);
	}

	static std::array<POINT, 4> windowVertexes(Point p, Size s)
	{
		int x = p.x();
		int y = p.y();
		int w = s.width();
		int h = s.height();

		return {
			POINT{x, y}, POINT{x+w, y},
			POINT{x, y+h}, POINT{x+w, y+h}
		};
	}

	void setCenterIn(const Rect& rect)
	{
		auto onScreen = [](POINT p) {
			return MonitorFromPoint(p,
				MONITOR_DEFAULTTONULL) != NULL;
		};

		Size s = windowSize();
		Point p = rect.center(s);

		int pass = 0;
		for (auto i : windowVertexes(p, s)) {
			if (onScreen(i))
				pass += 1;
		}

		if (pass < 3) // title bar is visible
			return;

		SetWindowPos(hwnd(), NULL, p.x(), p.y(),
			NULL, NULL, SWP_NOZORDER | SWP_NOSIZE);
	}

	bool childDialog(HWND dialog) const
	{
		bool topLevel = (dialog == GetAncestor(dialog, GA_ROOT));
		bool subWindow = (GetParent(dialog) == hwnd());
		return topLevel && subWindow;
	}

	int messageBox(ConStrRef msg, ConStrRef title, UINT flags) const
	{
		return MessageBox(hwnd(), u8to16(msg), u8to16(title), flags);
	}

	void info(ConStrRef msg, ConStrRef title = "Info") const
	{
		messageBox(msg, title, MB_ICONINFORMATION | MB_OK);
	}

	void error(ConStrRef msg, ConStrRef title = "Error") const
	{
		messageBox(msg, title, MB_ICONERROR | MB_OK);
	}

	bool ask(ConStrRef msg, bool defaultBtn = true,
		ConStrRef title = "Ask") const
	{
		DWORD extraFlag = defaultBtn ? MB_DEFBUTTON1 : MB_DEFBUTTON2;
		return IDYES == messageBox(msg, title,
			extraFlag | MB_ICONQUESTION | MB_YESNO);
	}

	std::string browseForFolder(ConStrRef title = "") const
	{
		std::wstring title16 = u8to16(title);

		TCHAR szDir[MAX_PATH];
		BROWSEINFO bInfo;
		bInfo.hwndOwner = hwnd();
		bInfo.pidlRoot = NULL;
		bInfo.pszDisplayName = szDir;
		bInfo.lpszTitle = title16.c_str();
		bInfo.ulFlags = 0;
		bInfo.lpfn = NULL;
		bInfo.lParam = 0;
		bInfo.iImage = -1;

		LPITEMIDLIST lpItem = SHBrowseForFolder(&bInfo);
		if (lpItem != NULL)
		{
			SHGetPathFromIDList(lpItem, szDir);
			return u16to8(szDir);
		}

		return {};
	}

	void revealPath(ConStrRef path) const
	{
		ShellExecute(hwnd(), L"open", u8to16(path),
			NULL, NULL, SW_SHOWDEFAULT);
	}

private:
	WindowBase* m_parent = nullptr;
	HWND m_hwnd = NULL;
	HFONT m_uiFont = NULL;
};

END_NAMESPACE_MCD
