#pragma once
#include "guard.h"
#include <atlbase.h>
#include <atlstdthunk.h>


namespace view
{


class UiState
{
public:
	enum State
	{
		Initial,
		Working
	};

	void update(State s)
	{
	}
};


class UiMessage
{
public:
	void info(ConStrRef msg, ConStrRef title = {})
	{
		MessageBox(0, StringUtil::u8to16(msg),
			StringUtil::u8to16(title), MB_ICONINFORMATION | MB_OK);
	}

	bool ask(ConStrRef msg, bool defaultYes = true, ConStrRef title = {})
	{
		DWORD extraFlag = defaultYes ? MB_DEFBUTTON1 : MB_DEFBUTTON2;
		return IDYES == MessageBox(0, StringUtil::u8to16(msg),
			StringUtil::u8to16(title),
			extraFlag | MB_ICONQUESTION | MB_YESNO);
	}
};

BOOL CALLBACK EnumChildProc(HWND hWnd, LPARAM lParam)
{
	HFONT hfDefault = (HFONT)lParam; //GetStockObject(DEFAULT_GUI_FONT);
	SendMessage(hWnd, WM_SETFONT, (WPARAM)hfDefault, MAKELPARAM(TRUE, 0));
    return TRUE;
}


void createControls(HWND hwnd)
{
	auto zz = StringUtil::u8to16("123 abc \xE4\xBD\xA0\xE5\xA5\xBD");


	NONCLIENTMETRICS metrics = {};
	metrics.cbSize = sizeof(metrics);
	SystemParametersInfo(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);
	HFONT guiFont = CreateFontIndirect(&metrics.lfCaptionFont);


	// Gets current font
	HDC dc = GetDC(NULL);
	SelectObject(dc, guiFont);

	RECT rect = { 0, 0, 0, 0 };
	DrawText(dc, L"a", 1, &rect,
		DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);

	DeleteDC(dc);
	DWORD heightPixel = rect.bottom + 7;


	//LOGFONT f;
	//GetObject(guiFont, sizeof(f), &f);



	HWND hwndButton = CreateWindow(
		L"BUTTON",  // Predefined class; Unicode assumed 
		L"OK",      // Button text 
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,  // Styles 
		10,         // x position 
		10,         // y position 
		100,        // Button width
		heightPixel,        // Button height
		hwnd,     // Parent window
		NULL,       // No menu.
		(HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
		NULL);      // Pointer not needed.

	//SendMessage(hwndButton, WM_SETFONT, (LPARAM)GetStockObject(DEFAULT_GUI_FONT), true);


	HWND edit = CreateWindowEx(
		WS_EX_CLIENTEDGE,
		L"Edit",  // Predefined class; Unicode assumed 
		zz.c_str(),
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,  // Styles 
		210,         // x position 
		10,         // y position 
		100,        // Button width
		heightPixel,        // Button height
		hwnd,     // Parent window
		NULL,       // No menu.
		(HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
		NULL);      // Pointer not needed.





	EnumChildWindows(hwnd, EnumChildProc, (LPARAM)guiFont);
	//DeleteObject(guiFont); !!!!!!!!!!!


	RECT rect11 = { 0 };
	RECT rect22 = {0};
	GetWindowRect(edit, &rect11);
	GetClientRect(edit, &rect22);

	int _a = 0;

}

class BaseCtrl;
class Layout
{
public:
	typedef std::vector<std::vector<BaseCtrl*>> Content;

	enum Style {
		Optimum,
		Fill,
		Fixed
	};

	struct Margin
	{
		int top;
		int right;
		int bottom;
		int left;
	};
};

class Window
{
public:
	Window(Layout::Content&& layoutContent,
		SIZE windowSize, ConStrRef windowTitle) :
		m_layoutContent(layoutContent),
		m_windowSize(windowSize),
		m_windowTitle(windowTitle)
	{
		for (auto& line : layoutContent) {
			for (auto& ctrl : line) {
				m_resGuard.emplace_back(ctrl);
			}
		}

		m_thunk.Init((DWORD_PTR)_windowProc, this);
	}

	int run(int showState)
	{
		HWND hwnd = createMainWindow();
		if (hwnd == NULL)
			return -1;

		m_hwnd = hwnd;
		m_defaultWndProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
		SetWindowLongPtr(hwnd, GWLP_WNDPROC,
			(LONG_PTR)m_thunk.GetCodeAddress());

		createControls(hwnd);
		ShowWindow(hwnd, showState);
		UpdateWindow(hwnd);
		return messageLoop();
	}

	template <class Obj, class Fn>
	Window& onQuit(Obj&& that, Fn&& func)
	{
		m_eventQuit = std::bind(func, that);
		return *this;
	}

private:
	static LRESULT CALLBACK _windowProc(
		Window* that, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		return that->windowProc(uMsg, wParam, lParam);
	}

	LRESULT CALLBACK windowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_LBUTTONDOWN:
			OutputDebugString(L" 111 WM_LBUTTONDOWN \n");
			break;

		case WM_COMMAND:
			OutputDebugString(L" 222 WM_COMMAND \n");
			break;

		case WM_CLOSE:
			if (m_eventQuit && m_eventQuit()) {
				PostQuitMessage(0);
				return 0;
			}
			break;
		}

		return CallWindowProc(m_defaultWndProc,
			m_hwnd, uMsg, wParam, lParam);
	}

	HWND createMainWindow()
	{
		SIZE size = m_windowSize;
		POINT pos = calcScreenCenter(size);
		return CreateWindowEx(
			WS_EX_CLIENTEDGE,
			L"#32770",
			StringUtil::u8to16(m_windowTitle),
			WS_OVERLAPPEDWINDOW,
			pos.x, pos.y,
			size.cx, size.cy,
			NULL, NULL, GetModuleHandle(NULL), NULL);
	}

	int messageLoop()
	{
		MSG msg;
		while (GetMessage(&msg, NULL, 0, 0) > 0) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		return (int)msg.wParam;
	}

	POINT calcScreenCenter(SIZE window)
	{
		POINT cursorPos;
		GetCursorPos(&cursorPos);

		HMONITOR monitor = MonitorFromPoint(
			cursorPos, MONITOR_DEFAULTTONEAREST);

		MONITORINFOEX mix;
		mix.cbSize = sizeof(mix);
		if (!GetMonitorInfo(monitor, (LPMONITORINFO)&mix)) {
			_should(false);
			mix.rcMonitor = {
				0, 0,
				GetSystemMetrics(SM_CXSCREEN),
				GetSystemMetrics(SM_CYSCREEN)
			};
		}

		const RECT& screen = mix.rcMonitor;
		return {
			(screen.right + screen.left - window.cx) / 2,
			(screen.bottom + screen.top - window.cy) / 2
		};
	}

	ATL::CStdCallThunk m_thunk;
	HWND m_hwnd = NULL;
	WNDPROC m_defaultWndProc = NULL;

	SIZE m_windowSize;
	std::string m_windowTitle;
	Layout::Content m_layoutContent;
	std::vector<std::unique_ptr<BaseCtrl>> m_resGuard;

	// events
	std::function<bool()> m_eventQuit;

};


template <class T>
class UiBinding
{
public:
	typedef const T& ConRef;
	typedef std::function<void(ConRef, ConRef)> Notifier;

