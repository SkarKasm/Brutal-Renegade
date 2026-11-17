# Renegade on Visual Studio 2022/2026 (x86)

The original game builds with VC++ 6.0 (`commando.dsw`). This port builds the same Release
sources with a current MSVC through CMake + Ninja. Nothing about the game's data or
behaviour is meant to change.

**Status (2026-10-18):** `Renegade.exe`, `scripts.dll` and `BandTest.dll` build with Visual
Studio Community 2026 (MSVC 14.51, x86) with **0 errors**, and the game runs:
- It reaches the main menu and plays mission M01 (terrain, HUD, first-person weapon, AI).
- The VR bridge loads with `-vr`, and the game falls back to flatscreen when no headset is
  connected.
- The gore test (`GORETEST`) runs for minutes without a crash.

Not yet checked: sound (Miles), Bink movies, a full mission playthrough, save/load,
multiplayer.

## Build

From an **x86 Native Tools Command Prompt** (or after `vcvarsall.bat x86`):

```bash
cmake -S Code -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build <build>
```

Output is `<build>/bin`: `Renegade.exe`, `scripts.dll`, `BandTest.dll` and their PDBs.
On this machine, `RenegadeVR Tools\build_game.cmd` does both steps.

**Use Ninja, not the Visual Studio generator.** The checkout path contains `&`
("Command & Conquer"), and generated Visual Studio custom-build steps break on it.

**Needs:** MSVC x86 tools, a Windows 10/11 SDK, and `llvm-dlltool` from any LLVM or
llvm-mingw install, to make the Miles/Bink import libraries. Set `RENEGADE_DLLTOOL` if it
isn't found.

## How it stays VC6-compatible

**Compiler switches** (in `Code/CMakeLists.txt`):
- VC6 language behaviour: `/permissive /Zc:forScope- /Zc:twoPhase- /Zc:wchar_t-
  /Zc:strictStrings- /Zc:rvalueCast-`.
- `_CONST_RETURN=` so `strchr`/`strstr` return `char*` as in VC6.
- `_USE_32BIT_TIME_T`.
- Debugging: `/Zi /Oy-` (PDBs and frame pointers), a fixed base address
  (`/DYNAMICBASE:NO`, like the original), and `/we4731` (see below).

**`Port/sources.cmake`** is generated from the `.dsp` files by `gen_sources.py`, so the build
compiles exactly the original Release file lists.

**`Port/compat/`** holds stand-ins for SDKs EA couldn't ship:

