#pragma once
#include "guard.h"


namespace view
{


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
};


class View
{
public:
	void run()
	{

	}

	// methods
	virtual void onDownload() = 0;


	// data
	std::string resUrl()
	{
		return "";
	}

	int connNum()
	{
		return 0;
	}

	HttpConfig httpConfig()
	{
		return HttpConfig();
	}

	UiState& uiState() { return m_uiState; }
	UiMessage& uiMessage() { return m_uiMessage; }

private:
	UiState m_uiState;
	UiMessage m_uiMessage;
};

} // namespace view