	void subscribe(Notifier client)
	{
		m_notifier = client;
	}

	operator ConRef() const
	{
		return m_value;
	}

	void operator =(ConRef newValue)
	{
		if (m_notifier)
			m_notifier(m_value, newValue);

		m_value = newValue;
	}

private:
	T m_value;
	Notifier m_notifier;
};

class BaseCtrl
{
public:
	virtual ~BaseCtrl() {}

	BaseCtrl(Layout::Style style, int width) :
		m_layoutStyle(style), m_layoutWidth(width) {}

private:
	Layout::Style m_layoutStyle;
	int m_layoutWidth;
};

class TextCtrl : public BaseCtrl
{
public:
	TextCtrl(Layout::Style style, int width = 0) :
		BaseCtrl(style, width) {}

	TextCtrl* setDefault(ConStrRef text)
	{
		m_text = text;
		return this;
	}

private:
	std::string m_text;
};

class EditCtrl : public BaseCtrl
{
public:
	typedef UiBinding<std::string> *BindingType;

	EditCtrl(Layout::Style style, int width = 0) :
		BaseCtrl(style, width) {}

	EditCtrl* bind(BindingType binding)
	{
		m_binding = binding;
		return this;
	}

private:
	BindingType m_binding = nullptr;
};

class SpacingCtrl : public BaseCtrl
{
public:
	SpacingCtrl(Layout::Style style, int width = 0) :
		BaseCtrl(style, width) {}

private:
};

class ButtonCtrl : public BaseCtrl
{
public:
	ButtonCtrl(Layout::Style style, int width = 0) :
		BaseCtrl(style, width) {}

	ButtonCtrl* setDefault(ConStrRef text)
	{
		m_text = text;
		return this;
	}

	template <class Obj, class Fn>
	ButtonCtrl* onClick(Obj&& that, Fn&& func)
	{
		m_onClick = std::bind(func, that);
		return this;
	}

private:
	std::string m_text;
	std::function<void()> m_onClick;
};

class NumberCtrl : public BaseCtrl
{
public:
	typedef UiBinding<int> *BindingType;

	NumberCtrl(Layout::Style style, int width = 0) :
		BaseCtrl(style, width) {}

	NumberCtrl* bind(BindingType binding)
	{
		m_binding = binding;
		return this;
	}

private:
	BindingType m_binding = nullptr;
};

template <class T, class... P>
T* create(P... args)
{
	return new T(args...);
}

class View
{
public:
	int run(int showState)
	{
		uiUrl = "https://httpbin.org/get";
		uiConnNum = 3;

		return Window(uiLayout(), {500, 320}, "MCD")
			.onQuit(this, &View::onQuit)
			.run(showState);
	}

private:
	Layout::Content uiLayout()
	{
		return {
			{
				create<TextCtrl>(Layout::Optimum)->setDefault("URL:"),
				create<EditCtrl>(Layout::Fill)->bind(&uiUrl),
			},
			{
				create<TextCtrl>(Layout::Optimum)->setDefault("Save To:"),
				create<EditCtrl>(Layout::Fill)->bind(&uiUrl),
				create<SpacingCtrl>(Layout::Fixed, 20),
				create<ButtonCtrl>(Layout::Optimum)
					->setDefault("Select Folder...")
					->onClick(this, &View::onSelectFolder),
			},
			{
				create<SpacingCtrl>(Layout::Fill),
				create<TextCtrl>(Layout::Optimum)->setDefault("Connections:"),
				create<NumberCtrl>(Layout::Optimum)->bind(&uiConnNum),
				create<ButtonCtrl>(Layout::Optimum)
					->setDefault("Download")
					->onClick(this, &View::onDownload),
			},
		};
	}

public:
	// components
	UiState uiState;
	UiMessage uiMessage;

	// models
	UiBinding<std::string> uiUrl;
	UiBinding<int> uiConnNum;

	// methods
	virtual bool onQuit() = 0;
	virtual void onSelectFolder() = 0;
	virtual void onDownload() = 0;

	HttpConfig httpConfig()
	{
		return HttpConfig();
	}
};

} // namespace view
