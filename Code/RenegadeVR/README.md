# RenegadeVR — Stage 0 (stereo proof of concept)

Optional OpenXR output for Renegade. Without `-vr` the game is unchanged.
Design and roadmap: [`VR_MOD_PLAN.md`](../../VR_MOD_PLAN.md).

## What Stage 0 does

- Starts an OpenXR session (Direct3D 11 binding) from a separate 32-bit DLL.
- Each frame, renders the combat scene once per eye with the headset's pose and
  per-eye field of view, layered on top of the normal game camera (yaw only by default).
- Shows the normal flatscreen game on the monitor, as before.
- Recenters automatically when the session starts.

**Not in Stage 0:** controllers, HUD or menus in the headset (the headset is blank
outside a level), weapon model alignment, controller aiming. Keyboard/mouse/gamepad
still play the game.

## Stage 1 — controllers, the gun in your hand, the EVA wrist display, manual reload (2026-10-19)

Built and checked with the headset simulator (below). **Not yet tried in a real headset.**

| Feature | What it does |
|---|---|
| Controllers | OpenXR actions for Touch, Index and the generic controller; API version 2 adds hand poses, sticks, buttons and vibration. |
| Hand aiming | The right controller's pointing ray replaces the screen centre for targeting; the soldier turns to it. Red laser sight. |
| Gun in hand | The first-person arms + weapon model follows the right controller. Its left arm follows the left controller (the forearm bone is driven; hand and fingers keep their animation). |
| EVA wrist display | The game's HUD is drawn into a 2048² texture each frame. Its bottom corners (radar, health, armour / weapon, ammo) show as a screen on Havoc's EVA-01 forearm unit. |
| Manual reload | The player's weapons don't reload themselves (`WeaponClass::ManualReloadForPlayer`). Squeeze the left grip with the hand low at the belt to take a magazine, bring it to the gun. Vibration on taking it and on the reload. |
| Movement | Left stick walks where the head looks (converted into the soldier's own forward/left, since he faces the gun). Right stick: snap turn 45° or smooth turn, next/previous weapon. |
| Feel | Vibration on every shot, a tick on an empty trigger pull. |

Controls: right trigger fire, left trigger secondary fire, A jump, B crouch, X or right
stick click use, Y recenter, left menu button pause menu (on the monitor). All settings
are in `renegade_vr.ini` (commented).

Simulator results (`VRSIM=tour`, M01): hand aim turned the soldier 20.5° for a 20° hand
turn; 6 rounds fired; with the clip empty and the trigger held 2.5 s the gun stayed empty;
belt-then-gun reload filled the clip (12); snap turn 45.0°; walking matched the head
direction (1.00). Flatscreen without `-vr` is unchanged.

### Headset simulator

No headset needed: `Renegade.exe -vr MISSION=M01 VRSIM=<scenario>` drives the same code
with a scripted head and controllers (`Commando/vrsim.cpp`), logs to `vr_sim.log` and saves
left-eye screenshots to `vr_shots\`. Scenarios: `idle`, `wrist`, `arm` (the four `ArmRoll`
settings), `tour` (everything, then quits). `RenegadeVR Tools\vrsim_run.ps1 -Scenario tour`
runs it and converts the screenshots to PNG.

### Game-side hooks (all marked `RENEGADE_VR`)

| File | Hook |
|---|---|
| `Combat/input.{h,cpp}` | `Input::Set_VR_Value`: controller values merged into the input functions (also while the window isn't focused) |
| `Combat/ccamera.{h,cpp}` | `CCameraClass::Set_VR_Aim_Ray`: targeting ray from the controller |
| `Combat/weaponview.{h,cpp}` | `WeaponViewClass::Set_VR_Hand_Transform`: places the first-person model |
| `Combat/weapons.{h,cpp}` | `WeaponClass::ManualReloadForPlayer` |
| `ww3d2/render2d.{h,cpp}` | `Render2DClass::Set_Render_Target_Size`: 2D drawing into an off-screen texture |
| `Commando/vrplayer.{h,cpp}` | the gameplay above |
| `Commando/vrsim.{h,cpp}` | the simulator |

Known limits: bullets still leave from the soldier's body (aimed at where the hand points);
the arm model's hands are posed by the weapon animation, not by finger tracking; menus,
the full-screen HUD parts (target info, damage arrows) and the pause menu are only on the
monitor; vehicles still use head/mouse aiming.

## Files

| File | Side | Purpose |
|---|---|---|
| `Code/RenegadeVR/renegade_vr_api.h` | both | C function table; VC6-safe |
| `Code/RenegadeVR/renegade_vr.cpp` | DLL | OpenXR + D3D11 |
| `Code/RenegadeVR/CMakeLists.txt` | DLL | 32-bit build, pulls the OpenXR loader |
| `Code/RenegadeVR/renegade_vr.ini` | runtime | sample settings |
| `Code/Commando/vrmanager.{h,cpp}` | game | loads the DLL, renders eyes |
| `Code/Commando/gamemode.cpp` | game | calls `VRManagerClass::Render_Eyes` before the monitor frame |
| `Code/Commando/init.cpp`, `shutdown.cpp` | game | `Init` / `Shutdown` |
| `Code/ww3d2/dx8wrapper.{h,cpp}` | game | device-release callback so eye surfaces survive alt-tab / resolution change |
| `Code/Commando/commando.dsp` | game | adds `vrmanager.cpp/.h` |

Every change to an existing file is marked `RENEGADE_VR`.

## Build

**1. Bridge DLL — built and checked on 2026-10-17.** Built with Visual Studio Community 2026
(MSVC 14.51, x86) and the CMake and Ninja that ship with it. Output:
`RenegadeVR.dll`, `openxr_loader.dll` (the official Khronos loader, 1.1.63) and
`rvr_smoketest.exe`, all in `<build>/bin`. The CRT is static, so no redistributables
are needed.

Use the **x86 Native Tools Command Prompt** (or `vcvarsall.bat x86`) and the **Ninja**
generator:

```bash
cmake -S Code/RenegadeVR -B Code/RenegadeVR/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRVR_OPENXR_SDK_DIR="F:/RealRTCW VR project/sof2-vr-proxy/OpenXR-SDK-release-1.1.63"
```

```bash
cmake --build Code/RenegadeVR/build
```

Why not the Visual Studio generator: this checkout's path contains `&`
("Command & Conquer"). The OpenXR SDK adds unquoted `xcopy` post-build steps to Visual
Studio projects, and those break on it. Visual Studio's ZERO_CHECK step breaks the same
way; `CMakeLists.txt` turns that one off (`CMAKE_SUPPRESS_REGENERATION`), but the SDK's
`xcopy` steps can't be turned off. With Ninja neither runs. Leave out
`RVR_OPENXR_SDK_DIR` to download the SDK instead.

The same sources also build with llvm-mingw (i686). `CMakeLists.txt` handles MinGW, so
either toolchain works.

**Smoke test (no game needed):** `rvr_smoketest.exe [seconds]` loads `RenegadeVR.dll`
exactly like the game does. It submits a reddish left eye, a bluish right eye and a
sweeping white bar, then prints PASS/FAIL and writes `rvr_smoketest.log`. Results here,
with the headset not connected:

```
[RenegadeVR] Runtime: Oculus 1.208.0
[RenegadeVR] xrGetSystem (is the headset connected?) failed: -35 XR_ERROR_FORM_FACTOR_UNAVAILABLE
```

So the 32-bit loader, the Meta 32-bit runtime and `XR_KHR_D3D11_enable` all work from
a 32-bit process. Still to do: run it with the headset connected (Link running) and
confirm PASS and the colours in the headset.

**2. Game — builds with Visual Studio 2026.** See [Code/Port/README.md](../Port/README.md):
CMake + Ninja, 0 errors, and it runs into mission M01. Tested on 2026-10-18 with
`Renegade.exe -vr` and no headset connected:

```
[RenegadeVR] Runtime: Oculus 1.208.0
[RenegadeVR] xrGetSystem (is the headset connected?) failed: -35 XR_ERROR_FORM_FACTOR_UNAVAILABLE
[RenegadeVR] Shutdown
```

The game then carried on in flatscreen, as designed. Still to check with the headset
connected: the stereo view itself.

The original VC6 route (`commando.dsw`) still needs VC++ 6.0 SP5 and the DirectX 8, Miles,
Bink, Umbra and GameSpy SDKs.

Note: the `.dsp`/`.dsw` files in this checkout have LF line endings. VC6 refuses to
open project files without CRLF ("not generated by Developer Studio"), so convert
them first if that happens. This is not caused by the VR change.

## Install and run

1. Use a **clean 1.037 Renegade install**, not the Tiberian Technologies-patched one
   in `D:\Command & Conquer Renegade(tm)\Renegade`.
2. Copy `Renegade.exe` (from `Run/`), `RenegadeVR.dll`, `openxr_loader.dll`, and
   optionally `renegade_vr.ini` into the game folder.
3. Start your OpenXR runtime (Meta Horizon Link or SteamVR), put the headset on.
4. Run windowed (recommended) with:

```bash
Renegade.exe -vr
```

5. Read `renegade_vr.log` in the game folder if nothing appears in the headset.

## Controls (Stage 0)

| Action | Input |
|---|---|
| Play | Unchanged keyboard / mouse / gamepad |
| Look around | Move your head |
| Recenter | **Ctrl+Home** (temporary until controller actions land), or the runtime's own recenter |

## Validation checklist

Things to confirm on hardware, roughly in order:

1. `Renegade.exe` without `-vr`: menus, load M01, alt-tab, change resolution. Should
   be exactly as before.
2. `-vr` with `RenegadeVR.dll` removed: game starts flatscreen.
3. `-vr` with the headset off or the runtime closed: game starts flatscreen, log
   explains why.
4. `-vr` with the headset on: `renegade_vr.log` shows `Runtime:`, `HMD adapter:`,
   the swapchain format and `Ready`. **This is the first real test of 32-bit OpenXR
   on your Meta runtime.**
5. In M01: scale looks right (doors ~2 m), no double vision, turning your head turns
   the view the right way, leaning gives parallax.
6. Colours match the monitor (if the headset is washed out, the sRGB format choice
   is wrong — the log names the format).
7. Ctrl+Home while looking sideways makes that the new forward.
8. Alt-tab and resolution change while in VR: no crash, eyes come back.
9. Note the headset frame rate and how it changes with `RenderScale`.

## Known limitations

- CPU readback of each eye stalls the GPU. Expect a low frame rate at high
  `RenderScale`. Stage 1.5 replaces this with shared textures.
- Mouse/stick turning is applied to the view instantly (no comfort options yet).
- The first-person weapon, HUD, dazzles (lens flares) and screen fades are not
  drawn in the headset yet.
- VIS and LOD are computed from the game camera, not each eye; check for
  pop-in at the edges of your view.
- When the game window loses focus the game stops drawing the scene (existing
  Renegade behaviour), and the headset goes blank too.
- If the OpenXR session ends (headset removed for a long time, runtime closed),
  VR stays off until the game is restarted.

## Double-click launcher (2026-10-20)

Use `Play Brutal Renegade VR.exe` from the project folder or the test install.
It supplies the VR arguments and starts M01 directly, skipping startup movies.
It reports VR initialization failure instead of falling back to flatscreen.
This starts a fresh M01 session; it does not choose a saved game or later mission.
The existing limitations above (especially menus in the headset) still apply.
Use `Play Brutal Renegade.exe` for normal flatscreen menus and save selection.

## Launcher display options (2026-10-20)

Both launcher EXEs now open a native mode/settings window. Select VR or Flatscreen,
then Launch game. Flatscreen offers supported monitor resolutions and fullscreen/
windowed mode, saved through the game's existing per-user Render settings. VR
keeps its M01 startup and leaves the flatscreen settings alone.
