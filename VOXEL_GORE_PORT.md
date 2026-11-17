# Layered voxel gore: Renegade port plan

Source audit: 2026-11-01. Read `voxel_gore_handoff/VOXEL_GORE_HANDOFF.md` fully before this audit. All source references below were checked in this checkout. They include its existing Brutal Renegade changes. No game was launched.

## Build and scope

The original `Code/commando.dsw` / `.dsp` build is VC6-era **32-bit x86, D3D8**. C++17 core code cannot be dropped into VC6. This checkout also has a modern x86 MSVC port: `Code/Port/README.md`, `Code/CMakeLists.txt`, `Code/Port/sources.cmake`. Its documented build uses CMake + Ninja from an x86 developer environment; avoid the VS generator because the path contains `&`. The early build/status paragraphs in VR_MOD_PLAN.md are historical; its Stage 5 note and the Port README describe the newer port. Keep the first proof outside both game builds. Later either use the modern game compiler explicitly or build the C++17 core in an x86 DLL with a versioned VC6-safe C ABI. No STL, engine classes, exceptions or cross-CRT ownership across that boundary. Validate struct sizes and use explicit buffer ownership/release.

Gore is already implemented here: `Combat/goremanager.cpp`, the bullet hook, bone hiding in `ww3d2/htree.h`, and `mesh_wounds.inl`, `mesh_tunnel.inl`, `mesh_thermal.inl` included by `mesh.cpp`. Generated `WoundSkinInfluences` in `meshmdl.cpp` blend a second bone for existing wound geometry. Voxel mode must disable competing cavity/tunnel/random limb-cut mutations for managed victims. Thermal/chemical effects need a deliberate coexistence policy. Initially preserve the current local-player camera exclusion.

All voxel state is client cosmetic: no gameplay damage, AI, hit-box, scoring, physics-object, save or damage-packet changes; no voxel allocations on dedicated servers. Start in SP. `GoreManager::Is_Enabled` (goremanager.cpp:985) currently permits solo play OR an offline-bot server override, so it is not a general MP safety gate.

## Handoff section 8 mapped to source

### 1. W3D replaces the MDS parser

* `Code/ww3d2/w3d_file.h`: chunk IDs; `W3dMeshHeader3Struct` (:1139), `W3dVertInfStruct` (:1175), `W3dHierarchyStruct` (:1290), `W3dPivotStruct` (:1298), HLOD structs (:1979). Chunks are little endian with 8-byte ID/length headers; high length bit denotes children. Parse bounded fields rather than unchecked native struct casts.
* `MeshModelClass::Load_W3D` (`meshmdlio.cpp:236`) requires MESH_HEADER3 and routes chunks through `read_chunks`. Geometry readers in `meshgeometry.cpp`: `read_vertices` (:1774), `read_vertex_normals` (:1807), `read_triangles` (:1837), `read_vertex_influences` (:1925). Preserve original vertex/triangle order.
* **One file influence per vertex**, not RTCW weighted skinning: `W3dVertInfStruct` is uint16 BoneIdx plus six padding bytes; the loader copies only BoneIdx. File positions/normals are bone-local. `MeshModelClass::get_deformed_vertices` (both overloads), `compose_deformed_vertex_buffer`, `get_deformed_screenspace_vertices` in `meshmdl.cpp:307,320,355,424` apply `HTree::Get_Skin_Transform(link)` directly. There is no extra inverse-bind matrix applied to file positions. Dominant bone = BoneIdx. Runtime `WoundSkinInfluences` are a separate modification, not file weights. Build templates from pristine geometry before legacy mutations.
* `HTreeClass::Load_W3D` (`htree.cpp:169`) / `read_pivots` (:252): BaseTransform = translation times `Build_Matrix3D(Quaternion(x,y,z,w))` (`Code/wwmath/quat.cpp`). The loader uses quaternion rotation, not Euler angles or pivot fixups. `Base_Update(identity)` (:500) forces pivot 0 to identity; pivots 1 onward become parent.Transform * BaseTransform in file order. Pre-3.0 files insert a root and shift parents; implement that explicitly or reject those versions in the first tool.
* `HLodDefClass::Load_W3D`, `read_header`, `SubObjectArrayClass::Load_W3D` (`hlod.cpp:577,641,800`) resolve mesh names, hierarchy name and attachment bones. The skeleton may be in a different W3D inside always.dat. Rigid meshes use the attachment transform; skins use vertex links. Exclude hidden hit boxes, collision/proxy boxes and shadow meshes from the anatomical solid.