| Missing | Replacement | Licence |
|---|---|---|
| DirectX 8 SDK `d3d8*.h` | Wine's headers, plus `WINBOOL`, `__MSABI_LONG` and gamma-calibration constants | LGPL 2.1+ (`d3d8/NOTICE.txt`) |
| D3DX8 | `d3dx8.h` + `d3dx8_compat.cpp`, written from scratch for the ~15 calls Renegade makes: texture creation with format/size fallback, surface copy with conversion between all W3D formats, box/point filtering, DXT1–5 decode and encode, mipmaps, matrix helpers | this project (GPL) |
| Miles Sound System 6 | `mss/mss.h` from TheSuperHackers/miles-sdk-stub, adjusted to Miles 6's integer handle and user-data types; `mss_autocleanup.cpp`; links to the game's own `Mss32.dll` | GPL 3+ |
| Bink | `bink/bink.h` from TheSuperHackers/bink-sdk-stub; links to the game's own `binkw32.dll` | GPL 2+ |
| Miles/Bink import libraries | `importlibs/*.def` (export names read from the game's DLLs), built at configure time with `llvm-dlltool` so the decorated names (`_AIL_startup@0`) match exactly | names only |
| GameSpy SDK | `GameSpy/*.h` + `gamespy_stub.cpp`: reporting does nothing, CD keys always pass, `get_sockaddrin` and MD5 are real implementations. GameSpy closed in 2014. | this project |
| GNU regex | `gnu_regex.h` + `regex_compat.cpp` on `std::regex` | this project |
| MFC `afxres.h`, `iostream.h` | minimal shims | — |

## Source changes

Every change is marked `PORT:` with the reason. They fall into these groups:

**Language rules VC6 didn't enforce**
- Missing `typename`: `wwlib/index.h`, `ww3d2/prim_anim.h`, `Combat/statemachine.h`.
- Implicit `int`: `WWAudio/WWAudio.h`, `Commando/consolemode.h`, `ConsoleMode.cpp`,
  `dlgcncbattleinfo.cpp`, `ww3d2/lightenvironment.cpp`. The two lighting cut-offs keep VC6's
  int value of 0, so behaviour is unchanged.
- Pointer-to-member syntax: `Combat/statemachine.h` (`ADD_STATE_TO_MACHINE` now takes the
  class from `decltype(this)`) and `mendozabossgameobj.cpp`.
- Friend templates: `WWOnline/RefPtr.h`. Iterators are no longer pointers:
  `wwui/IMEManager.cpp`, `WWOnline/WOLNetUtilObserver.cpp`.
- A loop variable shadowed inside its own body: `Combat/weaponbag.cpp`.
- `abs` overloads: `BandTest.cpp`. Missing includes: `viseme.cpp`, `GameSpy_QnR.cpp`.
  Renamed SDK field: `wwlib/Except.cpp`.

**Things VC6 compiled into always-true / never-true code**
- `hud.cpp`: `Count != 0` was always true.
- `scriptzone.cpp`: `Count == 0` was never true.
- Both now spell out the shipped behaviour explicitly.

**`DEFINE_AUTO_POOL`** (`wwlib/mempool.h`): the pool allocator is now defined once,
generically, in the header. This removes access errors for protected nested classes in
`camerashakesystem.h` and elsewhere.

**Script command table**
- `Combat/scriptcommands.h` and `Scripts/scriptcommands.h` used default arguments on
  function-pointer members, which modern C++ forbids. 23 members are now one-pointer
  wrapper structs that keep the defaults (`gen_script_commands.py`).
- Size and layout are unchanged, so the engine ↔ `scripts.dll` interface is binary compatible.
- `Scripts/DLLmain.cpp` exports `Create_Script` and the other entry points by plain name
  (`extern "C"`). The engine looks them up with `GetProcAddress`; whatever exported them in
  EA's build wasn't in the release.

**Runtime fixes found by running the game**
- `wwlib/cpudetect.cpp`: the OS-detection code left the OS name uninitialised on anything
  newer than Windows XP, which crashed the startup log. The shipped exe has the same bug.
- **Registry** (`wwlib/registry.cpp`, `slavemaster.cpp`, `WebBrowser.cpp`, `WOLBuddyMgr.cpp`,
  `BandTest.cpp`):
  - Settings now live under `HKEY_CURRENT_USER\Software\Westwood\Renegade` instead of HKLM.
  - The 2002 exe had no manifest, so Windows silently redirected its HKLM writes. A modern
    build has a manifest, which turns that off.
  - Keys are read from HKLM as a fallback.
  - A key that fails to open no longer leaves an uninitialised handle.
- `ww3d2/dx8wrapper.h`: `Convert_Color`/`Clamp_Color` were x87/CMOV inline assembly that used
  `EBP`/`EBX` as scratch registers. Modern MSVC uses those as frame pointers in
  stack-aligned functions, so the stack was corrupted wherever they were inlined
  (warning C4731, now an error).
  - They're replaced with C that gives the same results: truncation and the unmasked OR are
    unchanged.
  - That removed all 30 C4731 sites.
- `Commando/WINMAIN.CPP`: `packet.h` came from a folder not in the release; nothing used it.
- **Quitting from inside a mission** crashed twice during shutdown (the shipped order has the
  same problems):
  - `Combat/combat.cpp`: conversations are now stopped before `scripts.dll` is unloaded.
    Stopping one asks each soldier's observers for their names, and the script observers'
    code was already gone.
  - `wwphys/staticanimphys.cpp`: animated scenery freed after the physics scene no longer
    asks the (deleted) scene to drop its shadow.
- `Commando/mainloop.cpp`: `Stop_Main_Loop` writes its caller to the debugger output, so an
  unexpected exit can be traced.

- **Freeze in missions** (`ww3d2/predlod.cpp`, `ww3d2/hlod.cpp`): the level-of-detail optimizer
  spun forever when one object reported a NaN screen size or value. NaN breaks both its heap
  ordering and its stop conditions. Caught in a snapshot of the frozen game. The loop now has
  a hard iteration limit, NaN sizes count as 0, and gore pieces with a non-finite position
  are dropped.

- **Characters frozen in place, parachuters hanging in the sky** after playing a while
  (`wwphys/phys3.cpp`). Found with the stuck detector below:
  - One Nod soldier's position and velocity had become NaN, so his culling box was NaN too.
    Every collision test in the level then reported "start bad" against him. Every move was
    refused, and AI sight checks failed, while animations and aiming went on.
  - Likely source: `Apply_Move` sets its `dt` to 0 after a no-bounce collision (bullets, C4),
    then pushes another soldier with that `dt`. `Collide_Move` computed the pushed soldier's
    velocity as distance / 0. Now it keeps the old velocity when `dt` is 0.
  - `Apply_Move` rejected NaN moves only in debug builds; now it always does.
  - Safety net: before and after each step, an object with a NaN/infinite position or
    velocity goes back to its last valid position, stopped, with a line in `stuck_watch.log`.
- **Frame cap** (`Commando/mainloop.cpp`): 144 fps by default. It wasn't the cause of the
  freeze above, but a 2002 engine has no use for 650 fps. Change it with `MAXFPS=n` on the
  command line or the DWORD `MaxFPS` under `...\Renegade\Options`; 0 removes the cap.
- **Stuck detector** (`Combat/stuckwatch.cpp`): when the player holds a move key for 3 s
  without moving, it writes the physics state to `stuck_watch.log` next to the exe. The report
  covers test moves and what they hit, nearby objects, objects with broken boxes, and other
  soldiers. It only reads.

## Added features

- **Always full detail:** every multi-LOD model draws its most detailed level
  (`HLodClass::Always_Top_LOD`, on by default). It no longer goes through the 2002 polygon
  budget.

- **Resolution in the in-game options** (Options → Video): a list of every mode the video card
  offers. It's applied when the options screen closes and saved. Before, the field was
  read-only and only WWConfig could change it; WWConfig writes HKLM, which this build only
  reads as a fallback. `dlgconfigvideotab.cpp` and `chat.rc`. The Tiberian Technologies patch
  does the same for its own exe, but its engine code isn't open (the `source` folder is TT's
  scripts.dll SDK), so this is written from scratch.
