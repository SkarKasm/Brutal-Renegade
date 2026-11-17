# Renegade voxel-gore offline proof

This is an independent x86 C++17 tool. It is not registered in the game CMake/DSP files, does not launch the game, and does not deploy files. The only input access to the game directory is read-only. All extracted inputs, compiler outputs, logs and OBJs live under this directory. Generated inputs are game assets; do not redistribute them with the GPL source.

## Reproduce from the repository root (PowerShell)

```powershell
python Code/Tools/VoxelGore/prepare.py --data 'D:/Command & Conquer Renegade(tm)/Renegade/Data'
& './Code/Tools/VoxelGore/build/build_and_run.cmd'
if ($LASTEXITCODE -ne 0) { throw 'offline proof failed' }
```

Python standard library and an x86 MSVC C++17 toolchain are sufficient. The default vcvars path is `E:/VisualStudio/VC/Auxiliary/Build/vcvarsall.bat`; pass `--vcvars '<your vcvarsall.bat>'` to prepare.py on another machine. Preparation does not execute the build. The build script compiles only the harness, original core tests and original software viewer. It also creates PNGs with the handoff's dependency-free converter and runs verify.py. No game/SDK libraries are linked.

The automated fixture is `c_nod_mg_l0.w3d`. `--model` can select another single-LOD file skin for exploration, but the shot targets and verification counts deliberately describe this fixture; other skeletons/models need reviewed fixture changes. Data lookup prefers the named loose asset, then always3/always2/always.dat by filename in the MIX1 directory. It does not emulate the full game filesystem, TT VFS, map archive precedence or aggregate definitions.

## Files and implementation

* `w3d_reader.h`: checked little-endian W3D chunk/HTree/HLOD/file-skin reader. It follows ww3d2's quaternion expression order, parent-first Base_Update(identity), single BoneIdx and rotation-only normal transform. Pre-3.0 hierarchies and older mesh attributes are rejected. Initial scope is a single file LOD, visible skinned meshes only; rigid children/materials/UV export/animations are not implemented. Triangles and vertex order are retained.
* `offline.cpp`: builds gore::Body from the real rest mesh, adds metre-sized Renegade bone rods, then drives the handoff wrapper's generic hit/refinement/draw/sever algorithms. It installs a W3D descriptor directly into the wrapper's private model storage in the same translation unit. Its MDS/MD3 loaders are compiled but never called. This is an offline reuse technique, not the intended runtime ABI.
* `prepare.py`: resolves hierarchy name from the real HLOD, extracts working copies, writes source hashes and makes a **generated** wrapper copy with exactly one semantic change: METRES=1 instead of 0.0254. Header shims retain the original include layout. Original handoff files and the core remain untouched.
* `verify.py`: checks OBJ indices/finite geometry, colored flesh vertices, removed skin area, separate piece/stump skin area, x86 PE architecture and input hashes; verifies rejection of truncated chunks, oversized chunks and invalid vertex bone IDs.

`build/` contains generated wrapper/include layout, binaries and compiler/test log. `output/input/` contains read-only-source working copies; `manifest.json` records exact input provenance and SHA-256. Both generated directories are ignored locally by this tool's .gitignore.

## Outputs from the 2026-11-01 run

* `output/rest.obj`, `rest.png`: original 4,754 vertices / 8,022 triangles, posed by the 23-bone S_A_HUMAN hierarchy. Bounds: X -0.177..0.197, Y -0.885..0.886, Z -0.009..1.843 metres. `bones.obj` and `rest_palette.txt` expose bone segments and 3x4 rest matrices.
* `voxels.obj`: full voxel surface for occupancy inspection. 1.5 cm cells, 456,576 total cells, 41,066 filled, 833 bone; intact mesh has one component at crumb=40. Hidden hit boxes and C_SHADOWMESH are excluded and logged.
* `cut_skin.obj`, `flesh.obj`, `wounds.png`: three chest shots remove 358/303/293 cells, hitting 4/44/0 bone cells; all exit. Cut skin retains 7,980 whole triangles plus 845 barycentric subs. Flesh has 749 vertices / 1,412 triangles. Its vertex colors encode the wrapper's rim/muscle/bone tints (5 bone vertices, 254 at <=2 cm depth, 490 deeper). The stock viewer uses a flat red flesh tint, so its preview does not display those vertex colors. `flesh_depth.obj` is the unposed raw wound surface for comparison; depth ownership itself remains in core memory.
* `posed_skin.obj`, `posed_flesh.obj`: a synthetic 0.65-radian elbow bend checks the cut interpolation/flesh transform path. This is not a W3D animation reader. A flesh transform check passes within 1e-5 m; this does not establish engine parity.
* `stump_skin.obj`, `stump_flesh.obj`, `piece0_skin.obj`, `piece0_flesh.obj`, `severed.png`: independent offline corpse fixture; 13 parallel forearm shots remove 207 cells and detach a component owned by C L HAND. Piece is displayed with an offset next to the body. Baseline was one connected component, so this separation is caused by the burst, not an existing disconnected island.
* `proof.log`, `verification.json`, `build/build.log`: full evidence. MSVC produced machine 0x014c (x86). The unchanged core test prints ALL PASSED (0 failures). This actual bundled source has **18 CHECK calls**, and 18 successful checks executed; the handoff's 19/19 description does not match its test file.

Representative local Release x86 timings: template build about 9.4 ms; each chest carve 0.13-0.17 ms; full skin-cut/flesh rebuild plus pose about 11 ms. They are one small offline run, not gameplay or VR performance measurements.

I inspected rest/wound/sever previews: a plausible T-pose, visible through-holes and red wound walls, and a detached hand with cut faces. The selected BODY_0 visually includes the head/helmet silhouette; the tool does not distinguish those triangles anatomically and should not be used to tune skull/brains yet.

## Still unverified

No game run or engine build, no existing engine edits, no install writes. No comparison with a live engine vertex/matrix dump: the rest pose follows the same source formulas but **bit-for-bit renderer parity is not established**, particularly legacy x87/SIMD rounding. No real animation, hierarchy morph/scale, rigid/nested facial models, helmet separation, skull/brain classification, UV/material/pass fidelity, DX8 rendering, gameplay behavior, VR eye lifetime or multiplayer event coverage is proven. These are explicit next gates in `../../../VOXEL_GORE_PORT.md`.

## November 3 adapter and engine checks
See ../../../VOXEL_GORE_STATUS.md. build_engine_parity.cmd invokes the real engine HTree and MeshModel CPU skinning in a console-only tool. build_adapter_check.cmd builds Code/VoxelGore and verifies 25 state/geometry/lifetime cases. Both use the existing read-only input copies. Logs are under build/. Neither launches or deploys the game. The original offline.cpp proof remains preserved separately.
