#pragma once
#include "ui/window.h"

BEGIN_NAMESPACE_MCD

template <class T, class... P>
T* create(P... args)
{
	return new T(args...);
}

class View
{
public:
	typedef RandomProgressCtrl::Model RPCM;

	View() : m_window(uiLayout(), 500, "MCD") {}

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
					->bindModel(&uiDownload)
					->onClick(this, &View::onDownloadBtn)
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
			}
		};
	}

	void initModels()
	{
		uiUrl = "https://dldir1.qq.com/qqfile/QQIntl/QQi_PC/QQIntl2.11.exe";
		uiSavingPath = R"(C:\Users\Meow\Music)";

		uiChkProxyServer = false;
		uiChkUserAgent = false;
		uiChkCookie = false;

		uiProxyServer = "127.0.0.1:1080";
		uiUserAgent = "";
		uiCookie = "";

		uiConnNum = 1;
		uiDownload = "Download";

		uiProgress = RPCM();
		uiStatusText = "23% Got, 1.12 MiB/s.";
	}

	void onConnectionsChanged(bool upOrDown)
	{
		int value = uiConnNum + 1 * (upOrDown ? 1 : -1);
		if (value < 0)
			value = 0;

		uiConnNum = value;
	}

public:
	const Window& window = m_window;

	// models
	UiBinding<std::string> uiUrl;
	UiBinding<std::string> uiSavingPath;

	UiBinding<bool> uiChkProxyServer;
	UiBinding<bool> uiChkUserAgent;
	UiBinding<bool> uiChkCookie;

	UiBinding<std::string> uiProxyServer;
	UiBinding<std::string> uiUserAgent;
	UiBinding<std::string> uiCookie;

	UiBinding<int> uiConnNum;
	UiBinding<std::string> uiDownload;

	UiBinding<RPCM> uiProgress;
	UiBinding<std::string> uiStatusText;

	// methods
	virtual bool onQuit() = 0;
	virtual void onSelectFolder() = 0;
	virtual void onRevealFolder() = 0;
	virtual void onDownloadBtn() = 0;
	virtual void onAbort() = 0;

private:
	Window m_window;
};

class ViewState : public View
{
public:
	ViewState()
	{
		m_waitingAnimation.init(&uiStatusText,
			[this](WaitingAnimation::Binding *b) {
				*b = textFromStatus(m_curState);
			}
		);
	}

	enum State {
		Ready,
		Waiting,
		Working,
		Aborting,
		Aborted,
		Failed,
		Complete
	};

	void setState(State s)
	{
		if (s == m_curState)
			return;

		if (!inArray(s, {State::Waiting, State::Aborting}))
			m_waitingAnimation.stop();

		switch (s)
		{
		case State::Waiting:
			disableAllCtrl();
			m_waitingAnimation.play();
			uiDownload.ctrl()->setGuiText("Abort");
			break;

		case State::Aborting:
			uiDownload.ctrl()->setEnabled(false);
			break;

		case State::Aborted:
		case State::Failed:
		case State::Complete:
			resetUiToReady(s);
			break;
		}

		m_curState = s;
	}

private:
	void resetUiToReady(State s)
	{
		restoreCtrlState();
		uiDownload.ctrl()->setGuiText("Download");
		uiDownload.ctrl()->setEnabled(true);

		uiStatusText = textFromStatus(s);
		int range = (s == State::Complete)
			? RandomProgressCtrl::kMaxRange : 0;
		uiProgress = {{0, range}};
	}

	const char* textFromStatus(State s)
	{
		switch (s)
		{
		case State::Aborted:
			return "Aborted.";

		case State::Failed:
			return "Failed.";

		case State::Complete:
			return "Complete!";
		}

		return "";
	}

	void disableAllCtrl()
	{
		m_disabled.clear();
		window.eachCtrl([&](BaseCtrl* ctrl) {
			if (!ctrlInWhiteList(ctrl)
				&& ctrl->enabled()) {
				m_disabled.push_back(ctrl);
				ctrl->setEnabled(false);
			}
		});
	}

	bool ctrlInWhiteList(BaseCtrl* ctrl)
	{
		return inArray(ctrl, {
			uiDownload.ctrl(), uiStatusText.ctrl()
		});
	}

	void restoreCtrlState()
	{
		for (auto i : m_disabled)
			i->setEnabled(true);

		m_disabled.clear();
	}

	void onDownloadBtn() override
	{
		switch (m_curState)
		{
		case State::Ready:
		case State::Aborted:
		case State::Failed:
		case State::Complete:
			uiUrl = encodeUri(trim(uiUrl));
			if (uiSavingPath.get().empty()) {
				window.info("Please select a folder to save the file.");
				return;
			}

			onDownload();
			break;

		case State::Waiting:
		case State::Working:
			onAbort();
			break;
		}
	}

	virtual void onDownload() = 0;

	State m_curState = State::Ready;
	std::vector<BaseCtrl*> m_disabled;
	WaitingAnimation m_waitingAnimation;
};

END_NAMESPACE_MCD
