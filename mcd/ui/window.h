#pragma once
#include "control.h"
#include <atlbase.h>
#include <atlstdthunk.h>

BEGIN_NAMESPACE_MCD

class Typesetter123
{
public:
	typedef std::vector<std::unique_ptr<BaseCtrl>> ContentHost;

	Typesetter123(const Layout::Content& content) :
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

	bool parse(WindowBase* parentWindow, RECT clientRect, int lineHeight)
	{
		const int kContainerPadding = 10;

		int containerWidth = clientRect.right - kContainerPadding * 2;
		if (containerWidth <= 0)
			return false;

		class Typesetter
		{
		public:
			Typesetter(int padding, int lineHeight) :
				m_padding(padding), m_lineHeight(lineHeight)
			{
				m_curPos.set(padding, padding);
			}

			void moveOn(int w)
			{
				m_curPos.xPlus(w);
			}

			void moveTo(int x)
			{
				m_curPos.x(x);
			}

			void nextLine()
			{
				const int kLineSpacing = 5;
				m_curPos.yPlus(m_lineHeight + kLineSpacing);
				m_curPos.x(m_padding);
			}

			Point curPos() const { return m_curPos; }

		private:
			int m_padding = 0;
			int m_lineHeight = 0;
			Point m_curPos;
		};

		Typesetter typesetter(kContainerPadding, lineHeight);

		for (auto& line : m_layoutContent) {
			for (auto& ctrl : line) {
				ctrl->init(parentWindow, lineHeight);
			}

			int total_width = 0;
			for (auto& ctrl : line) {
				if (ctrl->layoutStyle() != Layout::Style::Fill)
					total_width += ctrl->totalWidth();
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
				auto following = ctrl->following();
				if (following) {
					int posX = following->createdPos().x();
					ctrl->setWidth(following->size().width());
					ctrl->create({ posX, typesetter.curPos().y() });
					typesetter.moveTo(posX + ctrl->totalWidth());
				}
				else {
					ctrl->create(typesetter.curPos());
					typesetter.moveOn(ctrl->totalWidth());
				}
			}

			typesetter.nextLine();
		}


		return true;
	}

private:
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
		GuiRandomProgressCtrl::get(); // initialize

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
		RECT clientRect = {0};
		GetClientRect(hwnd(), &clientRect);

		int lineHeight = calcTextSize("a").height() + 8;
		if (!m_typesetter.parse(this, clientRect, lineHeight))
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

	Typesetter123 m_typesetter;
	ResGuard::GdiFont m_uiFont;

	Size m_windowSize;
	std::string m_windowTitle;

	// events
	std::function<void(const Window&)> m_eventInit;
	std::function<void()> m_eventAllControlsMade;
	std::function<bool()> m_eventQuit;
};

END_NAMESPACE_MCD
