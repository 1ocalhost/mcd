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
	void info(ConStrRef msg, ConStrRef title = {})
	{
		::MessageBox(0, StringUtil::u8to16(msg),
			StringUtil::u8to16(title), MB_ICONINFORMATION | MB_OK);
	}
};


class View
{
public:
	int run()
	{
		onDownload();
		return 0;
	}

	// methods
	virtual void onDownload() = 0;


	// data
	std::string resUrl()
	{
		return "https://httpbin.org/get";
	}

	int connNum()
	{
		return 2;
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
