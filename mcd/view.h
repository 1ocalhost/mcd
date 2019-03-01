#pragma once
#include "ui/window.h"
#include <Shlobj.h> // SHBrowseForFolder

BEGIN_NAMESPACE_MCD

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
	void attach(HWND hwnd)
	{
		m_hwnd = hwnd;
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
		return MessageBox(m_hwnd, u8to16(msg), u8to16(title), flags);
	}

	HWND m_hwnd = NULL;
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

		return Window(uiLayout(), 500, "MCD")
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
			},
			{
				create<SpacingLineCtrl>((float)0.5)
			},
			{
				create<RandomProgressCtrl>(Layout::Fill)
			},
			{
				create<TextCtrl>(Layout::Fill)->bindModel(&uiStatusText),
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

	void onWindowMade(const Window& win)
	{
		cpUtil.attach(win.hwnd());
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

	UiBinding<std::string> uiStatusText;

	// methods
	virtual bool onQuit() = 0;
	virtual void onDownload() = 0;
};

END_NAMESPACE_MCD
