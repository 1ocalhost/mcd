#pragma once
#include "ui/window.h"
#include <Shlobj.h> // SHBrowseForFolder

BEGIN_NAMESPACE_MCD

class UiState
{
public:
	enum State {
		Ready,
		Working,
		Aborting
	};

	enum Waiting {
		Start,
		Wait,
		End
	};

	typedef std::function<void(Waiting)> OnWaittingFn;

	UiState(const Window* window) : m_window(*window)
	{
	}

	~UiState()
	{
		m_waiting = nullptr;
	}

	State curState() const
	{
		return m_curState;
	}

	void update(State s)
	{
		if (s == m_curState)
			return;

		if (s == State::Working) {
			disableAllCtrl();
			playWaiting();
			ctrlByUid("download").setText("Abort");
		}
		else if (s == State::Aborting) {
			ctrlByUid("download").setEnabled(false);
		}
		else if (s == State::Ready) {
			restoreCtrlState();
			m_waiting = nullptr;

			ctrlByUid("download")
				.setText("Download")
				.setEnabled(true);

			ctrlByUid("status").setText("");
		}

		m_curState = s;
	}

	void onWaiting(OnWaittingFn fn)
	{
		m_onWaiting = fn;
	}

private:
	class CtrlProxy
	{
	public:
		CtrlProxy(BaseCtrl* ctrl) : m_ctrl(ctrl) {}

		CtrlProxy& setText(ConStrRef text)
		{
			if (m_ctrl)
				m_ctrl->setGuiText(text);

			return *this;
		}

		CtrlProxy& setEnabled(bool enabled = true)
		{
			if (m_ctrl)
				m_ctrl->setEnabled(enabled);

			return *this;
		}

	private:
		BaseCtrl* m_ctrl;
	};

	void playWaiting()
	{
		if (!m_onWaiting)
			return;

		m_waiting = new std::thread();
		*m_waiting = std::thread([&](std::thread* self) {
			m_onWaiting(Waiting::Start);

			do {
				m_onWaiting(Waiting::Wait);
				sleep(0.5);
			} while (m_waiting == self);

			m_onWaiting(Waiting::End);
			delete self;
		}, m_waiting);
		m_waiting->detach();
	}

	CtrlProxy ctrlByUid(const char* id)
	{
		BaseCtrl* result = nullptr;
		m_window.eachCtrl([&](BaseCtrl* ctrl) {
			if (ctrl->uid() == id)
				result = ctrl;
		});

		return result;
	}

	void disableAllCtrl()
	{
		m_disabled.clear();
		m_window.eachCtrl([&](BaseCtrl* ctrl) {
			if (!ctrlInWhiteList(ctrl)
				&& ctrl->enabled()) {
				m_disabled.push_back(ctrl);
				ctrl->setEnabled(false);
			}
		});
	}

	bool ctrlInWhiteList(BaseCtrl* ctrl)
	{
		return inArray<NativeString>(
			ctrl->uid().c_str(), "download", "status");
	}

	void restoreCtrlState()
	{
		for (auto i : m_disabled)
			i->setEnabled(true);

		m_disabled.clear();
	}

	State m_curState = Ready;
	const Window& m_window;
	std::vector<BaseCtrl*> m_disabled;

	std::thread* m_waiting = nullptr;
	OnWaittingFn m_onWaiting;
};

class UiUtil
{
public:
	UiUtil(const Window* window) : m_window(*window)
	{
	}

	HWND hwnd() const
	{
		return m_window.hwnd();
	}

	void info(ConStrRef msg, ConStrRef title = {}) const
	{
		messageBox(msg, title, MB_ICONINFORMATION | MB_OK);
	}

	void error(ConStrRef msg, ConStrRef title = {}) const
	{
		messageBox(msg, title, MB_ICONERROR | MB_OK);
	}

	bool ask(ConStrRef msg, bool defaultBtn = true, ConStrRef title = {}) const
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

	void revealPath(ConStrRef path)
	{
		ShellExecute(NULL, L"open", u8to16(path),
			NULL, NULL, SW_SHOWDEFAULT);
	}

private:
	int messageBox(ConStrRef msg, ConStrRef title, UINT flags) const
	{
		return MessageBox(hwnd(), u8to16(msg), u8to16(title), flags);
	}

	const Window& m_window;
};

template <class T, class... P>
T* create(P... args)
{
	return new T(args...);
}

class View
{
public:
	typedef RandomProgressCtrl::Model RPCM;

