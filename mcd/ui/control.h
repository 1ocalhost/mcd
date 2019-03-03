#pragma once
#include "window_base.h"
#include "CommCtrl.h"

BEGIN_NAMESPACE_MCD

class BaseCtrl;
class Layout
{
public:
	typedef std::vector<BaseCtrl*> ContentLine;
	typedef std::vector<ContentLine> Content;

	enum Style {
		SpacingLine,
		Optimum,
		Fill,
		Fixed
	};
};

class BaseCtrl : public WindowBase
{
public:
	BaseCtrl(Layout::Style style, int width) :
		m_layoutStyle(style), m_layoutWidth(width)
	{
		setWidth(m_layoutWidth);
	}

	Layout::Style layoutStyle() const { return m_layoutStyle; }
	int layoutWidth() const { return m_layoutWidth; }
	Size size() const { return m_size; }
	int lineHeight() const { return m_lineHeight; }
	int totalWidth() const { return size().width() + m_MarginRight; }
	Point createdPos() const { return m_createdPos; }
	BaseCtrl* following() const { return m_following; }
	ConStrRef uid() const { return m_uid; }

	void setSize(const Size& s) { m_size = s; }
	void setWidth(int width) { m_size.width(width); }
	void setHeight(int height) { m_size.height(height); }

	BaseCtrl* follow(BaseCtrl* who)
	{
		m_following = who;
		return this;
	}

	BaseCtrl* setUid(ConStrRef uid)
	{
		m_uid = uid;
		return this;
	}

	BaseCtrl* setMarginRight(int r)
	{
		m_MarginRight = r;
		return this;
	}

	void init(WindowBase* parent, int lineHeight)
	{
		setParent(parent);
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
			u8to16(title),
			style | WS_CHILD | WS_VISIBLE,
			pos.x(), pos.y() + offsetY,
			size().width(), size().height(),
			parent()->hwnd(),
			NULL,
			GetModuleHandle(NULL),
			NULL
		));
	}

	void windowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_COMMAND:
			onMessageCommand(HIWORD(wParam));
			break;

		case WM_NOTIFY:
			onMessageNotify((LPNMHDR)lParam);
			break;
		}
	}

	virtual bool spacingLineHeight(float* heightRate)
	{
		UNUSED(heightRate);
		return false;
	}

	virtual void onMessageCommand(WORD /*eventType*/) {}
	virtual void onMessageNotify(LPNMHDR /*info*/) {}
	virtual void calcOptimumSize() = 0;
	virtual void create(Point pos) = 0;

private:
	Layout::Style m_layoutStyle;
	int m_lineHeight = 0;
	int m_layoutWidth = 0;
	int m_MarginRight = 0;
	Size m_size;

	std::string m_uid;
	Point m_createdPos;
	BaseCtrl* m_following = nullptr;
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

class SpacingLineCtrl : public BaseCtrl
{
public:
	SpacingLineCtrl(float heightRate) :
		BaseCtrl(Layout::Style::SpacingLine, 0),
		m_heightRate(heightRate) {}

	bool spacingLineHeight(float* heightRate) override
	{
		*heightRate = m_heightRate;
		return true;
	}

	void calcOptimumSize() override
	{
	}

	void create(Point pos) override
	{
		UNUSED(pos);
		assert(0);
	}

private:
	float m_heightRate = 0;
};

class TextCtrl : public BaseCtrl
{
public:
	typedef UiBinding<std::string> *BindingType;

	TextCtrl(
		Layout::Style style = Layout::Style::Optimum,
		int width = 0) :
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
			[this](ConStrRef from, ConStrRef to) {
			if (from != to && guiText() != to)
				setGuiText(to);
		});

		return this;
	}

	void calcOptimumSize() override
	{
		if (!m_binding)
			return;

		Size size = calcTextSize(*m_binding);
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

class RandomProgressCtrl : public BaseCtrl
{
public:
	typedef RandomProgressCtrl Self;
	typedef GuiRandomProgress::Model Model;
	typedef UiBinding<Model> *BindingType;

	RandomProgressCtrl(Layout::Style style, int width = 0) :
		BaseCtrl(style, width) {}

	void calcOptimumSize() override
	{
	}

	void create(Point pos) override
	{
		using namespace GuiRandomProgress;
		ControlClass::get(); // initialize window class
		createWindow(pos, ControlClass::name(), "");
		m_ctrl.attach(hwnd());
	}

	Self* bindModel(BindingType binding)
	{
		m_binding = binding;
		m_binding->subscribe(
			[this](const Model& from, const Model& to) {
			UNUSED(from);
			UNUSED(to);
			m_ctrl.update(m_binding->get());
		});

		return this;
	}

private:
	GuiRandomProgress::Control m_ctrl;
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
	ButtonCtrl(
		Layout::Style style = Layout::Style::Optimum,
		int width = 0) :
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
		Size size_ = calcTextSize(m_text + "wrap");
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

	CheckBoxCtrl(
		Layout::Style style = Layout::Style::Optimum,
		int width = 0) :
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
			[this](const int& from, const int& to) {
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

	EditCtrl(
		Layout::Style style = Layout::Style::Optimum,
		int width = 0) :
		BaseCtrl(style, width)
	{
		setMarginRight(5);
	}

	EditCtrl* bindModel(BindingType binding)
	{
		m_binding = binding;
		m_binding->subscribe(
			[this](ConStrRef from, ConStrRef to) {
			if (from != to && guiText() != to)
				setGuiText(to);
		});

		return this;
	}

	EditCtrl* bindEnabled(CheckBoxCtrl* ctrl)
	{
		m_bindEnabled = ctrl;
		ctrl->whenStateUpdated([this]() {
			setEnabled(m_bindEnabled->modelValue());
		});

		return this;
	}

	void calcOptimumSize() override
	{
		std::string text = m_binding->get() + "wrap";
		Size size_ = calcTextSize(text);
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
	HyperlinkCtrl(
		Layout::Style style = Layout::Style::Optimum,
		int width = 0) :
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
		setSize(calcTextSize(m_text));
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

	EditNumCtrl(
		Layout::Style style = Layout::Style::Optimum,
		int width = 0) :
		EditCtrl(style, width)
	{
		setMarginRight(2);
		m_proxy = "0";
	}

	EditNumCtrl* bindModel(BindingType binding)
	{
		m_proxy = toString((int)*binding);
		EditCtrl::bindModel(&m_proxy);

		m_binding = binding;
		m_binding->subscribe(
			[this](const int& from, const int& to) {
				UNUSED(from);
				m_proxy = toString(to);
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
		if (toNumber(m_proxy, &value)
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

	UpDownCtrl(
		Layout::Style style = Layout::Style::Optimum,
		int width = 0) :
		BaseCtrl(style, width) {}

	template <class Obj, class Fn>
	UpDownCtrl* onChanged(Obj&& that, Fn&& func)
	{
		m_onChanged = std::bind(func, that, _1);
		return this;
	}

	void calcOptimumSize() override
	{
		Size size_ = calcTextSize("aaa");
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

END_NAMESPACE_MCD
