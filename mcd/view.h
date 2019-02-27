#pragma once
#include "guard.h"
#include <atlbase.h>
#include <atlstdthunk.h>
//#include <CommCtrl.h>
#include <windowsx.h>
#include <Shlobj.h> // SHBrowseForFolder

namespace view
{

class GuiWindow
{
public:
	HWND hwnd() const { return m_hwnd; }
	void setHwnd(HWND hwnd) { m_hwnd = hwnd; }

	std::string guiText()
	{
		const int kSize = KB(5);
		std::unique_ptr<WCHAR> guard(new WCHAR[kSize]());
		GetWindowText(hwnd(), guard.get(), kSize);
		return StringUtil::u16to8(guard.get());
	}

	void setGuiText(ConStrRef text)
	{
		SetWindowText(hwnd(), StringUtil::u8to16(text));
	}

	LRESULT sendMessage(UINT msg, WPARAM wParam = 0, LPARAM lParam = 0)
	{
		return SendMessage(hwnd(), msg, wParam, lParam);
	}

	void setEnabled(bool enabled = true)
	{
		EnableWindow(hwnd(), enabled ? TRUE : FALSE);
	}

private:
	HWND m_hwnd = NULL;
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
		UNUSED(s);
	}
};


class UiUtil
{
public:
	UiUtil(HWND hwnd = NULL) : m_hwnd(hwnd) {}

	void info(ConStrRef msg, ConStrRef title = {}) const
	{
		MessageBox(m_hwnd, StringUtil::u8to16(msg),
			StringUtil::u8to16(title), MB_ICONINFORMATION | MB_OK);
	}

	bool ask(ConStrRef msg, bool defaultBtn = true, ConStrRef title = {}) const
	{
		DWORD extraFlag = defaultBtn ? MB_DEFBUTTON1 : MB_DEFBUTTON2;
		return IDYES == MessageBox(m_hwnd, StringUtil::u8to16(msg),
			StringUtil::u8to16(title),
			extraFlag | MB_ICONQUESTION | MB_YESNO);
	}

	std::string browseForFolder(ConStrRef title = "") const
	{
		std::wstring title16 = StringUtil::u8to16(title);

		TCHAR szDir[MAX_PATH];
		BROWSEINFO bInfo;
		bInfo.hwndOwner = m_hwnd;
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
			return StringUtil::u16to8(szDir);
		}

		return {};
	}

	void revealPath(ConStrRef path)
	{
		ShellExecute(NULL, L"open", StringUtil::u8to16(path),
			NULL, NULL, SW_SHOWDEFAULT);
	}

private:
	HWND m_hwnd = NULL;
};


class GuiHelper
{
public:
	GuiHelper()
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
		DrawText(dc, text_, (int)text_.size(), &rect,
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

	void setDirectly(ConRef newValue)
	{
		m_value = newValue;
	}

private:
	T m_value;
	Notifier m_notifier;
};

class BaseCtrl : public GuiWindow
{
public:
	virtual ~BaseCtrl() {}

	BaseCtrl(Layout::Style style, int width) :
		m_layoutStyle(style), m_layoutWidth(width)
	{
		setWidth(m_layoutWidth);
	}

	GuiHelper* guiHelper() const { return m_guiHelper; }
	HWND parentHwnd() const { return m_parentHwnd; }
	Layout::Style layoutStyle() const { return m_layoutStyle; }
	int layoutWidth() const { return m_layoutWidth; }
	Size size() const { return m_size; }
	int totalWidth() const { return size().width() + m_MarginRight; }
	Point createdPos() const { return m_createdPos; }
	BaseCtrl* following() const { return m_following; }

	void setSize(const Size& s) { m_size = s; }
	void setWidth(int width) { m_size.width(width); }
	void setHeight(int height) { m_size.height(height); }

	BaseCtrl* follow(BaseCtrl* who)
	{
		m_following = who;
		return this;
	}

	BaseCtrl* setMarginRight(int r)
	{
		m_MarginRight = r;
		return this;
	}

	void init(HWND parentHwnd, GuiHelper* guiHelper, int lineHeight)
	{
		m_parentHwnd = parentHwnd;
		m_guiHelper = guiHelper;
		m_lineHeight = lineHeight;
		setHeight(lineHeight);

		calcOptimumSize();
	}

