#pragma once
#include "control.h"
#include <atlbase.h>
#include <atlstdthunk.h>

BEGIN_NAMESPACE_MCD

class Typesetter
{
	class LineMode
	{
	public:
		LineMode(int lineHeight) :
			m_lineHeight(lineHeight)
		{
			m_padding = Typesetter::containerPadding();
			m_curPos.set(m_padding, m_padding);
		}

		Point curPos() const { return m_curPos; }
		void moveOn(int w) { m_curPos.xPlus(w); }
		void moveTo(int x) { m_curPos.x(x); }

		void nextLine()
		{
			const int kLineSpacing = 5;
			m_curPos.yPlus(m_lineHeight + kLineSpacing);
			m_curPos.x(m_padding);
		}

	private:
		int m_padding = 0;
		int m_lineHeight = 0;
		Point m_curPos;
	};

public:
	typedef std::vector<std::unique_ptr<BaseCtrl>> ContentHost;

	Typesetter(const Layout::Content& content) :
		m_layoutContent(content)
	{
		for (auto& line : content) {
			for (auto& ctrl : line) {
				m_resGuard.emplace_back(ctrl);
			}
		}
	}

	const ContentHost& content() const
	{
		return m_resGuard;
	}

	static int containerPadding()
	{
		return 20;
	}

	static int lineHeight(WindowBase* win)
	{
		return win->calcTextSize("a").height() + 8;
	}

	bool parse(WindowBase* parent)
	{
		int lineHeight_ = lineHeight(parent);
		int containerWidth = parent->clientSize().width()
			- containerPadding() * 2;

		if (containerWidth <= 0)
			return false;

		LineMode lineMode(lineHeight_);
		for (auto& line : m_layoutContent) {
			for (auto& ctrl : line)
				ctrl->init(parent, lineHeight_);

			findAndFillWidth(line, containerWidth);
			createInlineControls(line, &lineMode);
			lineMode.nextLine();
		}

		return true;
	}

private:
	void findAndFillWidth(
		const Layout::ContentLine& line,
		int containerWidth)
	{
		int total_width = 0;
		for (auto& ctrl : line) {
			if (ctrl->layoutStyle() != Layout::Style::Fill)
				total_width += ctrl->totalWidth();
		}

		auto iter = std::find_if(line.begin(), line.end(),
			[](BaseCtrl* x) -> bool {
			return x->layoutStyle() == Layout::Style::Fill;
		});

		int remainingWidth = containerWidth - total_width;
		if (iter != line.end() && remainingWidth > 0)
			(*iter)->setWidth(remainingWidth);
	}

	void createInlineControls(
		const Layout::ContentLine& line,
		LineMode* lineMode)
	{
		for (auto& ctrl : line) {
			auto following = ctrl->following();
			if (following) {
				int posX = following->createdPos().x();
				ctrl->setWidth(following->size().width());
				ctrl->create({posX, lineMode->curPos().y()});
				lineMode->moveTo(posX + ctrl->totalWidth());
			}
			else {
				ctrl->create(lineMode->curPos());
				lineMode->moveOn(ctrl->totalWidth());
			}
		}
	}

	Layout::Content m_layoutContent;
	ContentHost m_resGuard;
};

class Window : public WindowBase
{
public:
	Window(Layout::Content&& layoutContent,
		Size windowSize, ConStrRef windowTitle) :
		m_typesetter(layoutContent),
		m_windowSize(windowSize),
		m_windowTitle(windowTitle)
	{
		m_thunk.Init((DWORD_PTR)_windowProc, this);
		m_uiFont.reset(createUiFont());
		setUiFont(m_uiFont.get());
	}

	int run(int showState)
	{
		GuiRandomProgress::Control::get(); // initialize

		if (!createMainWindow())
			return -1;

		if (!createControls())
			return -2;

		ShowWindow(hwnd(), showState);
		UpdateWindow(hwnd());
		return messageLoop();
	}

	template <class Obj, class Fn>
	Window& onWindowMade(Obj&& that, Fn&& func)
	{
		using namespace std::placeholders;
		m_eventInit = std::bind(func, that, _1);
		return *this;
	}

	template <class Obj, class Fn>
	Window& onAllControlsMade(Obj&& that, Fn&& func)
	{
		m_eventAllControlsMade = std::bind(func, that);
		return *this;
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
			dispatchToChild((HWND)lParam, uMsg, wParam, lParam);
			break;

		case WM_NOTIFY:
			dispatchToChild(
				reinterpret_cast<LPNMHDR>(lParam)->hwndFrom,
				uMsg, wParam, lParam);
			break;

		case WM_CLOSE:
			if (m_eventQuit && m_eventQuit()) {
				PostQuitMessage(0);
				return 0;
			}
			break;
		}

		return CallWindowProc(m_defaultWndProc,
			hwnd(), uMsg, wParam, lParam);
	}

	void dispatchToChild(HWND child, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		for (auto& i : m_typesetter.content()) {
			HWND hwnd = i->hwnd();
			if (hwnd && hwnd == child) {
				i->windowProc(uMsg, wParam, lParam);
				break;
			}
		}
	}

	bool createMainWindow()
	{
		Size size = m_windowSize;
		POINT pos = calcScreenCenter(size);
		HWND hwnd = CreateWindowEx(
			WS_EX_CLIENTEDGE,
			L"#32770",
			u8to16(m_windowTitle),
			WS_OVERLAPPEDWINDOW,
			pos.x, pos.y,
			size.width(), size.height(),
			NULL, NULL, GetModuleHandle(NULL), NULL);

		if (!hwnd)
			return false;

		setHwnd(hwnd);
		if (m_eventInit)
			m_eventInit(*this);

		m_defaultWndProc = (WNDPROC)setWindowLong(
			GWLP_WNDPROC, (LONG_PTR)m_thunk.GetCodeAddress());

		return true;
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

	bool createControls()
	{
		if (!m_typesetter.parse(this))
			return false;

		initChildUiFont();
		if (m_eventAllControlsMade)
			m_eventAllControlsMade();

		return true;
	}

	void initChildUiFont()
	{
		for (auto& i : m_typesetter.content()) {
			i->setUiFont(m_uiFont.get());
			i->sendMessage(WM_SETFONT,
				(WPARAM)m_uiFont.get(),
				MAKELPARAM(TRUE, 0));
		}
	}

	static HFONT createUiFont()
	{
		NONCLIENTMETRICS metrics = {};
		metrics.cbSize = sizeof(metrics);
		SystemParametersInfo(SPI_GETNONCLIENTMETRICS,
			metrics.cbSize, &metrics, 0);
		return CreateFontIndirect(&metrics.lfCaptionFont);
	}

	ATL::CStdCallThunk m_thunk;
	WNDPROC m_defaultWndProc = NULL;

	Typesetter m_typesetter;
	ResGuard::GdiFont m_uiFont;

	Size m_windowSize;
	std::string m_windowTitle;

	// events
	std::function<void(const Window&)> m_eventInit;
	std::function<void()> m_eventAllControlsMade;
	std::function<bool()> m_eventQuit;
};

END_NAMESPACE_MCD
