#pragma once
#include "guard.h"
#include <atlbase.h>
#include <atlstdthunk.h>
#include <windowsx.h>


namespace view
{

class Size
{
public:
	Size() {}
	Size(int w, int h) { set(w, h); }

	int width() const { return m_width; }
	int height() const { return m_height; }

	void width(int w) { m_width = w; }
	void height(int h) { m_height = h; }

	void set(int w, int h)
	{
		m_width = w;
		m_height = h;
	}

private:
	int m_width = 0;
	int m_height = 0;
};

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

	bool ask(ConStrRef msg, bool defaultBtn = true, ConStrRef title = {})
	{
		DWORD extraFlag = defaultBtn ? MB_DEFBUTTON1 : MB_DEFBUTTON2;
		return IDYES == MessageBox(0, StringUtil::u8to16(msg),
			StringUtil::u8to16(title),
			extraFlag | MB_ICONQUESTION | MB_YESNO);
	}
};


class UiUtil
{
public:
	UiUtil()
	{
		createUiFont();
	}

	void init(HWND hwnd)
	{
		m_hwnd = hwnd;
	}

	Size calcTextSize(ConStrRef text)
	{
		HDC dc = GetDC(m_hwnd);
		SelectObject(dc, m_uiFont);

		RECT rect = {0};
		auto text_ = StringUtil::u8to16(text);
		DrawText(dc, text_, text_.size(), &rect,
			DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);

		DeleteDC(dc);
		return {rect.right, rect.bottom};
	}

	void resetWindowFont(HWND hwnd)
	{
		EnumChildWindows(hwnd, _resetWindowFont, (LPARAM)m_uiFont.get());
	}

private:
	void createUiFont()
	{
		NONCLIENTMETRICS metrics = {};
		metrics.cbSize = sizeof(metrics);
		SystemParametersInfo(SPI_GETNONCLIENTMETRICS,
			metrics.cbSize, &metrics, 0);
		HFONT font = CreateFontIndirect(&metrics.lfCaptionFont);
		m_uiFont.reset(font);
	}

	static BOOL CALLBACK _resetWindowFont(HWND hwnd, LPARAM lParam)
	{
		SendMessage(hwnd, WM_SETFONT, (WPARAM)lParam, MAKELPARAM(TRUE, 0));
		return TRUE;
	}

