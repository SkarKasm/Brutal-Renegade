/*
** mss_autocleanup.cpp - MSS_auto_cleanup lived in the static part of Miles' mss32.lib, not
** in Mss32.dll. mss.h's AIL_startup() macro calls it first; it makes sure AIL_shutdown runs
** when the program exits.
*/
#include <stdlib.h>
#include <windows.h>
#include "mss.h"

static void __cdecl Shutdown_Miles(void)
{
	AIL_shutdown();
}

extern "C" int MSS_auto_cleanup(void)
{
	static bool registered = false;
	if (!registered) {
		registered = true;
		atexit(Shutdown_Miles);
	}
	return 0;
}
