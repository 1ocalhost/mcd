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
	void onDownload() override
	{
		bool result = startDownload(
			resUrl(), connNum(), httpConfig());

		if (!result) {
			//showError();
		}

		uiState().update(result ? UiState::Working : UiState::Initial);
	}

	static bool checkUrlSupportRange(ConStrRef url,
		const HttpConfig& config, Bool* supportRange)
	{
		or_err(!config.hasHeader("Range"));
		HttpConfig config_(config);
		config_.addHeader("Range: bytes=0-");

		HttpGetRequest http(config_);
		HttpResult r = http.open(url);
		or_warn(r, url, r);
		or_warn(r.statusCode() == 206, url, r);
		return true;
	}

	static bool startDownload(ConStrRef url, int connNum = 1,
		HttpConfig config = HttpConfig())
	{
		or_err(inRange(connNum, 1, 50), connNum);

		if (connNum > 1) {
			Bool supported;
			or_err(checkUrlSupportRange(url, config, &supported));
			or_warn(supported, url);
		}

		//DownloadTask(url, connNum);
		return true;
	}
};

} // namespace app