Replace Gore_LoadModel's MDS reader with a format-neutral descriptor: skeleton parents/names/rest matrices; selected surfaces with original local positions/normals/links, rest positions/normals, UV channels, indices and material/pass attribution. Skin rest position = restBone[link] * filePosition; rest normal = rotation-only transform as in the engine. Rigid flesh surfaces use attachment bones. Concatenate surfaces only for gore::Body, retaining per-surface triangle identities for cuts. Core bones are uint8 (0..254): validate/densely remap W3D's uint16 links, never truncate. Cache keys must include actual hierarchy morph/scale and geometry identity, not filename alone.

### 2. Offline proof first

Use read-only real game assets; write working copies, logs and OBJs inside `Code/Tools/VoxelGore` only. Compile the unmodified handoff core separately. Reuse reference SkinStays/Refiner/BuildSkin, hit mapping, flesh posing and sever logic with metre units and Renegade names. Export rest skin, voxel surface, cut original skin (kept triangles plus barycentric subs), wound-only flesh and separated pieces. Record input hashes, excluded meshes, bounds, bones, counts, carved/bone cells and timings. Render front/side previews with the handoff viewer. Offline status/results are appended below.

### 3. Per-frame bone transforms

`Animatable3DObjClass::Get_Bone_Transform(int)` (`animobj.cpp:616`) validates transforms and evaluates a dirty hierarchy before returning HTree->Get_Transform. `Update_Sub_Object_Transforms` (:763) handles base, single, blended and combo animation. `HLodClass::Update_Sub_Object_Transforms` (`hlod.cpp:3353`) also places child/additional models on attachment bones. `MeshClass::Get_Deformed_Vertices` / `Compose_Deformed_Vertex_Buffer` (`mesh.cpp:516,535,544`) pass the mesh's container HTree to the skinning loops.

Evaluated HTree matrices include the world/root transform. DX8TextureCategoryClass::Render uses an identity world transform for skins (dx8renderer.cpp:1832), as does MeshClass::Render_Material_Pass (mesh.cpp:845): avoid applying the actor root twice. Wrapper model-space bones = inverse(rootWorld) * boneWorld. Validate the render hierarchy first; at draw time use the exact container palette used by that skin, including capture/VR/recoil/morph controls. Do not resample an animation frame or use a stale tree. Clone a hierarchy for base extraction; never Base_Update(identity) a live actor just to obtain rest bones.

Pose ABI is **nine matrix floats in row order, then three translation floats**. Matrix3D stores interleaved 3x4 rows, so memcpy of 12 floats is wrong. Flesh transform = poseBone * inverse(restBone); do not apply that inverse-bind a second time to bone-local file vertices. Snapshot impact-time bones before damage changes the pose or hides a part; use current draw-time bones for flesh. Disable legacy collapse for managed bones because hidden skin transforms can be singular.

### 4. Hits, weapon information and MP

`BulletDataClass::Bullet_Collision_Occurred` (`Combat/bullet.cpp:245`) has ContactPoint/Normal (:260), Velocity, AmmoDefinition, owner, victim via OtherObj's CombatPhysObserver and collided render-object name (:335). LastHitID (:342) rejects repeated hits. At :359-364 it snapshots dead state, calls Apply_Damage_Extended, then already calls `GoreManager::Bullet_Hit` with precise point/direction/victim/box. Queue a cosmetic hit after the same rejection/dedup checks, snapshotting the pre-damage palette. Carry ammo definition ID and scaled base damage explicitly: OffenseObjectClass has damage/warhead/owner, not an unambiguous weapon/primary-secondary ammo ID; looking up the shooter's current weapon later is unreliable.

`BulletClass::Collision_Occurred` (:743) routes physical projectiles here. `Simulate_Instant_Bullet` (:851) raycasts, explicitly computes ContactPoint (:897), and calls the same callback (:910). `WeaponClass::Fire_Bullet` (`weapons.cpp:764`) selects ammo and projectile velocity, including VR muzzle handling. Use actual projectile/muzzle direction, not view forward.