	void createWindow(Point pos,
		PCWSTR typeName, ConStrRef title,
		DWORD style = 0, DWORD exStyle = 0)
	{
		m_createdPos = pos;
		int offsetY = (m_lineHeight - size().height()) / 2;
		setHwnd(CreateWindowEx(
			exStyle,
			typeName,
			StringUtil::u8to16(title),
			style | WS_CHILD | WS_VISIBLE,
			pos.x(), pos.y() + offsetY,
			size().width(), size().height(),
			parentHwnd(),
			NULL,
			GetModuleHandle(NULL),
			NULL
		));
	}

	virtual void onMessageCommand(WORD /*eventType*/) {}
	virtual void onMessageNotify(LPNMHDR /*info*/) {}
	virtual void calcOptimumSize() = 0;
	virtual void create(Point pos) = 0;

private:
	GuiHelper* m_guiHelper = nullptr;
	HWND m_parentHwnd = NULL;
	Layout::Style m_layoutStyle;
	int m_lineHeight = 0;
	int m_layoutWidth = 0;
	int m_MarginRight = 0;
	Size m_size;

	Point m_createdPos;
	BaseCtrl* m_following = nullptr;
};

class TextCtrl : public BaseCtrl
{
public:
	typedef UiBinding<std::string> *BindingType;


	TextCtrl(Layout::Style style, int width = 0) :
		BaseCtrl(style, width)
	{
		setMarginRight(5);
	}

	TextCtrl* setDefault(ConStrRef text)
	{
		m_default = text;
		return bindModel(&m_default);
	}

	TextCtrl* setEndWithEllipsis(bool enabled = true, bool word = false)
	{
		m_endEllipsisflag = ((enabled ? SS_ENDELLIPSIS : 0)
			| (word ? SS_WORDELLIPSIS : SS_PATHELLIPSIS));
		return this;
	}

	TextCtrl* bindModel(BindingType binding)
	{
		m_binding = binding;
		m_binding->subscribe(
			[=](ConStrRef from, ConStrRef to) {
			if (from != to && guiText() != to)
				setGuiText(to);
		});

		return this;
	}

	void calcOptimumSize() override
	{
		if (!m_binding)
			return;

		Size size = guiHelper()->calcTextSize(*m_binding);
		setSize(size);
	}

	void create(Point pos) override
	{
		if (!m_binding)
			return;

		createWindow(pos, L"Static", *m_binding, m_endEllipsisflag);
	}

private:
	DWORD m_endEllipsisflag = 0;
	UiBinding<std::string> m_default;
	BindingType m_binding = nullptr;
};

class SpacingCtrl : public BaseCtrl
{
public:
	SpacingCtrl(Layout::Style style, int width = 0) :
		BaseCtrl(style, width) {}

	// do nothing
	void calcOptimumSize() override {}
	void create(Point /*pos*/) override {}
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

	ButtonCtrl* onClickFn(std::function<void()> onClick)
	{
		m_onClick = onClick;
		return this;
	}

	ConStrRef defaultText() const
	{
		return m_text;
	}

	void notifyOnClick()
	{
		if (m_onClick)
			m_onClick();
	}

private:
	void calcOptimumSize() override
	{
		Size size_ = guiHelper()->calcTextSize(m_text + "wrap");
		setSize({size_.width(), size().height() + 2});
	}

	void create(Point pos) override
	{
		createWindow(pos, L"Button", m_text);
	}

	void onMessageCommand(WORD eventType) override
	{
		if (eventType == BN_CLICKED && m_onClick)
			m_onClick();
	}

	std::string m_text;
	std::function<void()> m_onClick;
};

class CheckBoxCtrl : public ButtonCtrl
{
public:
	typedef UiBinding<bool> *BindingType;

	CheckBoxCtrl(Layout::Style style, int width = 0) :
		ButtonCtrl(style, width) {}

	CheckBoxCtrl* setDefault(ConStrRef text)
	{
		ButtonCtrl::setDefault(text);
		return this;
	}

	CheckBoxCtrl* bindModel(BindingType binding)
	{
		m_binding = binding;
		m_binding->subscribe(
			[=](const int& from, const int& to) {
			if (from != to)
				setChecked(to);
		});

		return this;
	}

	bool modelValue()
	{
		return m_binding && *m_binding;
	}

	void setChecked(bool checked = true)
	{
		WPARAM param = checked ? BST_CHECKED : BST_UNCHECKED;
		sendMessage(BM_SETCHECK, param);
	}

