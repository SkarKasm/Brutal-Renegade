# Renegade VR — Implementation Plan

Hybrid VR / flatscreen mod for Command & Conquer: Renegade, built on EA's GPL source
release. This document records what was found in both codebases, the chosen
architecture, the roadmap, and the physics-engine evaluation. Everything stated as
"verified" was checked in the files on disk on 2026-10-17; anything else is marked.

---

## 1. What the two codebases actually are

### Renegade (this tree, `Code/`)

| Area | Where | Notes |
|---|---|---|
| Build | `commando.dsw` (VC++ 6.0 SP5) | No modern solution. README: modern MSVC needs "extensive changes". **32-bit x86 only.** |
| Missing SDKs | `Code/DirectX`, `Miles6`, `Umbra`, `GameSpy`, `NvDXTLib`, `Lightscape` | Not in the tree (gitignored). Must be supplied before anything builds. |
| Renderer | `ww3d2/` — `DX8Wrapper`, `WW3D`, `CameraClass` | **Direct3D 8**, `d3d8.dll` loaded at runtime (`dx8wrapper.cpp:252`). Render-to-texture exists (`Set_Render_Target`, used by `texproject.cpp`). |
| Frame | `Commando/mainloop.cpp` → `GameModeManager::Render()` (`gamemode.cpp:211`) → `CombatManager::Render()` (`Combat/combat.cpp`) | `WW3D::Begin_Render` may not nest; extra passes must run *before* it, like the shadow projectors do. |
| Camera | `Combat/ccamera.cpp` (`CCameraClass : CameraClass`) | First/third person, sniper zoom, vehicle cameras, host-model (cinematic) mode. Camera space is -Z forward, +Y up — same as OpenXR view space. `Set_View_Plane(min,max)` takes tangents, so asymmetric eye FOVs map directly. |
| Input | `Combat/input.cpp`, `directinput.cpp`, `Commando/inputconfig*.cpp` | Abstract `INPUT_FUNCTION_*` table (move, turn, fire, reload, weapon cycle, vehicle, zoom…). This is the injection seam for VR controllers. |
| Weapons | `Combat/weapons.cpp`, `weaponview.cpp`, `weaponbag.cpp`, `muzzlerecoil.cpp` | First-person weapon is a separate render model (`WeaponViewClass`). |
| Characters | `Combat/soldier.cpp`, `humanstate.cpp`; `wwphys/humanphys.cpp` | Death = animation state (`HumanStateClass::DEATH`). HTree bones can be driven externally: `Capture_Bone` / `Control_Bone` (`ww3d2/htree.h:119`). |
| Vehicles | `Combat/vehicle.cpp`; `wwphys/vehiclephys.cpp`, `wheelvehicle`, `trackedvehicle`, `motorcycle`, `vtolvehicle` | All derive from `RigidBodyClass`. |
| Physics | `wwphys/` | Units are metres/kg/s (`phunits.h`). Scene steps in ≤1/15 s slices (`pscene.cpp:135`). Rigid bodies are a single OBB with penalty-spring contacts (`rbody.h`). Characters are `Phys3Class`. Static world collision is mesh AABTrees. Scene queries: `Cast_Ray/AABox/OBBox`, `Intersection_Test`, `Collect_Objects` (`pscene.h:652`). No ragdolls, no joints. |
| Modes | Single-player campaign, skirmish, multiplayer C&C mode | MP is server-authoritative; anything physical added must stay client-side cosmetic or be SP-only. |
| Gore | — | No blood/gib code found in `Combat/*.cpp` (0 matches). Gore is new work. |

