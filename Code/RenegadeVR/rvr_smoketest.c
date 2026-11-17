/*
**	Command & Conquer Renegade(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
** rvr_smoketest.c
**
** Loads RenegadeVR.dll exactly the way Renegade.exe does (LoadLibrary + RVR_Get_API), runs
** an OpenXR session for a few seconds and submits test images: the left eye reddish, the
** right eye bluish, with a white bar that sweeps across. Tests the 32-bit OpenXR path
** without building the game.
**
**    rvr_smoketest.exe [seconds]        (default 10; writes rvr_smoketest.log)
*/

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "renegade_vr_api.h"

int main(int argc, char ** argv)
{
	int seconds = (argc > 1) ? atoi(argv[1]) : 10;
	HMODULE dll = LoadLibraryA("RenegadeVR.dll");
	if (dll == NULL) {
		printf("FAIL: RenegadeVR.dll did not load (error %lu)\n", GetLastError());
		return 1;
	}
	RVR_Get_API_Func get_api = (RVR_Get_API_Func)GetProcAddress(dll, RVR_GET_API_NAME);
	const RVR_API * api = (get_api != NULL) ? get_api(RVR_API_VERSION) : NULL;
	if (api == NULL) {
		printf("FAIL: no RVR_Get_API version %d\n", RVR_API_VERSION);
		return 1;
	}
	if (!api->Init(0.5f, "rvr_smoketest.log")) {
		printf("FAIL: Init (no runtime or headset); see rvr_smoketest.log\n");
		api->Shutdown();
		return 2;
	}

	int w = 0, h = 0;
	api->Get_Eye_Size(&w, &h);
	printf("runtime: %s, eye %dx%d\n", api->Get_Runtime_Name(), w, h);

	unsigned int * pixels = (unsigned int *)malloc((size_t)w * h * 4);
	DWORD start = GetTickCount();
	int frames = 0, rendered = 0, tracked = 0;

	while (GetTickCount() - start < (DWORD)seconds * 1000) {
		RVR_Frame frame;
		if (!api->Begin_Frame(&frame)) {
			Sleep(10);					// session not running yet (headset asleep?)
			continue;
		}
		frames++;
		if (frame.PoseValid) {
			tracked++;
		}
		if (frame.ShouldRender) {
			int bar = (frames * 8) % w;
			for (int eye = 0; eye < RVR_EYE_COUNT; eye++) {
				unsigned int base = (eye == 0) ? 0xFF802020u : 0xFF202080u;		// X8R8G8B8
				for (int y = 0; y < h; y++) {
					unsigned int * row = pixels + (size_t)y * w;
					for (int x = 0; x < w; x++) {
						row[x] = (x >= bar && x < bar + 16) ? 0xFFFFFFFFu : base + (unsigned int)((y * 64 / h) << 8);
					}
				}
				api->Submit_Eye(eye, pixels, w * 4, w, h);
			}
			rendered++;
		}
		api->End_Frame();
	}

	printf("frames %d, rendered %d, with valid head pose %d, focused %d\n",
		frames, rendered, tracked, api->Is_Session_Focused());
	free(pixels);
	api->Shutdown();
	FreeLibrary(dll);

	if (frames == 0) {
		printf("FAIL: the session never started (put the headset on, or check the runtime)\n");
		return 3;
	}
	printf("PASS\n");
	return 0;
}
