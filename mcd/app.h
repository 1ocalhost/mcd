#pragma once
#include "http.h"
#include "view.h"


namespace app
{

using namespace	view;


class DlWorker
{

};



class App : public View
{
private:
	bool onQuit() override
	{
		return uiMessage.ask("Quit?", false);
	}

	void onSelectFolder() override
	{
		uiMessage.info("onSelectFolder");
	}

	void onDownload() override
	{
		showError(InternalError::assertFailed());
		return;


		Result r = startDownload(uiUrl, uiConnNum, httpConfig());

		if (r.failed()) {
			showError(r);
		}

		uiState.update(r.ok() ? UiState::Working : UiState::Initial);
	}

	void showError(const Result& r)
	{
		std::stringstream ss;
		ss << "Error: " << r.space() << "." << r.code();
		ss << std::endl << uiUrl.get();

		uiMessage.info(ss.str());
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

} // namespace app