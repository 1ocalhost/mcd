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


inline std::string errorString(DWORD code, PCWSTR module = L"")
{
	if (code == 0)
		return {};

	const bool fromSystem = (module[0] == '\0');
	DWORD flag = fromSystem
		? FORMAT_MESSAGE_FROM_SYSTEM : FORMAT_MESSAGE_FROM_HMODULE;

	LPCVOID source = fromSystem ? NULL : GetModuleHandle(module);
	if (!fromSystem && !source) {
		assert(0);
		return {};
	}

	flag |= FORMAT_MESSAGE_ALLOCATE_BUFFER;
	flag |= FORMAT_MESSAGE_IGNORE_INSERTS;

	PSTR message = NULL; // will be in ASCII safely
	DWORD size = FormatMessageA(
		flag,
		source, code, NULL, (PSTR)&message, 0, NULL);

	std::string result(message, (size_t)size);
	LocalFree(message);

	trimRight(&result);
	if (result.size() && *result.rbegin() == '.')
		result.pop_back();

	return result;
}

class App : public View
{
private:
	bool onQuit() override
	{
		return comUtil.ask("Quit?", false);
	}

	void onDownload() override
	{
		uiUrl = encodeUri(trim(uiUrl));
		//Result r = startDownload(uiUrl, uiConnNum, httpConfig());

		//if (r.failed()) {
		//	showError(r);
		//}

		//cpState.update(r.ok() ? UiState::Working : UiState::Ready);


		comState.update(UiState::Working);
	}

	void onAbort() override
	{
		comState.update(UiState::Ready);
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

		if (r.space() == http_api::resultSpace()) {
			std::string msg = errorString(r.code(), L"winhttp.dll");
			if (msg.size())
				ss << " (" << msg << ")";
		}

		comUtil.error(ss.str());
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
		comUtil.info("OK");
		return {};
	}
};

END_NAMESPACE_MCD
