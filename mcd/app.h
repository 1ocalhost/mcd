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
		uiUrl = encodeUri(trim(uiUrl));
		Result r = startDownload(uiUrl, uiConnNum, httpConfig());

		if (r.failed()) {
			showError(r);
		}

		cpState.update(r.ok() ? UiState::Working : UiState::Initial);
	}

	HttpConfig httpConfig()
	{
		HttpConfig config;

		if (uiChkProxyServer)
			config.setHttpProxy(uiProxyServer);

		if (uiChkUserAgent) {
			uiUserAgent = encodeUri(trim(uiUserAgent));
			if (uiUserAgent.get().size())
				config.addHeader(_S("User-Agent: ") + uiUserAgent);
		}

		if (uiChkCookie) {
			uiCookie = encodeUri(trim(uiCookie));
			if (uiCookie.get().size())
				config.addHeader(_S("Cookie: ") + uiCookie);
		}

		return config;
	}

	void showError(const Result& r)
	{
		std::stringstream ss;
		ss << "Error: " << r.space() << "." << r.code();
		//if (strcmp(r.space(), http_api::resultSpace()) == 0)
		//	ss << " (" << "****" << ")";

		cpUtil.info(ss.str());
	}

	static Result checkUrlSupportRange(ConStrRef url,
		const HttpConfig& config)
	{
		_must_not(config.hasHeader("Range"));
		HttpConfig config_(config);
		config_.addHeader("Range: bytes=0-");

		HttpGetRequest http;
		_call(http.init(config_));
		_call(http.open(url));

		if (http.statusCode() != 206)
			return Result("http", http.statusCode());

		bool support = http.headers().has("Content-Range");
		_should(support, url, http);
		_must_or_return(RequireError::httpSupportRange, support, url);

		return {};
	}

	Result startDownload(ConStrRef url, int connNum = 1,
		HttpConfig config = HttpConfig())
	{
		_must(inRange(connNum, 1, 50), connNum);
		if (connNum > 1)
			_call(checkUrlSupportRange(url, config));

		//DownloadTask(url, connNum);
		cpUtil.info("OK");
		return {};
	}
};

END_NAMESPACE_MCD