`SoldierGameObj::Apply_Damage_Extended` (`soldier.cpp:3607`) has victim, offense, direction and box name but **no exact point or ammo ID**. Its corpse branch (:3639) skips ordinary damage/death bookkeeping and invokes cosmetic damage handling; ordinary post-damage gore call is :3844. Use these for death/non-bullet handling, not a duplicate bullet carve. Corpses must stay cosmetic and their collision policy must not change as part of this port.

Normal MP health is applied on the server, but actual source also supports trusted-client bullet damage: `DefenseObjectClass::Apply_Damage` / `Request_Damage` (`damage.cpp:699,758`), `cCsDamageEvent::{Init,Act,Export_Creation,Import_Creation}` (`csdamageevent.cpp:74,99,151,173`). That event sends sender/damager/victim IDs, damage and warhead only; **no point/direction/box/ammo**. Act invokes damage without those fields. This is not an RTCW replicated flesh-hit event. No complete all-observer cosmetic impact stream was verified. Start SP-only. A later client can show locally observed impacts with acknowledged visual divergence; complete remote coverage requires a separately designed cosmetic stream/dedup policy without changing authoritative damage. Do not infer accurate remote wounds from health replication.

World-to-model: p = inverse(rootWorld)*worldPoint; d = inverse(rootWorld).linear*Velocity, normalized. Then use nearest posed parent-child segment and rest*inverse(pose) mapping, with the handoff's 8 cm miss tolerance. Track ID plus generation/model identity, not bare pointers; clear on respawn/model swap/unload and evict unseen victims. Reject repairs, unsupported weapons and unsuitable vehicle/player cases explicitly.

### 5. DX8 cuts and second flesh pass

`MeshClass::Render` (`mesh.cpp:686`) registers geometry and queues per-material tasks (:765). `DX8SkinFVFCategoryContainer::Render` (`dx8renderer.cpp:1323`) batches skins into a dynamic VB, calls mesh->Get_Deformed_Vertices (:1385), copies posed positions/normals/diffuse/both UV sets, assigns base offsets (:1424), binds a shared index buffer (:1435), then runs texture and procedural passes (:1437-1446).

`DX8FVFCategoryContainer::Generate_Texture_Categories` (:1228), `DX8TextureCategoryClass::Add_Mesh` (definition at :1519), and Vertex_Split_Table build shared **material-grouped** indices from source triangles. `DX8PolygonRendererClass::Render` (`dx8polygonrenderer.h:117`) calls DX8Wrapper::Draw_Triangles/Draw_Strip; Render_Sorted (:142) queues sorting triangles. These low-level draws no longer carry original triangle IDs. Mutating the shared MeshModel/index buffer would wound other instances or misassign material ranges.

Add a per-instance wounded path: preserve original triangle-to-pass/material identity, skin as usual, then append subs interpolated from the already-posed original corners (position, normalized normal, both UV channels, vertex color). Build private dynamic kept/sub indices grouped by source material/pass. Bypass strips for wounded geometry. Keep ordinary shared batching for intact instances. Budget appended vertices BEFORE allocating/splitting the existing 65,535-vertex/16-bit-index batches. Bound sub counts/bytes; honor sorting/additional/procedural passes or use a complete intact fallback. Validate surface identity, geometry revision, counts and LOD, not counts alone.

Emit flesh once per victim per view through a dedicated cosmetic RenderObj/task with dynamic VB/IB, unlit vertex color, depth test/write and no backface culling. It must not be a duplicate collision-bearing soldier. Wound-only remesh uses depthCm for rim/muscle and 255 for bone; brains need a head-region rule. Translate RTCW GL material behavior to D3D8 fixed-function states; do not copy its shader text. Cut lists are the first DX8 implementation, not a 3D shader mask. Freeze buffers until deferred texture/sorting tasks and both VR eyes are finished; RTCW's lifetime assumption is not evidence about Renegade. Separately validate `MeshModelClass::Shadow_Render` and projector passes; intact silhouette shadows are a possible limitation.

**Highest internal HLOD is LodCount-1, not 0**: `HLodClass::Prepare_LOD` around hlod.cpp:2876, Set_LOD_Level (:3092), high_lod at :3409. Force/retain that detail for wounded body and nested head across automatic LOD selection. This checkout already sets HLodClass::Always_Top_LOD=true (definition immediately before Prepare_LOD); retain a per-wounded-instance guarantee if that global policy changes. One Set_LOD_Level can be overwritten by automatic LOD selection. Filename `_l0` does not specify the internal array index.