	bool checked()
	{
		return sendMessage(BM_GETCHECK) == BST_CHECKED;
	}

	void whenStateUpdated(std::function<void()> todo)
	{
		m_whenStateUpdated = todo;
	}

private:
	void create(Point pos) override
	{
		createWindow(pos, L"Button", defaultText(), BS_AUTOCHECKBOX);
		if (m_binding)
			setChecked(*m_binding);
	}

	void onMessageCommand(WORD eventType) override
	{
		if (eventType == BN_CLICKED) {
			if (m_binding)
				m_binding->setDirectly(checked());
			
			if (m_whenStateUpdated)
				m_whenStateUpdated();

			notifyOnClick();
		}
	}

	BindingType m_binding = nullptr;
	std::function<void()> m_whenStateUpdated;
};

class EditCtrl : public BaseCtrl
{
public:
	typedef UiBinding<std::string> *BindingType;

	EditCtrl(Layout::Style style, int width = 0) :
		BaseCtrl(style, width)
	{
		setMarginRight(5);
	}

	EditCtrl* bindModel(BindingType binding)
	{
		m_binding = binding;
		m_binding->subscribe(
			[=](ConStrRef from, ConStrRef to) {
			if (from != to && guiText() != to)
				setGuiText(to);
		});

		return this;
	}

	EditCtrl* bindEnabled(CheckBoxCtrl* ctrl)
	{
		m_bindEnabled = ctrl;
		ctrl->whenStateUpdated([=]() {
			setEnabled(m_bindEnabled->modelValue());
		});

		return this;
	}

	void calcOptimumSize() override
	{
		std::string text = m_binding->get() + "wrap";
		Size size_ = guiHelper()->calcTextSize(text);
		setWidth(size_.width());
	}

	void create(Point pos) override
	{
		createWindow(pos, L"Edit", *m_binding,
			ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);

		if (m_bindEnabled)
			setEnabled(m_bindEnabled->modelValue());
	}

	void onMessageCommand(WORD eventType) override
	{
		if (eventType == EN_CHANGE)
			m_binding->setDirectly(guiText());
	}

private:
	BindingType m_binding = nullptr;
	CheckBoxCtrl* m_bindEnabled = nullptr;
};

class HyperlinkCtrl : public BaseCtrl
{
public:
	HyperlinkCtrl(Layout::Style style, int width = 0) :
		BaseCtrl(style, width)
	{
		setMarginRight(5);
	}

	HyperlinkCtrl* setDefault(ConStrRef text)
	{
		m_text = text;
		return this;
	}

	template <class Obj, class Fn>
	HyperlinkCtrl* onClick(Obj&& that, Fn&& func)
	{
		m_onClick = std::bind(func, that);
		return this;
	}

	HyperlinkCtrl* onClickFn(std::function<void()> onClick)
	{
		m_onClick = onClick;
		return this;
	}

	void calcOptimumSize() override
	{
		setSize(guiHelper()->calcTextSize(m_text));
	}

	void create(Point pos) override
	{
		std::stringstream ss;
		ss << "<a>" << m_text << "</a>";
		createWindow(pos, L"SysLink", ss.str());
	}

	void onMessageNotify(LPNMHDR info) override
	{
		UINT code = info->code;
		if (code == NM_CLICK || code == NM_RETURN) {
			if (m_onClick)
				m_onClick();
		}
	}

private:
	std::string m_text;
	std::function<void()> m_onClick;
};

class EditNumCtrl : public EditCtrl
{
public:
	typedef UiBinding<int> *BindingType;

	EditNumCtrl(Layout::Style style, int width = 0) :
		EditCtrl(style, width)
	{
		setMarginRight(2);
		m_proxy = "0";
	}

	EditNumCtrl* bindModel(BindingType binding)
	{
		m_proxy = StringUtil::toString((int)*binding);
		EditCtrl::bindModel(&m_proxy);

		m_binding = binding;
		m_binding->subscribe(
			[=](const int& from, const int& to) {
				UNUSED(from);
				m_proxy = StringUtil::toString(to);
		});

		return this;
	}

	void create(Point pos) override
	{
		createWindow(pos, L"Edit", m_proxy,
			ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE);
	}

