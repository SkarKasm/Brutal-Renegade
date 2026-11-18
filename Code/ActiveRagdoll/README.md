# Jolt active ragdolls, first integration

The shared Renegade character simulation now has an experimental Jolt path for death reactions and subsequent shots into corpses. Flatscreen and OpenXR VR use the same simulation. Pose updates happen once after GameObjManager::Post_Think, independently of render passes.

## Current scope

- Reuses Brutally RealRTCW VR's cg_physics.cpp/.h bridge and bundled Jolt 5.6.0. The bridge's world scale changes from inches to metres. Jolt's MIT licence is retained under third_party/JoltPhysics/LICENSE.
- Automatically suppresses capsule pairs overlapping in the initial animation until they separate. Collision groups use the same one-based pool indices as the contact listener, so suppression applies to the correct corpse.
- Maps eleven capsule segments to the human C PELVIS, spine, head, arm and leg bones, initialized from the current pose. Stores each bone's offset from its physical capsule so the original skin follows its own skeleton.
- Joint motors follow death animation targets and fade over 0 to 2.6 seconds according to the hit region. Headshots go limp immediately. Hits add bounded local velocity at the projectile surface contact and temporarily suppress the struck joint's motor. The scoped bullet contact is consumed by its matching damage call, including the killing shot. New ragdolls inherit the soldier's current velocity.
- Fixed 120 Hz stepping, at most eight catch-up steps, 32 corpse slots, and ten actively driven bodies. Extra corpses retain native presentation when the pool is full. Handles carry generations. Destruction and level unload release the associated bodies and bone controls.
- Imports static physical terrain triangles on level load. An empty collision export disables the feature for that level. Missing bones leave native presentation available. Existing native animation bone captures are sampled before the death ragdoll takes over, rather than preventing ragdoll creation.
- Single-player only, including the game's offline skirmish classification. Player-body IK is excluded. Chemical corpses retain their existing presentation.

## Build and enable

Configure the main engine with BR_JOLT_RAGDOLLS=ON and rebuild Renegade. The option defaults to OFF while integration is experimental. This path uses the existing MSVC x86 toolchain and a static Jolt library, with no extra runtime DLL.

Copy the resulting Renegade.exe into a separate test installation and place the supplied ragdoll.ini beside it. Enabled=0 restores the native presentation on the next level load. ragdoll.log reports level collision export and body creation.

## Offline verification

The standalone CMake project in this directory builds ragdoll_check. It verifies actual joint motor movement, metre-scale gravity, contact against terrain, pause, bounded catch-up, pool capacity, stale handles and lifecycle ownership. Tests use explicit runtime checks in Release builds.

The main engine also exposes the excluded target ragdoll_engine_check. Run it with a locally extracted S_A_HUMAN hierarchy W3D. It loads the hierarchy through Renegade's real HTree loader, constructs the eleven segments at a rotated and translated world origin, and compares Jolt-to-bone matrices against native captured HTree poses over sixty steps. Original game fixture assets stay local.

On November 18, 2026, the x86 core checks passed. The native binding check passed with a maximum matrix error of 4.76837158e-7. Renegade.exe linked successfully. These are CPU and build checks. Headset and flatscreen presentation still require the user's game testing.

## Remaining integration

- Living balance, stagger, wounded-state transitions and recovery.
- Anatomically tuned hinge limits and per-limb muscle profiles. This first adapter uses swing/twist joints.
- Moving doors, lifts and vehicles in the Jolt collision world.
- Voxel detached-piece ownership, physical joint separation and collision/hit-test bounds after large corpse displacement. Existing skin-hidden branches stop driving, while voxel-specific sever state still needs its own binding.
- Save/load of physical poses. Existing loaded corpses can acquire physics when shot; physics state itself is transient.
- Review of corpse appearance, aiming/hit registration, stairs, terrain seams and scene performance with actual game rendering in both modes.

The public Alpha V0.052 release predates this integration.
