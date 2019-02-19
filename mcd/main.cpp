#include "http.h"

#pragma comment(lib, "winhttp")
#pragma comment(lib, "shlwapi")

void foo()
{
	HttpConfig config;
	config.setHttpProxy("127.0.0.1:1080");
	//config.setUserAgent("");
	config.addRequestHeader("User-Agent: curl");
	config.addRequestHeader("Cookie: 111");
	config.addRequestHeader("Cookie:2222");
	config.addRequestHeader("hello: aaa");
	config.addRequestHeader("hello: bbb");

	HttpGetRequest http(config);

	// test resumable
	auto url = "https://httpbin.org/get";
	HttpResult r = http.open(url);
	_should(r) << url << r;

	if (r) {
		std::string text;
		http.save(&text);

		auto server2 = r.headers().firstValue("server");
		auto server3 = r.headers().firstValue("server-x");
		bool hasKey = r.headers().has("Content-length");
		bool hasKey2 = r.headers().has("Range");

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