	void onMessageCommand(WORD eventType) override
	{
		EditCtrl::onMessageCommand(eventType);
		const int kMax = std::numeric_limits<int>::max();

		unsigned long value = 0;
		if (StringUtil::toNumber(m_proxy, &value)
			&& value <= (unsigned long)kMax) {
			m_binding->setDirectly((int)value);
			return;
		}

		m_binding->setDirectly(0);
		m_proxy = "0";
	}

private:
	UiBinding<std::string> m_proxy;
	BindingType m_binding = nullptr;
};

class UpDownCtrl : public BaseCtrl
{
public:
	typedef UiBinding<int> *BindingType;

	UpDownCtrl(Layout::Style style, int width = 0) :
		BaseCtrl(style, width) {}

	template <class Obj, class Fn>
	UpDownCtrl* onChanged(Obj&& that, Fn&& func)
	{
		using namespace std::placeholders;
		m_onChanged = std::bind(func, that, _1);
		return this;
	}

	void calcOptimumSize() override
	{
		Size size_ = guiHelper()->calcTextSize("aaa");
		setWidth(size_.width());
	}

	void create(Point pos) override
	{
		createWindow(pos, L"msctls_updown32", "");
	}

	void onMessageNotify(LPNMHDR info) override
	{
		UINT code = info->code;
		if (code == UDN_DELTAPOS) {
			if (m_onChanged)
				m_onChanged(((LPNMUPDOWN)info)->iDelta < 0);
		}
	}

private:
	std::function<void(bool /*upOrDown*/)> m_onChanged;
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

		m_guiHelper.init(hwnd);
		m_hwnd = hwnd;
		if (m_eventInit)
			m_eventInit(*this);

		m_defaultWndProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
		SetWindowLongPtr(hwnd, GWLP_WNDPROC,
			(LONG_PTR)m_thunk.GetCodeAddress());

		createControls();
		m_guiHelper.resetWindowFont(hwnd);
		if (m_eventAllControlsMade)
			m_eventAllControlsMade();

		ShowWindow(hwnd, showState);
		UpdateWindow(hwnd);
		return messageLoop();
	}

	HWND hwnd() const
	{
		return m_hwnd;
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
			onMessageCommand((HWND)lParam, HIWORD(wParam));
			break;

		case WM_NOTIFY:
			onMessageNotify((LPNMHDR)lParam);
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

	void onMessageCommand(HWND from, WORD eventType)
	{
		for (auto& i : m_resGuard) {
			HWND hwnd = i->hwnd();
			if (hwnd && hwnd == from) {
				i->onMessageCommand(eventType);
				break;
			}
		}
	}

	void onMessageNotify(LPNMHDR info)
	{
		HWND from = info->hwndFrom;
		for (auto& i : m_resGuard) {
			HWND hwnd = i->hwnd();
			if (hwnd && hwnd == from) {
				i->onMessageNotify(info);
				break;
			}
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
		return m_guiHelper.calcTextSize("a").height() + 8;
	}

	void createControls()
	{
		RECT clientRect = {0};
		GetClientRect(m_hwnd, &clientRect);

		const int kContainerPadding = 10;

		int containerWidth = clientRect.right - kContainerPadding*2;
		int lineHeight = calcLineHeight();
		if (containerWidth <= 0)
			return;

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
				ctrl->init(m_hwnd, &m_guiHelper, lineHeight);
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
					ctrl->create({posX, typesetter.curPos().y()});
					typesetter.moveTo(posX + ctrl->totalWidth());
				}
				else {
					ctrl->create(typesetter.curPos());
					typesetter.moveOn(ctrl->totalWidth());
				}
			}

			typesetter.nextLine();
		}
	}

	UiUtil m_uiUtil;
	GuiHelper m_guiHelper;

	ATL::CStdCallThunk m_thunk;
	HWND m_hwnd = NULL;
	WNDPROC m_defaultWndProc = NULL;

	Size m_windowSize;
	std::string m_windowTitle;
	Layout::Content m_layoutContent;
	std::vector<std::unique_ptr<BaseCtrl>> m_resGuard;

	// events
	std::function<void(const Window&)> m_eventInit;
	std::function<void()> m_eventAllControlsMade;
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
		initModels();

		return Window(uiLayout(), {500, 320}, "MCD")
			.onWindowMade(this, &View::onWindowMade)
			.onAllControlsMade(this, &View::onAllControlsMade)
			.onQuit(this, &View::onQuit)
			.run(showState);
	}