### 6. Heads and helmets

`SoldierGameObj::Find_Head_Model` (`soldier.cpp:3298`) searches objects attached to C HEAD for nested S_A_HEAD/S_B_HEAD skeletons; speech animation drives these (:2548). Some skins can instead contain integrated head vertices. Audit actual loaded surfaces/attachments. Nested facial skins need their own palette composed through the body's head attachment; a rigid-head approximation must be stated as such.

For integrated heads tag the owning bone/descendants plus an anatomical region, build a 6-25 mm subsurface skull shell there, and classify brains inside. Also audit the reference Gore_Draw condition `depthCm > 25`: gorecore.cpp stores integer centimetres, so this means 25 cm, not 2.5 cm, and should not be copied as a Renegade brain threshold. The reference Model::head flag applies to the whole model: setting it on an entire soldier would miscolor its torso. Helmets/gas masks/hair/eyewear may be rigid children or integrated skin. Do not voxelize protective gear as flesh. Define per-asset leave/hide/cut behavior and test all LODs. Existing rigid-part hiding helpers are guidance, not proof all helmets are covered.

### 7. Units

`Code/wwphys/phunits.h`: PHYSICS_METERS(x)=x; kilograms/seconds likewise. Renegade is Z-up. Confirm plausible metre bounds on real assets. Wrapper units-to-metres becomes **1**, replacing RTCW's 0.0254; audit every inch-based threshold and bone radius too. Keep mesh positions/pose translations in one space. The core's optional `RigidPiece::step(dt,floorY)` assumes Y-up: map axes at that physics adapter if used for Z-up Renegade. Geometry core stays unchanged.

### 8. Tune and budget

Start at 0.015 m body cells, 0.010 m separately scoped heads, crumb 40. Round energy=0.25+damage*0.02 m; rIn=min(0.03,0.012+damage*0.0002), rOut=min(0.05,2*rIn). Use scaled ammo damage, not last health lost. Classify pellets/fire/chemical/energy/repair explicitly. Bone rods need Renegade uppercase/spaced names (C SPINE*, C L UPPERARM etc.), not RTCW's case-sensitive name table. Compare baseline components before calling a disconnected asset island a severed limb. Cap wounded bodies, pieces, subtriangles and bytes for x86 address space. Rebuild on hits, pose unchanged flesh each draw. Profile full remesh first; dirty-region adoption needs persistent patch ownership and seams. Handoff timings are not Renegade measurements.

## Ordered plan and done criteria

1. **Offline W3D/rest-pose proof, new tool only.** Validate chunks/counts/indices/links/hierarchy; read external skeletons without changing the install. Dump rest skin/bones, exclusions and bounds; compile the unchanged core and run its bundled checks; voxelize, shoot chest/limb, export kept/sub skin, wound flesh and severed pieces; render front/side previews. Done: plausible real soldier, filled voxels, nonzero removal/flesh, valid OBJs/barycentrics, asset hashes and reproducible commands. Engine parity remains a separate gate: compare base and two animated/captured poses against actual engine matrix/vertex/normal dumps within a stated tolerance. Do not claim bit-for-bit identity from a separately implemented loader.
2. **Format-neutral adapter/ABI.** Extract reusable wrapper logic with metres, names, surface/material identities, checked palettes and head-region rules. Done: step 1 outputs preserved; animated hit/rest round trips and cuts verified; x86 ABI sizes/ownership checked. No engine edits in this session.
3. **SP cosmetic state/hits.** Add lifetime/cache, precise hit queue and evaluated palette snapshots; gate legacy cuts, handle death/corpse/respawn/unload. Done: one carve per accepted hit, no healing holes, living parts stay attached, separation only after authoritative death; gore-off baseline and gameplay/collision unchanged.
4. **DX8 private cuts/flesh.** Implement dynamic wounded path, preserve materials/UVs/passes, force detail, enforce 16-bit/lifetime budgets. Done: shared-model victims wound independently; walk/death/VR poses track; no UV tearing/black flesh/overflow/stale eye buffers or duplicate flesh; intact fallback works. Check shadows/transparency/thermal overlays/device reset.
5. **Heads/pieces.** Complete nested/integrated heads, skull/brains/helmet policy, severed original skin/flesh frozen at sever time, cosmetic ballistics only. Done: repeat corpse cuts conserve parts and stumps; pieces block no actor or bullet; close-up facial/helmet/LOD inspection, cleanup on unload.
6. **Performance and optional MP.** Measure x86 CPU/VB/IB/memory under repeated hits and stereo; only then adopt dirty patches. MP is locally observed cosmetics with documented coverage or a separately approved cosmetic event design. Done: measured bounded budgets/eviction, no dedicated-server work or gameplay/protocol side effects; host/client/dedicated/respawn validation before any MP correctness claim.

