# Voxel gore status — 2026-11-03

World authoring is on hold at the user's request. Keep tool output brief; no image previews, browsing, or remote map preview servers. No game launch or main-install deployment.

Updated handoff: voxel_gore_handoff/RENEGADE_HANDOFF_2026-11-03.md. Its 1cm body / 7.5mm separately scoped head, living separation, double-sided draw and caching requirements supersede the older handoff. Reference snapshot: RealRTCW commit 3aca3fc0944bdd680fac1f98cb09521b7c64899d. Code/VoxelGore/PROVENANCE.json records hashes and adaptations; core files remain byte-identical.

Completed offline:
- Actual engine HTree loader, Base_Update and two captured palettes, plus MeshModelClass::get_deformed_vertices, compared with the real Nod W3D fixture: 14,262 vertex/normal comparisons, zero measured matrix/position/normal error. This is a CPU executable with no WW3D/window/device initialization. Two unreachable animation-audio link stubs are offline-target-only. Real animation clips, morphs, live renderer and other assets remain unverified.
- Code/VoxelGore: fixed-width C API, validated format-neutral descriptors, independent grids, generation-safe handles, 128MiB accounting/reserve policy, baseline-island exclusion, separation without a death prerequisite, strict one-body rebuild per frame, visibility gating, pose/light cache and immutable reference-counted snapshots surviving actor/level cleanup.
- Private material-grouped render packets interpolate position, normalized normal, packed colour and both UV channels from already-skinned corners. Exact mesh identity/count checks and pre-allocation 16-bit limits return an intact fallback.
- 25 real-fixture adapter checks pass. Rigid-head skull initialization and corrected integer-centimetre brain threshold exist, but head/helmet fixtures have not been verified.

Reproduce locally:
Code/Tools/VoxelGore/build_engine_parity.cmd
Code/Tools/VoxelGore/build_adapter_check.cmd
Logs: Code/Tools/VoxelGore/build/engine_parity_build.log and adapter_check.log.
CPU parity dump: Code/Tools/VoxelGore/output/engine_parity.txt.
Optional CMake targets are excluded from all; no game binary was rebuilt/deployed.

NEXT: native SP impact-time palette queue and instance ownership; exact source mesh/material identities; guarded replacement of legacy gore only when a complete voxel draw exists; bind private packets/flesh to DX8 submission with per-instance cull override and state restoration. Then heads/helmets, cosmetic pieces/physics sleep, blood, and single-player lost-limb reactions. Do not silently add gameplay/network changes. No MP correctness claim. No active runtime gore replacement yet. Prepare a separate test copy only; gameplay testing requires Kamal's permission.

## Native connection completed — 2026-11-04
The SP bullet hook now calls GoreManager::Prepare_Voxel_Bullet before Apply_Damage_Extended, carrying exact world contact/direction, ammo ID and scaled damage. Managed victims skip competing legacy ballistic tunnels and bone collapse. ww3d2/voxel_skin.cpp is linked into the game and MeshClass::Render invokes it before the intact shared-index queue. Actual posed vertices feed private per-pass material groups, both UV channels, double-sided skin and flesh, additional passes, saved/restored DX8 state, and pre-narrowing 16-bit VB/IB splitting. Logic-frame budgeting is shared by stereo views.

Detached original-skin pieces now have frozen impact palettes, cosmetic scene nodes marked uncollidable/dont-save, terrain-only ray callbacks, Z-up adaptation of the unchanged core RigidPiece, 10-second sleep and 30-second cleanup. Head/helmet anatomy, blood-layer expansion, heavy blast routing, saved visual wounds and living-limb AI reactions are not certified by this pass. Unsupported render states fall back intact and are logged once.

Renegade.exe builds successfully. The 25 adapter checks and 14,262 engine skinning comparisons pass. A new headless native_bridge_check passes precise-hit adoption, shared-model instance isolation, cut/flesh command submissions through the real MeshClass::Render hook and level cleanup. Its offline draw sink replaces only GPU submission; no game/window/device was launched.

Installed ONLY into D:/Command & Conquer Renegade(tm)/RenegadeVR Test. Prior test EXE/config backed up under Voxel-backup-*. Matching PDB and VOXEL-GORE-TEST.txt installed. Main EXE hash verified unchanged. See Code/Tools/VoxelGore/output/native-deployment.json. User GPU/visual testing is the next gate. No main-install changes, game launch, remote preview or image transfer.

## 2026-11-04 follow-up: attached heads and vanilla weapons

Supersedes the previous native deployment. Actual user logs identified legacy
geometry mutation and shots assigned to invisible lower LOD meshes. Enumeration
now selects only highest-detail HLOD models plus attachments; wounded actors
retain that detail level. Shot rays use the nearest actual skin vertex bone to
transform into rest space. Separately attached S_C_HEAD heads support multiple
bones and finer 7.5mm cells. Supported misses and unsupported assets no longer
fall through into legacy ballistic mesh mutation in voxel-enabled solo play.

Both c_nod_mg_head and c_nod_mgo_head now pass the native head regression (actual
W3D inputs, highest-LOD selection, cut skin and flesh submission). Adapter checks
pass; body engine parity remains 14,262 comparisons with zero measured error.
These headless checks do not prove live GPU rendering or all character variants.

Vanilla weapon routing, definition ammo/damage tuning and icons are restored.
146 manifested weapon overrides were renamed into a dated backup in the test
copy, including the earlier enhanced pack. Stock archive resolution was checked
for pistol, rifle, sniper and magazines. Terrain assets were excluded. Source
models/downloads are retained. restore_vanilla_weapons.py records exact rollback
provenance in output/vanilla-weapons-deployment.json.

Rebuilt EXE/PDB deployed to RenegadeVR Test only. Main EXE hash unchanged. User
must restart into a fresh solo mission; no game, desktop control or network used.
Current deployment record: Code/Tools/VoxelGore/output/native-deployment.json.