**Game install found next to the source:** `D:\…\Renegade\` contains Tiberian Technologies
(TT) patch files (`tt.vfs`, `ttle.dll`, `scripts.dll`, `Shared.dll`). An exe built from
this source is a 1.037-era client and will not match a TT-patched install. **Use a
separate, clean 1.037 copy of the game for testing** (see §5).

### Brutally Real RTCW (`F:\RealRTCW VR project`)

There are three different VR code bodies in that folder. Only one of them is a working
reference:

1. **`xr-migration/repo/` — RealRTCWXR (Team Beef fork, merged with RealRTCW 5.4.4).**
   This is the real implementation: `code/RealRTCWXR/RealRTCWXR/*.c` (~2,800 lines) plus
   `TBXR_Common.c` and hooks in `cgame`/`game`. OpenGL + `XR_KHR_opengl_enable`, 64-bit.
   **This is what the comparison below is based on.**
2. **`code/openxr/` at the project root** (~970 lines). A skeleton, not a reference:
   eye FBOs are never created (`fboId` stays 0), `R_OpenXR_RenderStereoScene` renders
   from a zeroed `viewParms_t`, `OpenXR_RecenterView` is declared but not defined, and
   OpenXR Y-up poses are added to id Tech's Z-up origin without axis conversion.
3. **`sof2-vr-proxy/`** — a 32-bit `opengl32.dll` proxy for SoF2. Useful only as
   evidence about 32-bit OpenXR (see risks).

---

## 2. Feature comparison

Legend for RTCW column: **Yes** = implemented and used in code; **Cvar only** =
registered (and in menus) but no code reads it; **Not found** = no implementation found.
Renegade column: **Reuse concept** / **Renegade-specific** / **N/A**.

| Feature (your list) | RealRTCWXR status (verified) | Renegade plan |
|---|---|---|
| Optional VR mode | Separate exe (`RealRTCWXR.exe`) | **Renegade-specific**: same exe, VR only with `-vr` and only if `RenegadeVR.dll` loads. Done in Stage 0. |
| OpenXR | Yes, OpenGL binding | **Renegade-specific**: D3D8 can't bind to OpenXR. Bridge DLL with D3D11 binding (§3). |
| Stereo rendering at recommended size | Yes | **Renegade-specific**: two extra W3D passes with per-eye `CameraClass`. Stage 0. |
| Head tracking (orientation + position) | Yes (`hmdorientation`, `hmdposition`, `vr_positional_factor`) | **Reuse concept**. Stage 0 = view only; Stage 2 = drive character facing/aim. Positional tracking limited by `Phys3Class` collision. |
| Recenter / height calibration | Yes (`TBXR_Common.c` recenter, `vr->heightCalibration`) | **Reuse concept**. Recenter in Stage 0; height calibration Stage 1. |
| Desktop mirror (select eye) | Yes (`vr_mirror_eye`) | **Renegade-specific**: Stage 0 mirror = the normal flatscreen view (spectator-style). Eye-mirror option Stage 1. |
| VR settings (handedness, sticks, turn, height, comfort) | Yes for `vr_control_scheme`, `vr_switch_sticks`, `vr_turn_mode/angle`, `vr_walkdirection`; menus in `z_zzzRealRTCWXR/ui` | **Reuse concept**; Renegade uses `wwui` dialogs + registry (`cRegistryBool/Int`), not cvars. Stage 1 = INI file, Stage 4 = in-game menu. |
| Controller mapping | Yes (`VrInputDefault.c`, OpenXR action sets in `OpenXrInput.c`) | **Renegade-specific**: inject into `INPUT_FUNCTION_*`. Stage 1. |
| Head / controller aiming | Yes (`weaponangles`, `vr_weapon_pitchadjust`) | **Renegade-specific**: Renegade aims via a screen-space targeting star (`Update_Star_Targeting`). Controller aim needs a new aim source in `CCameraClass`/soldier targeting. Stage 2. |
| Per-weapon alignment | Yes (`vr_weapon_adjustment_%i` cvars, `weapons_vr.cfg`, `vr_align_weapons` dev mode) | **Reuse concept**: per-weapon offset/rotation/scale table applied to `WeaponViewClass`. Stage 2. |
| Two-handed weapons | Yes (`vr_two_handed_weapons`, `vr_virtual_stock`) | **Reuse concept**, Stage 3. |
| Radial weapon menu | Yes (`vr->item_selector`, `cg_weapons.c`) | **Renegade-specific**: drive `WeaponBagClass` selection; render with `Render2DClass` on a quad layer. Stage 3. |
| Reload / alt fire | Yes | **Reuse concept**: map to `RELOAD_WEAPON`, `FIRE_WEAPON_SECONDARY`. Stage 1. |
| Melee / grenades | Velocity-triggered knife: Yes (`vr_knife_velocity_trigger`). `vr_force_velocity_trigger`: **Cvar only** (looks like a JKXR leftover) | Renegade has no melee weapon in SP base game and C4/grenades are fired weapons. Physical throws only if they fit — **evaluate in Stage 3**, not promised. |
| Snap / smooth turn | Yes | **Reuse concept**, Stage 1. |
| Comfort vignette | **Cvar only** (`vr_comfort_vignette`: 0 reads) | **Renegade-specific** new work, Stage 3. RTCW is *not* a reference here. |
| Real-life crouch | **Cvar only** (`vr_irl_crouch_enabled`: 0 reads) | New work, Stage 2. |
| Campaign support | Yes | **Renegade-specific**: campaign scripts/cinematics are camera-driven; Stage 4 playthrough validation. |
| World interactions | Gesture "use" (`vr_gesture_triggered_use`) | **Renegade-specific**: map to `INPUT_FUNCTION_ACTION`; terminals/PTs use dialogs → need VR UI (Stage 4). |
| Vehicles | Generic `in_vehicle`/`remote_angles` state; `vr_vehicle_use_hmd_direction`, `vr_vehicle_use_3rd_person`, `vr_vehicle_use_controller_for_speed`: **Cvar only** | **Renegade-specific** and large: Renegade has wheeled, tracked, hover/VTOL, gunner seats. Stage 3. RTCW gives no vehicle reference. |
| Vehicle camera look-around | Not found as a working feature | New work, Stage 3. |
| Campaign balance preserved | n/a | Rule for all stages: no AI, damage, or ammo changes. |
| VR HUD / menus | Yes (`using_screen_layer`, `vr_screen_dist`) | **Renegade-specific**: render HUD and `wwui` dialogs to an off-screen target, submit as an OpenXR quad layer. Stage 1 (HUD), Stage 4 (menus). |
| Cinematics (theatre / immersive / stereo) | Yes (`vr_immersive_cinematics`, `vr_cinematic_stereo`) | Renegade has Bink movies (2D → theatre quad, Stage 4) and in-engine cinematics via `Is_Using_Host_Model` (stereo works naturally, needs comfort review). |
| Complete flatscreen mode | Yes | Default path is untouched unless `-vr`. Verified by design in Stage 0. |
| Haptics | Yes (`vr_haptic_intensity`) | Reuse concept, Stage 2. Not in your list but present in RTCW. |
| Gore, ragdolls | Brutally RealRTCW's `cg_vrextras.c`/`g_vrextras.c`: Ghoul2-style cuts, SoF2 stump caps, wounds, guts, ragdolls | Gore slice done (not built): bone skin hiding + frozen-pose limb gibs + SoF2 assets via a converter — see [Code/Tools/GoreAssets/README.md](Code/Tools/GoreAssets/README.md). Ragdolls — §6. |

---

## 3. VR runtime and integration approach

**Constraint 1 — Direct3D 8.** OpenXR has bindings for D3D11, D3D12, Vulkan and OpenGL,
not D3D8. **Constraint 2 — VC6.** The game compiles with VC++ 6.0, which cannot compile
`openxr_platform.h` + `d3d11.h` (no `stdint.h`, no modern SDK).
**Constraint 3 — 32-bit.** Renegade is x86-only, so the OpenXR loader and runtime must
be 32-bit.

**Decision: a bridge DLL with a plain C ABI.**

```
Renegade.exe (VC6, D3D8)                     RenegadeVR.dll (VS2022, x86)
 ├─ VRManagerClass  (Commando/vrmanager.cpp)   ├─ OpenXR instance/session (XR_KHR_D3D11_enable)
 │   renders each eye with W3D into a           ├─ D3D11 device on the HMD's adapter (LUID)
 │   lockable D3D8 render target                ├─ per-eye swapchains (sRGB BGRA)
 │   locks it, hands pixels to the bridge  ───► ├─ uploads pixels, xrEndFrame
 └─ loads the DLL with LoadLibrary; if it's     └─ poses, recenter, (later) actions/haptics
    missing or fails, the game stays flatscreen
```

- **Stage 0 transport: CPU copy.** Lock the D3D8 eye target, upload into the D3D11
  swapchain image. Simple and works with the stock renderer, but it stalls the GPU
  and copies ~2 × 14 MB per frame at Quest 3 native size. Default render scale is
  0.7 to keep this tolerable. **Not a shipping path.**
- **Stage 1.5 transport: zero-copy.** Replace `d3d8.dll` with an in-process
  D3D8→D3D9Ex translation layer (crosire's d3d8to9 is BSD-2 licensed; license to be
  re-checked when adopted), create eye targets as shared D3D9Ex textures, open them in
  D3D11 with `OpenSharedResource`. Known work item: D3D9Ex forbids `D3DPOOL_MANAGED`,
  which W3D uses heavily, so the translation layer must remap it.
- Same bridge pattern is used for the physics engine (§6): modern compiler on one side,
  VC6-safe C header on the other.

**Why not port the whole game to modern MSVC first?** It is the long-term right move
and it is on the roadmap (Stage 5), but it is not needed to prove VR works and
it would touch most files in the tree.

---

## 4. Roadmap

| Stage | Goal | Exit criteria |
|---|---|---|
| **0 — PoC (built; runs flatscreen; headset test pending)** | OpenXR session, head pose, both eyes rendered by W3D, flatscreen untouched | Headset shows the level in stereo with correct scale and head look; game without `-vr` is byte-for-byte the old behaviour |
| 1 — Playable seated VR (**mostly built 2026-10-19, simulator-tested, headset test pending**) | Controller actions → `INPUT_FUNCTION_*`, snap/smooth turn, HUD on the left wrist (EVA unit), INI settings, manual reload; also pulled in from Stage 2: hand aiming, weapon in hand, left arm follows the controller, haptics. Still to do: eye mirror option, height calibration | Can complete M01 seated with controllers |
| 1.5 — Performance | Zero-copy D3D9Ex shared textures | Stable HMD frame rate on RTX 3060 at 1.0 render scale |
| 2 — Body & aim | Head-yaw drives soldier facing, controller aim for weapon, per-weapon alignment table, haptics, IRL crouch | Weapon in hand, bullets go where the controller points |
| 3 — Combat & vehicles | Radial weapon menu, two-handed weapons, vignette, vehicle controls per type, vehicle look-around | Drive every vehicle type in the campaign |
| 4 — Campaign & UI | VR menus/dialogs/PTs, Bink theatre mode, in-engine cinematic comfort, full campaign pass | Full campaign completable in VR and flatscreen |
| 5 — Toolchain | Port to modern MSVC — **done early** (Code/Port, VS 2026), so testing didn't wait for VC6 | Game builds and runs M01 |
| Physics track | See §6 (runs in parallel after Stage 1) | |

---

## 5. Build, test and hardware validation

**Build prerequisites** (none of these were run — commands are what to run):

- VC++ 6.0 SP5 for the game, plus the missing SDKs in `Code/` (DirectX 8 SDK at
  minimum; Miles, Bink, Umbra, GameSpy per README).
- Visual Studio 2022 (MSVC v143) + CMake ≥ 3.20 for `Code/RenegadeVR`. The OpenXR
  loader is built from the Khronos OpenXR-SDK (Apache-2.0) as 32-bit.

```
cmake -S Code/RenegadeVR -B Code/RenegadeVR/build -A Win32
cmake --build Code/RenegadeVR/build --config Release
```

Then build `commando.dsw` → Batch Build → Commando Release.

**Test matrix for Stage 0:**

1. Start without `-vr` → identical to an unmodified build (menus, campaign load,
   alt-tab, resolution change).
2. Start with `-vr` but no `RenegadeVR.dll` → log says VR unavailable, game is flatscreen.
3. Start with `-vr`, headset off / runtime missing → same fallback.
4. Start with `-vr`, headset on → stereo view in M01; check scale (doorways ~2 m),
   stereo convergence (no double vision), head roll and positional parallax.
5. Alt-tab and change resolution while in VR → no device-reset failure.
6. Recenter hotkey while looking sideways.
7. Check `renegade_vr.log` next to the exe for runtime name, swapchain sizes, format.

**Hardware on this machine (from `F:\RealRTCW VR project\openxr_session_probe.log` and
registry):** NVIDIA RTX 3060, Meta Horizon OpenXR runtime with both
`oculus_openxr_64.json` and `oculus_openxr_32.json` registered.

**Risks**

| Risk | Impact | Mitigation |
|---|---|---|
| 32-bit OpenXR support. The SoF2 32-bit probe log stops at the `xrCreateSession` call with no result line (OpenGL binding). | Could block VR entirely | D3D11 binding is the best-supported one on Meta's runtime. Test the bridge first; SteamVR's runtime is the fallback. **Not confirmed on hardware.** |
| CPU readback cost | Low frame rate in Stage 0 | Render scale; Stage 1.5 zero-copy. |
| D3D8 → D3D9Ex managed-pool remap | Stage 1.5 effort | Prototype on M01 first. |
| VC6 toolchain availability | Can't build at all | Stage 5 port; until then VC6 in a VM. |
| TT-patched install | Exe/data mismatch | Clean 1.037 install for testing. |
| Camera-driven scripts/cinematics | Nausea, broken shots | Stage 4 comfort options. |
| VIS/culling from the main camera only | Pop-in at eye edges | Test item; widen cull if needed. |
| Multiplayer | Cheating/desync if VR changes aim | VR aim/physics SP-only until reviewed. |

---

## 6. Physics engine evaluation (no physics code added yet)

### What Renegade's own physics can and can't do

- Good at: character movement (`Phys3Class`/`HumanPhysClass`), vehicles (wheeled,
  tracked, VTOL, motorcycle), projectiles, networked rigid bodies. Tuned for the
  campaign; replacing it would change gameplay and break MP.
- Not available: joints, ragdolls, convex/mesh-shaped dynamic bodies (rigid bodies are
  one OBB each), stable stacking. These are exactly what gore, ragdolls and VR
  grabbing need.
- **Integration seam:** run a second, cosmetic physics world next to `wwphys`, fed
  with static collision converted from W3D meshes, stepped from `CombatManager`
  right after `COMBAT_SCENE->Update()` (`combat.cpp:697`). Results drive render
  objects and HTree bones (`Control_Bone`), never `wwphys` objects.

### Candidates (checked against each project's README / LICENSE on 2026-10-17)

| | Renegade wwphys | **Jolt Physics** | Bullet 3 | Box3D |
|---|---|---|---|---|
| License | GPLv3 + EA terms | MIT | zlib | MIT |
| GPLv3-compatible | — | Yes | Yes | Yes |
| Language / compiler | VC6 C++ | C++17; VS2022+, Clang 16+, GCC 12+ | C++03 | C17 (samples need C++20) |
| 32-bit Windows | Yes | **Yes — "Windows x86/x64/ARM64"; `cmake_vs2026_cl_32bit.bat`** | Not stated in README (historically supported; unverified) | Not stated |
| Build system | DSP/DSW | CMake | premake / CMake / VS2010 batch / vcpkg | CMake |
| Maturity | Shipped 2002 | Shipped in AAA titles; actively developed | Mature; maintainers point users to PyBullet | **v0.1.0, author calls it alpha**, PRs disabled |
| Mesh / hull / heightfield | AABTree mesh (static) | Mesh, convex hull, heightfield, compound, capsule… | `btBvhTriangleMeshShape`, convex hull, heightfield | Mesh, hull, heightfield, capsule |
| Ragdoll support | None | **Built-in ragdoll with animated keying/motors and skeleton mapping** | Constraints; ragdoll is demo code | Joints only; no ragdoll helper |
| Character controller | `HumanPhysClass` | `CharacterVirtual` + rigid character | `btKinematicCharacterController` | "Character mover" |
| Vehicles | Wheeled, tracked, VTOL, bike | Wheeled, tracked, motorcycle | `btRaycastVehicle` | Wheel joint only |
| Determinism | — | Deterministic (same build) | Not claimed | Cross-platform determinism claimed |
| VR interaction fit | Poor | Good: constraints for grab, sensors, CCD | OK | Good on paper; API may still change |

**Common to all three:** none compiles under VC6, so every candidate goes behind a
C-ABI DLL until Stage 5. Coordinates: all are Y-up by convention but none enforce it;
Renegade is Z-up metres. Use Z-up gravity `(0,0,-9.81)` and keep metres — no axis swap
needed. Update timing: fixed 1/60 s sub-steps accumulated from
`TimeManager::Get_Frame_Seconds()`, interpolated for render.

**Recommendation: Jolt Physics.** It is the only candidate that is both production-proven
and ships ragdolls with animation keying, which maps directly onto Renegade's
`Capture_Bone`/`Control_Bone`. It documents 32-bit Windows support, which Renegade needs.
Bullet is a reasonable fallback but its maintenance has moved to PyBullet. **Box3D is
not recommended now:** it is v0.1 alpha and the API may change; re-evaluate at 1.0.
**Do not replace wwphys.**

### Smallest prototype (Physics Stage P0)

Single-player only, client-side, no networking, no gameplay effect:

1. `RenegadePhys.dll` (VS2022, x86, Jolt pinned to a release tag) with a C API:
   `create_world`, `add_static_mesh(verts, tris)`, `add_box`, `step`, `get_transform`.
2. On level load, convert static meshes within ~30 m of the player from their W3D
   collision geometry to one Jolt `MeshShape`.
3. A debug console command spawns one crate model (existing W3D asset) as a Jolt box;
   each frame copy its transform to the render object.

Exit criteria: crate falls, collides with terrain and walls, settles, and does not
affect AI, bullets or saving. Then:

- **P1** — VR-grabbable prop (needs Stage 2 controller poses).
- **P2** — Death ragdoll for soldiers in SP: on `HumanStateClass::DEATH`, build a Jolt
  ragdoll from the HTree, capture bones, drive them from the ragdoll; despawn after N s.
- **P3** — Gore: blood decals via existing `PhysDecalSys`, hit-location reactions,
  dismemberment only if W3D skins can be split cleanly (unconfirmed).

Sources: [Jolt README](https://github.com/jrouwe/JoltPhysics), [Jolt Build README](https://github.com/jrouwe/JoltPhysics/blob/master/Build/README.md),
[Jolt LICENSE](https://github.com/jrouwe/JoltPhysics/blob/master/LICENSE),
[Bullet README](https://github.com/bulletphysics/bullet3),
[Box3D repo](https://github.com/erincatto/box3d), [Box3D LICENSE](https://github.com/erincatto/box3d/blob/main/LICENSE),
[Phoronix on Box3D](https://phoronix.com/news/Box3D-Open-Source-3D-Physics).
