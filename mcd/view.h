#pragma once
#include "ui/window.h"

BEGIN_NAMESPACE_MCD

class UiState
{
public:
	enum State {
		Ready,
		Waiting,
		Working,
		Aborting
	};

	enum WaitingAnimation {
		Start,
		Wait,
		End
	};

	typedef std::function<void(WaitingAnimation)> OnWaittingFn;

	UiState(const Window* window) : m_window(*window)
	{
	}

	~UiState()
	{
		m_waiting = nullptr;
	}

	bool is(State s) const
	{
		return m_curState == s;
	}

	void update(State s)
	{
		if (s == m_curState)
			return;

		if (s != State::Waiting)
			m_waiting = nullptr;

		if (s == State::Waiting) {
			disableAllCtrl();
			playWaiting();
			ctrlByUid("download").setText("Abort");
		}
		else if (s == State::Aborting) {
			ctrlByUid("download").setEnabled(false);
		}
		else if (s == State::Ready) {
			restoreCtrlState();

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
			m_onWaiting(WaitingAnimation::Start);

			do {
				m_onWaiting(WaitingAnimation::Wait);
				sleep(0.5);
			} while (m_waiting == self);

			m_onWaiting(WaitingAnimation::End);
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
		comState(&m_window)
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
		uiUrl = "https://dldir1.qq.com/qqfile/QQIntl/QQi_PC/QQIntl2.11.exe";
		uiSavingPath = R"(C:\Users\Meow\Music)";
		uiConnNum = 1;
		uiProxyServer = "127.0.0.1:1080";

		uiChkProxyServer = false;
		uiChkUserAgent = false;
		uiChkCookie = false;

		uiProgress = RPCM{};
		uiStatusText = "23% Got, 1.12 MiB/s.";

		comState.onWaiting(std::bind(&View::onWaiting, this, _1));
	}

	void onWaiting(UiState::WaitingAnimation s)
	{
		if (s != UiState::WaitingAnimation::Wait) {
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

	void onDownloadClick()
	{
		if (comState.is(UiState::Ready)) {
			uiUrl = encodeUri(trim(uiUrl));
			if (uiSavingPath.get().empty()) {
				window.info("Please select a folder to save the file.");
				return;
			}

			onDownload();
		}
		else if (comState.is(UiState::Waiting)
			|| comState.is(UiState::Working)) {
			onAbort();
		}
	}

public:
	const Window& window = m_window;
	UiState comState;

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
	virtual void onSelectFolder() = 0;
	virtual void onRevealFolder() = 0;
	virtual void onDownload() = 0;
	virtual void onAbort() = 0;

private:
	Window m_window;
};

END_NAMESPACE_MCD