	HWND m_hwnd = NULL;
	ResGuard::Font m_uiFont;
};

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

	ConRef get() const
	{
		return m_value;
	}

	operator ConRef() const
	{
		return get();
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
		m_layoutStyle(style), m_layoutWidth(width)
	{
		setWidth(m_layoutWidth);
	}

	UiUtil* util() const { return m_util; }
	HWND parentHwnd() const { return m_parentHwnd; }
	HWND hwnd() const { return m_hwnd; }
	Layout::Style layoutStyle() const { return m_layoutStyle; }
	int layoutWidth() const { return m_layoutWidth; }
	Size size() const { return m_size; }

	//void setHwnd(HWND hwnd) { m_hwnd = hwnd; }
	void setSize(const Size& s) { m_size = s; }
	void setWidth(int width) { m_size.width(width); }
	void setHeight(int height) { m_size.height(height); }

	void onMessageCommand(WORD eventType, HWND src)
	{
		if (!m_hwnd)
			return;

		switch (eventType)
		{
		case BN_CLICKED:
			if (src == m_hwnd)
				onClick();
			break;
		}
	}

	void init(HWND parentHwnd, UiUtil* util, int lineHeight)
	{
		m_parentHwnd = parentHwnd;
		m_util = util;
		m_lineHeight = lineHeight;
		setHeight(lineHeight);

		if (layoutStyle() == Layout::Style::Optimum)
			calcOptimumSize();
	}

	void createWindow(int x, int y,
		PCWSTR typeName, ConStrRef title,
		DWORD style = 0, DWORD exStyle = 0)
	{
		int offsetY = (m_lineHeight - size().height()) / 2;
		m_hwnd = CreateWindowEx(
			exStyle,
			typeName,
			StringUtil::u8to16(title),
			style | WS_CHILD | WS_VISIBLE,
			x, y + offsetY, size().width(), size().height(),
			parentHwnd(),
			NULL,
			GetModuleHandle(NULL),
			NULL
		);
	}

	virtual void onClick() {}
	virtual void calcOptimumSize() = 0;
	virtual void create(int x, int y) = 0;

private:
	UiUtil* m_util = nullptr;
	HWND m_parentHwnd = NULL;
	HWND m_hwnd = NULL;
	Layout::Style m_layoutStyle;
	int m_lineHeight = 0;
	int m_layoutWidth = 0;
	Size m_size;
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

	void calcOptimumSize() override
	{
		Size size = util()->calcTextSize(m_text);
		setSize(size);
	}

	void create(int x, int y) override
	{
		createWindow(x, y, L"Static", m_text);
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

	void calcOptimumSize() override
	{
		Size size = util()->calcTextSize(*m_binding);
		setSize(size);
	}

	void create(int x, int y) override
	{
		createWindow(x, y, L"Edit", *m_binding,
			ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
	}

private:
	BindingType m_binding = nullptr;
};

class SpacingCtrl : public BaseCtrl
{
public:
	SpacingCtrl(Layout::Style style, int width = 0) :
		BaseCtrl(style, width) {}

	void calcOptimumSize() override
	{

	}

	void create(int x, int y) override
	{

	}

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

	void calcOptimumSize() override
	{

	}

	void create(int x, int y) override
	{

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

	void calcOptimumSize() override
	{

	}

	void create(int x, int y) override
	{

	}

private:
	BindingType m_binding = nullptr;
};


class Window
{
public:
	Window(Layout::Content&& layoutContent,
		Size windowSize, ConStrRef windowTitle) :
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

		m_util.init(hwnd);
		m_hwnd = hwnd;
		m_defaultWndProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
		SetWindowLongPtr(hwnd, GWLP_WNDPROC,
			(LONG_PTR)m_thunk.GetCodeAddress());

		createControls();
		m_util.resetWindowFont(hwnd);

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
		case WM_COMMAND:
			onMessageCommand(HIWORD(wParam), (HWND)lParam);
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

	void onMessageCommand(WORD eventType, HWND src)
	{
		switch (eventType)
		{
		case BN_CLICKED:
			for (auto& i : m_resGuard) {
				i->onMessageCommand(eventType, src);
			}
			break;
		}
	}

	HWND createMainWindow()
	{
		Size size = m_windowSize;
		POINT pos = calcScreenCenter(size);
		return CreateWindowEx(
			WS_EX_CLIENTEDGE,
			L"#32770",
			StringUtil::u8to16(m_windowTitle),
			WS_OVERLAPPEDWINDOW,
			pos.x, pos.y,
			size.width(), size.height(),
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

	POINT calcScreenCenter(Size window)
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
			(screen.right + screen.left - window.width()) / 2,
			(screen.bottom + screen.top - window.height()) / 2
		};
	}

	int calcLineHeight()
	{
		return m_util.calcTextSize("a").height() + 8;
	}

	void createControls()
	{
		RECT clientRect = {0};
		GetClientRect(m_hwnd, &clientRect);
		int containerWidth = clientRect.right;
		int lineHeight = calcLineHeight();
		int curPosX = 0;
		int curPosY = 10;

		for (auto& line : m_layoutContent) {
			for (auto& ctrl : line) {
				ctrl->init(m_hwnd, &m_util, lineHeight);
			}

			int total_width = 0;
			for (auto& ctrl : line) {
				if (ctrl->layoutStyle() != Layout::Style::Fill)
					total_width += ctrl->size().width();
			}

			auto iter = std::find_if(line.begin(), line.end(),
				[](BaseCtrl* x) -> bool {
					return x->layoutStyle() == Layout::Style::Fill;
				}
			);

			int remainingWidth = containerWidth - total_width;
			if (iter != line.end() && remainingWidth > 0)
				(*iter)->setWidth(remainingWidth);

			for (auto& ctrl : line) {
				ctrl->create(curPosX, curPosY);
				curPosX += ctrl->size().width();
			}
			return;
		}
	}

	UiUtil m_util;
	ATL::CStdCallThunk m_thunk;
	HWND m_hwnd = NULL;
	WNDPROC m_defaultWndProc = NULL;

	Size m_windowSize;
	std::string m_windowTitle;
	Layout::Content m_layoutContent;
	std::vector<std::unique_ptr<BaseCtrl>> m_resGuard;

	// events
	std::function<bool()> m_eventQuit;
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
				create<SpacingCtrl>(Layout::Fixed, 4),
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
