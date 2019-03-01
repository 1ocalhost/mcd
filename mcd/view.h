#pragma once
#include "ui/window.h"
#include <Shlobj.h> // SHBrowseForFolder

BEGIN_NAMESPACE_MCD

class UiState
{
public:
	enum State
	{
		Ready,
		Working
	};

	UiState(const Window* window) : m_window(*window)
	{
	}

	void update(State s)
	{
		if (s == State::Working) {
			disableAllCtrl();
		}
		else if (s == State::Ready) {
			if (m_curState == State::Working)
				restoreCtrlState();
		}

		m_curState = s;
	}

private:
	void disableAllCtrl()
	{
		m_disabled.clear();
		m_window.eachCtrl([&](BaseCtrl* ctrl) {
			if (userInputType(ctrl)
				&& !ctrlInWhiteList(ctrl)
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

	static bool userInputType(BaseCtrl* ctrl)
	{
		return dynamic_cast<EditCtrl*>(ctrl)
			|| dynamic_cast<CheckBoxCtrl*>(ctrl)
			|| dynamic_cast<ButtonCtrl*>(ctrl)
			|| dynamic_cast<UpDownCtrl*>(ctrl)
			|| dynamic_cast<HyperlinkCtrl*>(ctrl)
			|| dynamic_cast<TextCtrl*>(ctrl)
		;
	}

	State m_curState = Ready;
	const Window& m_window;
	std::vector<BaseCtrl*> m_disabled;
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
	View() :
		m_window(uiLayout(), 500, "MCD"),
		cpState(&m_window),
		cpUtil(&m_window)
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
					->onClick(this, &View::onDownload)
					->setUid("download")
			},
			{
				create<SpacingLineCtrl>((float)0.5)
			},
			{
				create<RandomProgressCtrl>(Layout::Fill)
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

		uiStatusText = "23% Got, 1.12 MiB/s.";
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

	UiBinding<std::string> uiStatusText;

	// methods
	virtual bool onQuit() = 0;
	virtual void onDownload() = 0;

private:
	Window m_window;
};

END_NAMESPACE_MCD
