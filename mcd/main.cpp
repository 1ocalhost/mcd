#include "app.h"
#include <errno.h>

#pragma comment(lib, "winhttp")
#pragma comment(lib, "shlwapi")


int WINAPI WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nCmdShow)
{
	app::App().run();

	return 0;
}