	View() :
		m_window(uiLayout(), 500, "MCD"),
		comState(&m_window),
		comUtil(&m_window)
	{}

	int run(int showState)
	{
		initModels();
		return m_window
			.onQuit(this, &View::onQuit)
			.run(showState);
	}

private:
	BaseCtrl* createClearButton(UiBinding<std::string>* model)
	{
		auto clearFn = [](UiBinding<std::string>* m) {
			*m = "";
		};

		return create<HyperlinkCtrl>()
			->setDefault("Clear")
			->onClickFn(std::bind(clearFn, model));
	}

	std::vector<BaseCtrl*> createOptionLine(ConStrRef name,
		BaseCtrl* following,
		UiBinding<bool>* modelCheck,
		UiBinding<std::string>* modelEdit)
	{
		CheckBoxCtrl* checkCtrl = create<CheckBoxCtrl>()
			->setDefault(name)->bindModel(modelCheck);

		BaseCtrl* editCtrl = create<EditCtrl>(Layout::Fill)
			->bindModel(modelEdit)->bindEnabled(checkCtrl)->follow(following);

		return {checkCtrl, editCtrl, createClearButton(modelEdit)};
	}

	Layout::Content uiLayout()
	{
		CheckBoxCtrl* chkProxyServer = create<CheckBoxCtrl>()
			->setDefault("HTTP Proxy:")->bindModel(&uiChkProxyServer);

		EditCtrl* editProxyServer = create<EditCtrl>(Layout::Fill)
			->bindModel(&uiProxyServer)->bindEnabled(chkProxyServer);

		return {
			{
				create<TextCtrl>()->setDefault("URL:"),
				create<EditCtrl>(Layout::Fill)->bindModel(&uiUrl),
				createClearButton(&uiUrl)
			},
			{
				create<TextCtrl>()->setDefault("Save To:"),
				create<TextCtrl>(Layout::Fill)
					->bindModel(&uiSavingPath)->setEndWithEllipsis(),
				create<HyperlinkCtrl>()
					->setDefault("Select")
					->onClick(this, &View::onSelectFolder),
				create<HyperlinkCtrl>()
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
				create<TextCtrl>()->setDefault("Connections:"),
				create<EditNumCtrl>()->bindModel(&uiConnNum),
				create<UpDownCtrl>()
					->onChanged(this, &View::onConnectionsChanged),
				create<SpacingCtrl>(Layout::Fixed, 20),
				create<ButtonCtrl>()
					->setDefault("Download")
					->onClick(this, &View::onDownloadClick)
					->setUid("download")
			},
			{
				create<SpacingLineCtrl>((float)0.5)
			},
			{
				create<RandomProgressCtrl>(Layout::Fill)
					->bindModel(&uiProgress)
			},
			{
				create<TextCtrl>(Layout::Fill)
					->bindModel(&uiStatusText)
					->setUid("status")
			}
		};
	}

	void initModels()
	{
		uiUrl = "https://httpbin.org/get";
		uiConnNum = 1;
		uiProxyServer = "127.0.0.1:1080";

		uiChkProxyServer = false;
		uiChkUserAgent = false;
		uiChkCookie = false;

		uiProgress = RPCM{};
		uiStatusText = "23% Got, 1.12 MiB/s.";

		comState.onWaiting(std::bind(&View::onWaiting, this, _1));
	}

	void onWaiting(UiState::Waiting s)
	{
		if (s != UiState::Waiting::Wait) {
			uiStatusText = "";
			return;
		}

		char ch = '.';
		if (uiStatusText.get().size())
			ch = uiStatusText.get().front();

		if (ch == '-')
			uiStatusText = "\\ ...";
		else if (ch == '\\')
			uiStatusText = "/ ...";
		else
			uiStatusText = "- ...";
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
			comUtil.revealPath(uiSavingPath);
	}

	void onSelectFolder()
	{
		uiSavingPath = comUtil.browseForFolder();
	}

	void onDownloadClick()
	{
		if (comState.curState() == UiState::Ready)
			onDownload();
		else if (comState.curState() == UiState::Wait)
			onAbort();
	}

public:
	// components
	UiState comState;
	UiUtil comUtil;

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

	UiBinding<RPCM> uiProgress;
	UiBinding<std::string> uiStatusText;

	// methods
	virtual bool onQuit() = 0;
	virtual void onDownload() = 0;
	virtual void onAbort() = 0;

private:
	Window m_window;
};

END_NAMESPACE_MCD
