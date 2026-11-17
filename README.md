# Brutal Renegade

A Windows modification for **Command & Conquer: Renegade**, with flatscreen and OpenXR VR play, voxel wounds, progressive blood soaking, and expanded tactical controls.

The current public build is **Alpha V0.052**. A working, owned copy of Renegade supplies the original game data and runtime libraries.

## Download and install

Download the playable ZIP from this repository's Releases page.

1. Make a separate copy of a working Renegade installation.
2. Extract the release ZIP and copy everything **inside Game** beside Renegade.exe. Merge the Data folder and accept the supplied replacements.
3. Run **Play Brutal Renegade.exe** or **Play Brutal Renegade VR.exe** and select Flatscreen or VR.
4. For VR, connect the headset and start the PC VR connection before launching.

The game and OpenXR bridge are 32-bit. VR requires an active OpenXR runtime with 32-bit application and Direct3D 11 support.

Steam ownership is optional. Compatibility with a clean original CD installation still needs verification. RenHD integration is future work.

## Features in this alpha

- Flatscreen and OpenXR VR modes with vanilla weapons.
- Voxel bullet wounds, wound interiors, progressive blood soaking, and blast and burning effects.
- Wrist EVA display with health, armour, ammunition, map, and objective compass.
- VR manual reloads, weapon wheel, quicksave and quickload.
- Character geometry and texture upgrades.
- Experimental offline Command & Conquer mode and tactical view.

## VR controls

- Left stick click opens the pause panel. Right trigger operates menus.
- X or right stick click interacts with vehicles and ladders. Left stick climbs.
- A ejects the magazine. Use off-hand grip to take a replacement and insert it.
- Hold right grip to open the weapon wheel, select with the right stick, and release to equip.
- On the first wheel page, tap left trigger for Quick Actions. Select up to save or down to load, then release right grip.
- Right trigger places remote C4. Left trigger detonates it.

## Experimental tactical mode

Use the former LAN entry for offline Command & Conquer mode. F6 toggles tactical view. WASD pans, the mouse wheel zooms, dragging selects units, and right-click issues orders.

## Known issues and support

Alpha V0.052 passed an isolated VR simulator check. CD edition headset compatibility and online multiplayer are unverified. The reported 36/72 FPS headset timing split remains under investigation. Models first loaded during gameplay may still cause a hitch.

If VR closes during startup, attach **renegade_vr.log** from beside Renegade.exe to an issue, together with the headset, connection software, active OpenXR runtime, game edition, and any error message. If the log is absent, report that and verify that RenegadeVR.dll and openxr_loader.dll are beside Renegade.exe.

For frame timing, enable FrameTiming=1 under [VR] in renegade_vr.ini. This writes vr_frame_times.csv beside the executable.

The separate open-world authoring project is paused. Its campaign is outside this alpha.

## Building

Use an MSVC x86 Native Tools prompt, the Windows SDK, CMake, Ninja, and llvm-dlltool. Review [Code/Port/README.md](Code/Port/README.md) for the modern engine port.

cmake -S Code -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRENEGADE_GAME_DIR="PATH_TO_OWNED_GAME"
cmake --build build

Build the OpenXR bridge separately from Code/RenegadeVR. RVR_OPENXR_SDK_DIR selects a local Khronos OpenXR SDK checkout. See [Code/RenegadeVR/CMakeLists.txt](Code/RenegadeVR/CMakeLists.txt) for the x86 configuration.

The matching release source archive and executable build ledger accompany Alpha V0.052. The version counts distinct retained deployed executable builds.

## Credits and licence

Based on Electronic Arts' released Command & Conquer: Renegade source. Preserve the GPL v3 licence and additional terms in [LICENSE.md](LICENSE.md). Original source information remains in [README-UPSTREAM.md](README-UPSTREAM.md).

Public release blood sprites and decals are procedural. Original game archives and private SoF2 assets are supplied by their respective owners' installations.
