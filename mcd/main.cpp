#include "http.h"

#pragma comment(lib, "winhttp")
#pragma comment(lib, "shlwapi")

void foo()
{
	HttpConfig config;
	config.setHttpProxy("127.0.0.1:1080");
	config.setUserAgent("Meow");
	config.addRequestHeader("User-Agent: curl");
	config.addRequestHeader("Cookie");

	HttpGetRequest http(config);

	// test resumable
	auto url = "https://httpbin.org/get";
	HttpResult r = http.open(url);
	_should(r) << url << r;

	if (r) {
		if (r.statusCode() == 308) {

		}
	}
	// http body
	//std::string text;
	//r.saveToString(&text);


	//// download file
	//HttpResult r = http.open("https://abc.com");
	//if (r.allRight)
	//{
	//	if (r.statusCode == 206)
	//	{
	//		OutputStream os;
	//		r.saveToFile(&os);
	//		// os.setDataSize(12345);
	//		// os.write(&data, size);
	//	}
	//}

}

int WINAPI WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nCmdShow)
{
	foo();
	return 0;
}