private:
	BaseCtrl* createClearButton(UiBinding<std::string>* model)
	{
		return create<HyperlinkCtrl>(Layout::Optimum)
			->setDefault("Clear")
			->onClickFn(std::bind(
				[](UiBinding<std::string>* m) { *m = ""; }, model));
	}

	std::vector<BaseCtrl*> createOptionLine(ConStrRef name,
		BaseCtrl* following,
		UiBinding<bool>* modelCheck,
		UiBinding<std::string>* modelEdit)
	{
		CheckBoxCtrl* checkCtrl = create<CheckBoxCtrl>(Layout::Optimum)
			->setDefault(name)->bindModel(modelCheck);

		BaseCtrl* editCtrl = create<EditCtrl>(Layout::Fill)
			->bindModel(modelEdit)->bindEnabled(checkCtrl)->follow(following);

		return {checkCtrl, editCtrl, createClearButton(modelEdit)};
	}

	Layout::Content uiLayout()
	{
		CheckBoxCtrl* chkProxyServer = create<CheckBoxCtrl>(Layout::Optimum)
			->setDefault("HTTP Proxy:")->bindModel(&uiChkProxyServer);

		EditCtrl* editProxyServer = create<EditCtrl>(Layout::Fill)
			->bindModel(&uiProxyServer)->bindEnabled(chkProxyServer);

		return {
			{
				create<TextCtrl>(Layout::Optimum)->setDefault("URL:"),
				create<EditCtrl>(Layout::Fill)->bindModel(&uiUrl),
				createClearButton(&uiUrl)
			},
			{
				create<TextCtrl>(Layout::Optimum)->setDefault("Save To:"),
				create<TextCtrl>(Layout::Fill)
					->bindModel(&uiSavingPath)->setEndWithEllipsis(),
				create<HyperlinkCtrl>(Layout::Optimum)
					->setDefault("Select")
					->onClick(this, &View::onSelectFolder),
				create<HyperlinkCtrl>(Layout::Optimum)
					->setDefault("Reveal")
					->onClick(this, &View::onRevealFolder)
			},
			{
				chkProxyServer,
				editProxyServer,
				createClearButton(&uiProxyServer),
				create<SpacingCtrl>(Layout::Fixed, 100)
			},
			createOptionLine("User-Agent:",
				editProxyServer,
				&uiChkUserAgent,
				&uiUserAgent),
			createOptionLine("Cookie:",
				editProxyServer,
				&uiChkCookie,
				&uiCookie),
			{
				create<SpacingCtrl>(Layout::Fill),
				create<TextCtrl>(Layout::Optimum)->setDefault("Connections:"),
				create<EditNumCtrl>(Layout::Optimum)->bindModel(&uiConnNum),
				create<UpDownCtrl>(Layout::Optimum)
					->onChanged(this, &View::onConnectionsChanged),
				create<SpacingCtrl>(Layout::Fixed, 20),
				create<ButtonCtrl>(Layout::Optimum)
					->setDefault("Download")
					->onClick(this, &View::onDownload)
			}
		};
	}

	void initModels()
	{
		uiUrl = "https://httpbin.org/get";
		uiConnNum = 3;
		uiProxyServer = "127.0.0.1:1080";

		uiChkProxyServer = false;
		uiChkUserAgent = true;
		uiChkCookie = false;
	}

	void onWindowMade(const Window& win)
	{
		cpUtil = UiUtil(win.hwnd());
	}

	void onAllControlsMade()
	{

	}

	void onConnectionsChanged(bool upOrDown)
	{
		int value = uiConnNum + 1 * (upOrDown ? 1 : -1);
		if (value < 0)
			value = 0;

		uiConnNum = value;
	}

	void onRevealFolder()
	{
		if (uiSavingPath.get().size())
			cpUtil.revealPath(uiSavingPath);
	}

	void onSelectFolder()
	{
		uiSavingPath = cpUtil.browseForFolder();
	}

public:
	// components
	UiState cpState;
	UiUtil cpUtil;

	// models
	UiBinding<std::string> uiUrl;
	UiBinding<std::string> uiSavingPath;
	UiBinding<int> uiConnNum;

	UiBinding<std::string> uiProxyServer;
	UiBinding<std::string> uiUserAgent;
	UiBinding<std::string> uiCookie;

	UiBinding<bool> uiChkProxyServer;
	UiBinding<bool> uiChkUserAgent;
	UiBinding<bool> uiChkCookie;

	// methods
	virtual bool onQuit() = 0;
	virtual void onDownload() = 0;

	HttpConfig httpConfig()
	{
		return HttpConfig();
	}
};

} // namespace view
