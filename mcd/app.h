#pragma once
#include "network/http.h"
#include "view.h"

BEGIN_NAMESPACE_MCD

class UiString : public std::string
{
public:
	typedef UiString Self;

	UiString(ConStrRef str) : std::string(str) {}

	Self& operator +(const UiBinding<std::string>& str)
	{
		append(str.get());
		return *this;
	}
};

typedef UiString _S;

class App : public View
{
private:
	bool onQuit() override
	{
		return cpUtil.ask("Quit?", false);
	}

	void onDownload() override
	{
		uiUrl = encodeUri(uiUrl);

		//uiUrl = (uiUrl.get() + toString(uiConnNum));

		//uiUrl = httpConfig().var_dump();


		//Result r = startDownload(uiUrl, uiConnNum, httpConfig());

		//if (r.failed()) {
		//	showError(r);
		//}

		//cpState.update(r.ok() ? UiState::Working : UiState::Initial);
	}

	HttpConfig httpConfig()
	{
		HttpConfig config;

		if (uiChkProxyServer)
			config.setHttpProxy(uiProxyServer);

		if (uiChkUserAgent)
			uiUserAgent = encodeUri(uiUserAgent);
			config.addHeader(_S("User-Agent: ") + uiUserAgent);

		if (uiChkCookie)
			uiCookie = encodeUri(uiCookie);
			config.addHeader(_S("Cookie: ") + uiCookie);

		return config;
	}

	void showError(const Result& r)
	{
		std::stringstream ss;
		ss << "Error: " << r.space() << "." << r.code();
		ss << std::endl << uiUrl.get();

		cpUtil.info(ss.str());
	}

	static Result checkUrlSupportRange(bool *support, ConStrRef url,
		const HttpConfig& config)
	{
		_must_not(config.hasHeader("Range"));
		HttpConfig config_(config);
		config_.addHeader("Range: bytes=0-");

		HttpGetRequest http;
		_call(http.init(config_));
		_call(http.open(url));

		*support = (http.statusCode() == 206);
		_should(*support, url, http);
		return {};
	}

	static Result startDownload(ConStrRef url, int connNum = 1,
		HttpConfig config = HttpConfig())
	{
		_must(inRange(connNum, 1, 50), connNum);

		if (connNum > 1) {
			bool supported;
			_call(checkUrlSupportRange(&supported, url, config));
			_must_or_return(RequireError::httpSupportRange, supported, url);
		}

		//DownloadTask(url, connNum);
		return {};
	}
};

END_NAMESPACE_MCD
