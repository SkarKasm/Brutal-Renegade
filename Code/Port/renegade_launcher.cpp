// Native VR/flatscreen launcher. Settings match DX8Wrapper's per-user Render key.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <algorithm>

#ifndef RVR_LAUNCH_VR
#define RVR_LAUNCH_VR 0
#endif
static const wchar_t * RenderKey = L"Software\\Westwood\\Renegade\\Render";
enum { VR_MODE = 101, FLAT_MODE, RESOLUTION, DISPLAY_MODE, LAUNCH };
struct Resolution { DWORD width, height; };
static std::wstring Directory;
static std::vector<Resolution> Resolutions;
static HWND VRButton, FlatButton, ResolutionLabel, ResolutionCombo, DisplayLabel, DisplayCombo, Hint;
static HFONT Font;
static int DPI = 96;
static int Scale(int value) { return MulDiv(value, DPI, 96); }
static bool Has_Game(const std::wstring & dir) {
	DWORD attrs = GetFileAttributesW((dir + L"\\Renegade.exe").c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}
static DWORD Read(const wchar_t * name, DWORD fallback) {
	HKEY key;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, RenderKey, 0, KEY_QUERY_VALUE, &key)) return fallback;
	DWORD value, size = sizeof(value), type;
	if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &size) || type != REG_DWORD) value = fallback;
	RegCloseKey(key); return value;
}
static bool Running() {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) return false;
	PROCESSENTRY32W entry = { sizeof(entry) }; bool found = false;
	if (Process32FirstW(snapshot, &entry)) do {
		if (_wcsicmp(entry.szExeFile, L"Renegade.exe")) continue;
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
		if (!process) continue;
		wchar_t path[32768]; DWORD size = 32768;
		if (QueryFullProcessImageNameW(process, 0, path, &size) && !_wcsicmp(path, (Directory + L"\\Renegade.exe").c_str())) found = true;
		CloseHandle(process);
	} while (!found && Process32NextW(snapshot, &entry));
	CloseHandle(snapshot); return found;
}
static void Update_Mode() {
	bool flat = SendMessageW(FlatButton, BM_GETCHECK, 0, 0) == BST_CHECKED;
	for (HWND control : { ResolutionLabel, ResolutionCombo, DisplayLabel, DisplayCombo }) EnableWindow(control, flat);
	SetWindowTextW(Hint, flat ? L"Choose your display settings, then launch the normal game menu."
		: L"Connect your headset. VR opens the main menu.\nSet headset resolution in your VR runtime.");
}
static void Populate() {
	DWORD width = Read(L"RenderDeviceWidth", GetSystemMetrics(SM_CXSCREEN));
	DWORD height = Read(L"RenderDeviceHeight", GetSystemMetrics(SM_CYSCREEN));
	DEVMODEW mode = {}; mode.dmSize = sizeof(mode);
	for (DWORD index = 0; EnumDisplaySettingsW(NULL, index, &mode); ++index) {
		if (mode.dmBitsPerPel != 32 || mode.dmPelsWidth < 640 || mode.dmPelsHeight < 480) continue;
		Resolution r = { mode.dmPelsWidth, mode.dmPelsHeight };
		if (std::none_of(Resolutions.begin(), Resolutions.end(), [=](const Resolution & other) { return r.width == other.width && r.height == other.height; })) Resolutions.push_back(r);
	}
	std::sort(Resolutions.begin(), Resolutions.end(), [](const Resolution & a, const Resolution & b) { return a.width != b.width ? a.width < b.width : a.height < b.height; });
	SendMessageW(ResolutionCombo, CB_ADDSTRING, 0, (LPARAM)L"Keep current resolution"); int selection = 0;
	for (size_t index = 0; index < Resolutions.size(); ++index) {
		wchar_t text[64]; swprintf_s(text, L"%lu x %lu", Resolutions[index].width, Resolutions[index].height);
		SendMessageW(ResolutionCombo, CB_ADDSTRING, 0, (LPARAM)text);
		if (Resolutions[index].width == width && Resolutions[index].height == height) selection = (int)index + 1;
	}
	SendMessageW(ResolutionCombo, CB_SETCURSEL, selection, 0);
	SendMessageW(DisplayCombo, CB_ADDSTRING, 0, (LPARAM)L"Fullscreen");
	SendMessageW(DisplayCombo, CB_ADDSTRING, 0, (LPARAM)L"Windowed");
	SendMessageW(DisplayCombo, CB_SETCURSEL, Read(L"RenderDeviceWindowed", 0) ? 1 : 0, 0);
}
static void Launch(HWND window) {
	if (Running()) { MessageBoxW(window, L"Close Renegade before launching it with new settings.", L"Renegade is already running", MB_OK | MB_ICONINFORMATION); return; }
	bool vr = SendMessageW(VRButton, BM_GETCHECK, 0, 0) == BST_CHECKED;
	// VR never writes the flatscreen display settings.
	if (!vr) {
		HKEY key;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, RenderKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL)) { MessageBoxW(window, L"Could not save display settings.", L"Brutal Renegade", MB_OK | MB_ICONERROR); return; }
		bool saved = true;
		auto write = [&](const wchar_t *name, DWORD value) { saved &= RegSetValueExW(key, name, 0, REG_DWORD, (BYTE *)&value, sizeof(value)) == ERROR_SUCCESS; };
		int selection = (int)SendMessageW(ResolutionCombo, CB_GETCURSEL, 0, 0);
		if (selection > 0 && (size_t)selection <= Resolutions.size()) {
			write(L"RenderDeviceWidth", Resolutions[selection - 1].width);
			write(L"RenderDeviceHeight", Resolutions[selection - 1].height);
			write(L"RenderDeviceDepth", 32);
		}
		write(L"RenderDeviceWindowed", SendMessageW(DisplayCombo, CB_GETCURSEL, 0, 0) == 1 ? 1 : 0);
		RegCloseKey(key);
		if (!saved) { MessageBoxW(window, L"Could not save display settings.", L"Brutal Renegade", MB_OK | MB_ICONERROR); return; }
	}
	std::wstring command = L"\"" + Directory + L"\\Renegade.exe\" SKIPINTRO=1";
	if (vr) command += L" -vr VRREQUIRED=1";
	STARTUPINFOW startup = { sizeof(startup) }; PROCESS_INFORMATION process = {};
	if (!CreateProcessW((Directory + L"\\Renegade.exe").c_str(), &command[0], NULL, NULL, FALSE, 0, NULL, Directory.c_str(), &startup, &process)) {
		wchar_t reason[512] = {};
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), 0, reason, 512, NULL);
		MessageBoxW(window, reason, L"Could not start Brutal Renegade", MB_OK | MB_ICONERROR); return;
	}
	CloseHandle(process.hThread); CloseHandle(process.hProcess); DestroyWindow(window);
}
static HWND Control(HWND parent, const wchar_t *kind, const wchar_t *text, DWORD style, int x, int y, int width, int height, int id = 0) {
	HWND control = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style, Scale(x), Scale(y), Scale(width), Scale(height), parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
	SendMessageW(control, WM_SETFONT, (WPARAM)Font, TRUE); return control;
}
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_CREATE:
		Control(window, L"STATIC", L"Play mode", 0, 24, 18, 400, 22);
		VRButton = Control(window, L"BUTTON", L"VR", BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP, 24, 46, 150, 26, VR_MODE);
		FlatButton = Control(window, L"BUTTON", L"Flatscreen", BS_AUTORADIOBUTTON | WS_TABSTOP, 190, 46, 200, 26, FLAT_MODE);
		SendMessageW(RVR_LAUNCH_VR ? VRButton : FlatButton, BM_SETCHECK, BST_CHECKED, 0);
		ResolutionLabel = Control(window, L"STATIC", L"Flatscreen resolution", 0, 24, 88, 180, 23);
		ResolutionCombo = Control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 210, 84, 225, 280, RESOLUTION);
		DisplayLabel = Control(window, L"STATIC", L"Display mode", 0, 24, 128, 180, 23);
		DisplayCombo = Control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 210, 124, 225, 120, DISPLAY_MODE);
		Hint = Control(window, L"STATIC", L"", 0, 24, 174, 415, 52);
		Control(window, L"BUTTON", L"Launch game", BS_DEFPUSHBUTTON | WS_TABSTOP, 285, 242, 150, 36, LAUNCH);
		Populate(); Update_Mode(); return 0;
	case WM_COMMAND:
		if (LOWORD(wParam) == VR_MODE || LOWORD(wParam) == FLAT_MODE) Update_Mode();
		else if (LOWORD(wParam) == LAUNCH) Launch(window);
		return 0;
	case WM_DESTROY: PostQuitMessage(0); return 0;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
	SetProcessDPIAware(); HDC screen = GetDC(NULL); DPI = GetDeviceCaps(screen, LOGPIXELSX); ReleaseDC(NULL, screen);
	Font = CreateFontW(-Scale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	wchar_t path[32768]; DWORD length = GetModuleFileNameW(NULL, path, 32768);
	if (!length || length >= 32768) return 1;
	Directory.assign(path, length); Directory.resize(Directory.find_last_of(L"\\/"));
	if (!Has_Game(Directory)) Directory += L"\\RenegadeVR Test";
	if (!Has_Game(Directory)) { MessageBoxW(NULL, L"Renegade.exe was not found. Keep this launcher beside the game or above the RenegadeVR Test folder.", L"Brutal Renegade", MB_OK | MB_ICONERROR); return 1; }
	WNDCLASSW type = {}; type.lpfnWndProc = WindowProc; type.hInstance = instance; type.lpszClassName = L"BrutalRenegadeLauncher";
	type.hCursor = LoadCursor(NULL, IDC_ARROW); type.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	if (!RegisterClassW(&type)) return 1;
	RECT rect = { 0, 0, Scale(460), Scale(300) }; DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	AdjustWindowRectEx(&rect, style, FALSE, 0); RECT work; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
	int width = rect.right - rect.left, height = rect.bottom - rect.top;
	HWND window = CreateWindowExW(0, type.lpszClassName, L"Brutal Renegade Alpha V0.052", style, work.left + (work.right - work.left - width) / 2, work.top + (work.bottom - work.top - height) / 2, width, height, NULL, NULL, instance, NULL);
	if (!window) return 1;
	ShowWindow(window, SW_SHOW); UpdateWindow(window); MSG message;
	while (GetMessageW(&message, NULL, 0, 0) > 0) if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
	DeleteObject(Font); return 0;
}