- **Wide screens** (16:9, 16:10…): the game camera (`Combat/ccamera.cpp`) and the menu
  backdrops (`wwui/menubackdrop.cpp`) keep the 4:3 vertical view and show more at the sides.
  Before, the game camera stayed 4:3 and was stretched across the screen, and the menu
  backdrops were cropped top and bottom (zoomed in). 4:3 screens look exactly as before.

## Developer options (command line)

| Option | Effect |
|---|---|
| `REGMOD=NAME` | Uses `HKCU\...\Renegade\NAME` for settings: a separate profile, so tests don't touch your normal settings. This one is original Renegade. |
| `MISSION=M01 [DIFFICULTY=0..3]` | Starts that campaign mission as soon as the main menu appears (`dlgmainmenu.cpp`). |
| `FIRETEST` | Like `GORETEST`, but the player's own weapon does the killing: a Nod soldier appears 4–7 m ahead and the trigger is held until it dies. Exercises the real bullet and kill path with the normal gore.ini odds. |
| `GORETEST` | Every 3 s, spawns a Nod soldier in front of the player and kills it with a head shot, arm shot, leg shot or point-blank explosion in turn. Keeps the player alive. Writes `gore_test.log`. |
| `MAXFPS=n` | Frame-rate cap (default 144, 0 = none). See "Runtime fixes". |
| `-vr` | VR output through `RenegadeVR.dll` (see `Code/RenegadeVR/README.md`). |

## Test tools (outside the repo, in `RenegadeVR Tools\`)

| Tool | What it does |
|---|---|
| `build_game.cmd` | Configure and build. |
| `run_test.ps1` | Build, copy into `RenegadeVR Test`, run under `crashtrace`, and print a symbolized stack for any crash (via `llvm-symbolizer` and the PDB). |
| `crashtrace\crashtrace.exe` | Runs a program under the Win32 debug API and reports the first access violation or unhandled exception with registers and a stack. |
| `capture_game.ps1` | Launches the test build and saves a series of window captures (`PrintWindow`, so other windows can't cover it). |
| `RenegadeVR Test\` | A test install: hard links to the game files (no extra disk space; your real install is untouched), the Tiberian Technologies binaries left out, and these builds added. |

## 2026-10-20 fixes and launchers

- Corrected Miles 6 positional volume declarations to integer 0..127 (including
  the integer return ABI). The old float declarations failed all five native
  volume round trips; the corrected declarations pass all five against Mss32.dll
  and its Miles Fast 2D Positional Audio provider.
- Fixed dialog flush leaving an invisible active main/pause menu after canceling
  a transition. Key messages now dispatch before polling input, repeated Escape
  is ignored by menus, and short Escape presses queue one menu action.
- Added `Play Brutal Renegade.exe` and `Play Brutal Renegade VR.exe`. Both work
  beside the game or in the project folder above `RenegadeVR Test`, set the game
  working directory, and skip startup movies. The flat launcher opens the normal
  menu. The VR launcher requests `-vr VRREQUIRED=1 MISSION=M01`: it starts M01, and
  reports initialization failure instead of silently falling back to flatscreen.
  VR menus and real-headset behavior still require further work/validation.
- `miles_volume_check` is an explicit build target; it plays no audio. Run beside
  the game DLLs or with the game's folder as working directory.

## Launcher display options (2026-10-20)

Both launcher EXEs now open a native mode/settings window. Select VR or Flatscreen,
then Launch game. Flatscreen offers supported monitor resolutions and fullscreen/
windowed mode, saved through the game's existing per-user Render settings. VR
keeps its M01 startup and leaves the flatscreen settings alone.
