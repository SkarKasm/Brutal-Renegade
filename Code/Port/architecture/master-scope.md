# Accepted master scope — 22 October 2026

This blueprint extends the existing Brutal Renegade development codebase.
Current implementation: continue the complete blueprint after the initial Module1 headers.
Priority: fix Command & Conquer main-install startup, then continue outstanding work.
Deploy successful gameplay builds to the main installation with backups.
Existing gameplay, gore, RTS/bot and VR prototypes remain in the same project.
Testing is authorized by the user as of 23 October 2026. Use the development copy for tests; deploy successful builds to the main installation with backups.

## 1. Native x64 engine and memory

Audit all runtime pointers/handles/counts, use native address-sized types and
checked arithmetic. Preserve binary data widths through explicit schemas and
reference-token maps. Retire artificial process-memory caps only as native
allocators and dependencies become ready. Implement native audio (OpenAL target),
modern graphics compatibility and video decoding; rebuild source-owned DLLs.
The header component is now present. Whole-engine x64 migration is outstanding.
Legacy GPU buffer limits remain independent of CPU address width.

## 2. See-through ballistic cavities and progressive burns

Implement ray-based entry and exit cavities with deformed rims, cutout epicenters
and double-sided internal tissue/bone/fabric tunnels. Bind both ends to the
animated skeleton with explicit endpoint weights; evaluate topology/material
limits during every update. Ragdoll integration remains a separate dependency.
Implement localized healthy/carbonized/exposed-tissue layers, ragged noise masks,
matte carbon and glossy tissue; bind smoke/embers to charred regions after death.
The supplied screenshots are references for the present stretched cavity
interiors and uniform red burn appearance. No new gore implementation is claimed
by the architecture header work.

## 3. Command & Conquer campaign and tactical control

Keep the rebranded main-menu hub and extend the current map ladder into an offline
narrative campaign. Use actual installed C&C_*.mix packages and native level-load
entry points: the illustrative mp_*.w3d names and Engine::LoadSceneFile in the
blueprint are not APIs/assets already established in this codebase.
Victory must account for defined enemy base destruction and economic defeat,
then persist progress before native transition. Specify whether loss of an income
source, remaining credits or retained factories constitutes economic defeat.
Retain building target tracking, squad orders, native credit flow, factory gates,
unit possession and the tactical camera. Fix initialization null references.
Pending earlier work includes overhead visibility/culling and friendly scouting
fog of war, independent of the radar's show-enemies setting.

## 4. Saboteur world and progression

Create a separate mode for Logan Sheppard with exclusive weapon/character asset
names and a default suppressed pistol plus suppressed MP5. Supplied SOCOM/MP5/VKS
archives have been extracted and audited; animation retargeting is unfinished.
Logan already exists as the tutorial instructor (script object ID400005).
Use native GDI_Logan_Sheppard / GDI_Logan_Sheppard_Tutorial definitions and
c_ag_logan / c_logan_l0 / c_logan_head assets as the protagonist baseline.
Preserve any shared Havoc-named textures; names do not establish character identity.
A newly authored Logan model is not a prerequisite.

Build regional biomes from multiplayer environments with coordinate transforms,
streamed collision/actors, regional lighting and persistent territory state.
Seamless stitching requires an actual world-streaming path; loading several
static W3D meshes alone does not establish a functional open world.

Represent territories using stable fixed-width base/biome/faction IDs, major/minor
classification, core sabotage state and separate Nod/GDI actor/spawner identities.
Capture processing must be idempotent: mark capture, stop old spawners, deactivate
old actors, activate GDI assets/guards, update persistent progression and play FX.
Never serialize native pointers or assume a runtime actor ID survives unloading.

Economy tracks active friendly harvesters, captured bases, credit generation and
spending. Start from the blueprint's 25 credits/harvester/second as a tunable value;
Logan's mission rewards and upgrades must remain restrained. Unlock checks require
base progression and funds (and rate gates where specified). A purchase must
validate grant availability first and debit exactly once with rollback on failure.
Do not let repeated capture events or failed purchases generate/spend funds twice.