## Verification limits

Verified implementations above by reading source. No game launched, game build attempted, existing engine files edited, or game install files written. Live engine skinning parity, W3D animation evaluation, head/helmet coverage, DX8 integration, visual/performance behavior and complete remote MP hit coverage remain unverified. Offline proof results follow when available.


## Offline proof completed in this session

New tool: [Code/Tools/VoxelGore/README.md](Code/Tools/VoxelGore/README.md). Reproduction commands, design limits and all output names are there. Only this document and new files under that directory were written; no existing engine/handoff files or game install folders were modified. This folder has no .git metadata, so there is no git diff/status evidence; scope is based on the actual write operations.

Read the real loose `Renegade/Data/c_nod_mg_l0.w3d` (SHA-256 ac5788a67792380919f58c09b5e521988401355e6866c663edf2533565fda67b) and extracted only a working copy of `s_a_human.w3d` from `always.dat` at 242796600+2552 (SHA-256 2f6d5aa552685c600345813a22dcc7608183e8dafb99740ec0bfe8f94547688d). The input manifest records these sources. The fixture has one visible skin BODY_0, 4,754 vertices / 8,022 triangles, and 23 bones. Shadow and hidden hit meshes were excluded. Its measured height is 1.853 m. Source skeleton has C SPINE and C SPINE1, **no C SPINE2**: prefer checked palettes and fallbacks rather than assuming bone names from other characters.

Compiled with MSVC C++17 Release **x86** outside the game build. The original core suite printed ALL PASSED (0 failures); actual test file contains 18 CHECK calls and 18 passed in this run, despite the handoff saying 19/19. At 1.5 cm, the soldier has 456,576 grid cells, 41,066 filled and 833 bone. Baseline has one component at crumb=40. Three chest shots removed 358/303/293 cells, hitting 4/44/0 bone cells, and all exited. Wound output has 7,980 kept source triangles plus 845 subs; flesh has 749 vertices / 1,412 triangles with rim/muscle/bone vertex colors. An offline corpse forearm burst removed 207 cells and detached one C L HAND component using the original victim skin/flesh. Synthetic elbow posing and barycentric/OBJ checks passed; malformed chunks/invalid links were rejected. See output/proof.log and output/verification.json.

Inspected [rest](Code/Tools/VoxelGore/output/rest.png), [wounds](Code/Tools/VoxelGore/output/wounds.png) and [severed](Code/Tools/VoxelGore/output/severed.png) previews: plausible rest geometry, open chest tunnels with wound walls, and a detached hand/cut face. Flat-tint software previews do not display the OBJ flesh vertex colors. BODY_0 visually includes the helmet/head silhouette; this proof does not partition it as anatomy versus gear. Head shells/brains and nested head fixtures remain future work.

Step 1's basic real-W3D -> rest skin -> unmodified core -> cut/flesh/sever OBJ proof is complete. **Its engine-parity gate remains open**: no live renderer dumps or real animation comparisons were made. Source formula matching is not proof of bit-for-bit equivalence. UV/material passes, morph/scale, runtime DX8, VR lifetime and MP behavior remain untested. Representative offline timings were ~9.4 ms template build, ~0.13-0.17 ms per carve, ~11 ms complete wound rebuild/pose; these are not in-game budgets.

## November 3 continuation
The updated handoff and current implementation evidence are tracked in VOXEL_GORE_STATUS.md. Its live separation and finer cell sizes supersede the original plan's corpse-only / 1.5cm defaults. The actual engine HTree + MeshModel CPU skinning comparison now passes on the Nod fixture at rest and two captured poses (14,262 comparisons; zero measured errors). A reusable C adapter and private material-grouped geometry builder pass 25 fixture checks. Runtime SP ownership/hits, DX8 submission and full head/helmet/physics/blood behavior remain next; no game was launched or installation modified.

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