Stealth combines guard vision cones, raycast visibility, distance, lighting and
stance/movement/firing. Awareness updates scale by delta time, use bounded
exponential growth, clamp 0..100 and trigger localized base alarms at 100. Define
cooldown/decay and hearing separately; hidden enemies must not leak through RTS
markers, selection or targeting. Runtime guards need stable per-actor state.

## Scope status

The new architecture component is implemented and compiles on x86 and AMD64.
Other systems above are accepted requirements and migration work, not completed
features. Current playable engine remains x86. The supplied C++ schematic globals
and Engine/TriggerFX calls will be adapted to native systems in later modules.


## 5. Physical building destruction — accepted 23 October 2026

Extend native building health/destroyed states into physically destructible
assemblies. This is accepted scope, not an implemented feature.

- Localized damage to pre-fractured panels, walls, glazing, pipes and machinery;
  remove the correct render and collision pieces together. Broken walls expose
  actual rooms and create usable openings where structural clearance permits.
- Track supporting sections and trigger staged structural collapse when supports
  fail. Preserve occupied floors and exits until the relevant collapse stage;
  do not remove a whole floor simply because a cosmetic facade panel breaks.
- Destroyed machinery affects native power, refining, factories and defenses.
  Keep BaseController and building victory/economy semantics consistent; avoid
  duplicate destruction announcements, refunds, capture rewards or production.
- Large ruins persist; small fragments use bounded lifetimes and debris budgets.
  Debris must not permanently trap actors, block all routes or exhaust memory.
- Rebuild/invalidate affected navigation links and update collision, cover,
  targeting and RTS picking when openings or obstacles change.
- Use grouped pre-fractured sections with native collision/animation first.
  Integrate Jolt for falling sections/debris after the physics adapter is ready;
  Jolt is currently built separately and is not linked into gameplay.
- Define stable assembly/section IDs and versioned damage state for saving and
  streamed region unloading/reloading. Never persist runtime pointers or actor IDs.
- Reusable building prefabs must group the controller, exterior/interior meshes,
  aggregates/doors/MCTs, lights, purchase terminals and service/spawn/delivery
  anchors. Base relocation/removal and Saboteur faction swaps must use this same
  assembly contract, rather than deleting a controller and leaving its meshes.

Implementation order: assembly ownership and authored fracture metadata; one
building prototype; localized breach/collision and facility consequences;
structural collapse, navigation and persistence; broader faction building set;
Jolt debris integration. Destruction is shared by Command and Saboteur modes,
with mode-specific victory/capture rules. No building removal was performed by
accepting this plan.

## Current continuation checkpoints — 23 October 2026

- Module 1: architecture headers compile x86/x64; playable engine remains x86.
- Module 2: paired head/abdomen cavities, dual-endpoint tunnel skinning, localized
  thermal materials and attached smoke/embers are compiled and installed. Full
  visual/runtime gore validation and broader thermal shader work remain pending.
- Module 3: full-world rendering is confirmed working by the user. Under's first
  exterior terrain pass is installed and tested in four fixed overhead views,
  with 94 tiles loaded and 4/4 collision probes. Enclosed mountain shells,
  underground fitting, other map biomes, new navigation and real stitching remain.
  Scouting fog of war, campaign progression and AI/order validation remain work.
- Module 4: existing Logan identification and weapon archive audits are complete;
  retargeting, sandbox mode, streaming, territory/economy and stealth remain work.
- Module 5: physical building destruction is planned only.

Continue the accepted modules; keep the rejected Under shell-cap experiment
staged until it can preserve underground clearances. Terrain testing permission
also permits the previously deferred gore/gameplay checks. A successful compile
or file deployment must not be reported as visual confirmation.


### Module 1/2 continuation: renderer keys and stock skeleton
Native-width renderer key hashing and model/lighting registry lifecycle fixes are installed. Stock chest-bone fallback is installed. Two native burn/chemical inspections produced 12 captures and passed 32 clone/lighting/free/reuse cycles; no new exception file. Corpse appearance, full cavity regression, whole-engine x64 and the remaining modules are still pending.
