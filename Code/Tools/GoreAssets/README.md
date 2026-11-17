# Renegade gore ("brutal" mode)

Cosmetic gore for the single-player game: blood, decapitation, limbs blown off, flying
body parts, guts and chunks, with Soldier of Fortune II's stump caps and gore models,
like Brutally RealRTCW. Built and seen working in game on 2026-10-18 (see "Tested in game").

## What it does

| Event | Result |
|---|---|
| Any bullet hit on a soldier | Blood spray at the hit bone, a splat on the wall or floor behind |
| Killing head shot (50%, any weapon) | Head comes off and flies, neck cap and a pumping fountain, brain chunk |
| Killing arm/leg shot (50%, any weapon) | Cut at the shoulder/elbow/hip/knee, the limb flies, stump cap, fountain, sometimes a bone |
| Close explosion kill | 1–2 parts off, chunks, sometimes an organ |
| Point-blank explosion kill | Most parts off, lots of chunks and organs, big splatter |
| Any kill | A blood pool under the body after it falls |
| Flying parts | Bounce off the terrain, leave blood where they land, disappear after 45 s |

Not included, by request: picking up or throwing heads. No stealth sections in
Renegade make that useful.

Numbers are defaults from [gore.ini](gore.ini).

**Rules it keeps:**
- Only in single player (`IS_SOLOPLAY`). Multiplayer clients don't see every hit, so it
  would look different on each machine.
- Purely visual. It doesn't change damage, AI, physics objects, saves or networking.
- Your own soldier is never dismembered (the camera is in its head). `Player=1` adds
  blood only.
- Fire, chemical, electric and Tiberium kills get no gore (`NoGoreWarheads`).
- With the SoF2 assets missing, blood and dismemberment still work. Caps, chunks and
  organs are skipped, and the debug log says so once.

## How it works

Renegade's characters are W3D skins on a Character Studio biped skeleton (`C HEAD`,
`C L UPPERARM`…). Each skin vertex follows exactly one bone. This is the same idea as
Ghoul 2's cuts in SoF2, but done in W3D:

- **Hiding a body part:** `RenderObjClass::Set_Bone_Skin_Hidden(bone)` collapses that
  bone's skin vertices onto the bone, so the part disappears. Rigid meshes on the bone
  (helmets, gear) are hidden too. Done for the cut bone and all of its children.
- **The flying part:** a second copy of the soldier's model, frozen in the victim's exact
  pose (`Control_Bone_Root_Relative`), with everything *except* the cut part hidden. It is
  then moved as a rigid body: gravity, spin, bounces against terrain ray casts.
- **Stumps:** an SoF2 cap mesh and a blood emitter attached to the cut bone, so they
  follow the body's death animation.

| File | Change |
|---|---|
| `Code/Combat/goremanager.{h,cpp}` | New: all gore logic, gibs, emitters, decals, settings |
| `Code/Combat/soldier.cpp` | Calls `GoreManager::Soldier_Damaged` after damage is applied |
| `Code/Combat/combat.cpp` | `Init`/`Shutdown`, `Update` after the physics scene, cleanup on level unload |
| `Code/Combat/Combat.dsp` | Adds the new files |
| `Code/ww3d2/pivot.{h,cpp}` | Pivot flags: `SkinHidden`, `CaptureRootRelative` |
| `Code/ww3d2/htree.{h,cpp}` | `Set_Skin_Hidden`, `Control_Bone_Root_Relative`, `Get_Skin_Transform` |
| `Code/ww3d2/meshmdl.cpp` | The 4 skinning loops use `Get_Skin_Transform` |
| `Code/ww3d2/rendobj.h`, `animobj.{h,cpp}` | Render-object virtuals for the above |
| `Code/Tools/GoreAssets/convert_sof2_gore.py` | New: SoF2 pk3 → W3D meshes and TGA textures |
| `Code/Tools/GoreAssets/gore.ini` | New: settings, copy next to the exe |

Every change to an existing file is marked `RENEGADE_VR`. With no bone hidden,
`Get_Skin_Transform` returns the same matrix as before, so existing rendering is unchanged.

## Assets from Soldier of Fortune II

Uses the same SoF2 sources Brutally RealRTCW used: the `cap_*_off` stump surfaces of
`models/characters/average_sleeves/average_sleeves.glm` with `models/characters/caps/caps.jpg`,
and `models/characters/gore/` (bone gib, chunks, guts, lung, stomach, ribs, brain, blood
sprays, splats and pools).

**Licensing:** these belong to Raven Software / Activision. The converter reads them
from *your* SoF2 install. Its output goes into your Renegade `Data` folder for your own
use. Don't commit the output to this GPL repository and don't put it in a public
release. A public release would need replacement art.

Convert (needs Python 3 and Pillow; not run yet):

```bash
py -3 -m pip install pillow
```

```bash
py -3 "Code/Tools/GoreAssets/convert_sof2_gore.py" --sof2 "F:/RealRTCW VR project/Soldier of Fortune 2/base" --out "<clean Renegade install>/Data"
```

Then copy `Code/Tools/GoreAssets/gore.ini` next to `Renegade.exe`.

**Run on 2026-10-17** against `F:\RealRTCW VR project\Soldier of Fortune 2\base`. It wrote 36
files (21 meshes, 14 textures and a manifest) to
`D:\Command & Conquer Renegade(tm)\RenegadeVR Staging\Data`, with nothing missing. A separate
checker then walked every `.w3d` chunk tree. All chunk sizes, vertex, triangle and UV
counts are consistent, every referenced texture exists, and the mesh names match the file
names. It also rendered front, side and top previews. From those:
- The caps are flat discs facing +X.
- SoF2's raw chunk sizes vary wildly (1 cm to 64 cm), so `PIECES` carries a per-model scale.
  After scaling: small chunk ~4 cm, large ~10–12 cm, brain ~16 cm, rib cage ~38 cm.
- `chunk_med` / `chunk_gib_med` are thin spikes (SoF2 streak effects). They are converted
  but the game doesn't throw them.
- SoF2 has no neck cap, so the neck uses the shoulder cap, like Brutally RealRTCW.
- SoF2's `cap_hip_r_off` is a lumpy pelvis piece, so the hip uses the top-of-leg cap on
  both sides.
- The flying parts use SoF2's own limb-end caps (`cap_arm_uppr_r_torso_off` and others)
  instead of the body cap turned round.

**Tested in game (2026-10-18)** with the ported build, mission M01 and the `GORETEST`
developer option (see Code/Port/README.md). Head shots, arm and leg cuts and point-blank
explosions all run without a crash for minutes:
- head shot: head plus brain thrown;
- limb cuts: 1-2 pieces;
- explosion: about 14 pieces.

On screen: severed limbs and heads lying where they landed, capped stumps, SoF2
organs/chunks, blood splats and pools on the ground.

Tuning after the first look:
- The spray texture is now SoF2's `blood_drop` (the `gil_spurt` streak sheet looked like
  scribbles as a sprite).
- Particle counts are budgeted per event (the explosion burst was about 220 particles).
- Pools are smaller (0.5-0.85 m), and there are fewer chunks.

**Fixed after the first play (2026-10-18):**
- **Black sheet below the neck on decapitated heads.** Each hidden bone used to collapse onto
  its own joint. Skin triangles that span two hidden bones then became flat sheets between
  those joints: on the flying head, the whole hidden body made a dark spike hanging from the
  gas mask. Now every hidden bone of a cut collapses onto the cut joint
  (`Set_Bone_Skin_Hidden(bone, true, collapse_to)`).
- **No limb cuts in play.** The minimum damage was measured as the health the last bullet took,
  usually a few points, against 25 needed. Rifles and pistols never qualified. The shot's own
  damage is used now, and the minimums default to 0 (chances: head 50%, limbs 50%).
- **Everyone stuck in place after a kill (suspected cause, fix untested).** Flying pieces
  went into the world in the default "collides with everything" group. A cut limb is a whole
  copy of the soldier model, hit boxes included, so it could block movement and sight lines.
  Corpses are switched to bullet-only, but the pieces never were. Now every gore object
  collides with nothing. A piece whose transform goes non-finite is removed, and a limb is
  not copied from a broken pose. Pieces also move through their scene node, so their
  culling box follows them.

Not judged yet: how the caps line up up close, and whether the flying limb copies keep
the victim's pose. The captures are too small to tell; worth a close look in play.

The converter prints the `cap_*` surfaces it found and lists anything missing. Sizes are
SoF2 units × 0.0254 m (`--scale` changes it). Cap rim radii are set per joint in `CAPS` at
the top of the script.

## Validation

Build (VC6, see [Code/RenegadeVR/README.md](../../RenegadeVR/README.md)), convert the assets,
then in single player:

1. **Gore off** (`Enabled=0`): the game looks and plays as before. The skinning change is
   on every character, so look at characters, vehicles with skins, and cinematics.
2. **Blood:** shoot soldiers. Sprays come from the right body part, and splats land on
   walls and floors behind them. Pools appear under bodies.
3. **Head shots** (sniper rifle, ≥ 30 damage): the head comes off cleanly with no
   stretched triangles. The neck cap sits on the neck and faces out. The head flies with
   its helmet.
4. **Limbs:** shoot to kill in the upper arm, forearm, thigh and calf. The cut is at the
   right joint and the stump cap faces outward.
5. **Explosions:** grenade or C4 close by, then point blank.
6. **Pieces:** they bounce on terrain and don't fall through floors (mostly; it's a ray
   test, not real collision). They disappear after `GibLifetime`.
7. **Level change / reload:** no leftover pieces, no crash.
8. **Multiplayer / skirmish with others:** no gore.
9. Check `_logfile.txt` / debug output for `Gore:` lines (missing assets).

## Unconfirmed until tested

- **Bone axes:** the caps assume a biped bone's +X runs along the bone toward its child.
  If caps face sideways, the axis in `GoreManager`'s caps (and the converter's frame)
  needs changing.
- **Skeleton and box names:** the `C …` bone names and `K_…` hit-box names come from
  Renegade's code (`humanstate.cpp`, `humanrecoil.cpp`). Characters on a different
  skeleton (mutants, Raveshaw, Mendoza) may lack some bones. Those cuts are then skipped.
- **Warhead names** in `NoGoreWarheads` aren't checked against `armor.ini`.
- **Decal textures:** W3D decals use the texture's alpha. The SoF2 PNGs have alpha, but
  how they blend in W3D isn't checked.
- **Lower LODs:** rigid parts on a cut bone are hidden for the current LOD only. A far-away
  body switching LOD could show a helmet floating where the head was.
- **Converter output in game:** the files are structurally valid, but whether W3D's loader
  accepts them is only proven by the game loading them.

## Next steps

- Bullet holes and wound textures on bodies (RTCW's "Bullet wounds"), using W3D's
  per-mesh decals (`MeshClass::Create_Decal` works on skins).
- Replace the ray-test bounces with the Jolt prototype (VR_MOD_PLAN.md §6): proper
  ragdolls driving the same bones (`Control_Bone`), and grabbable parts in VR.
- Optional shots to corpses (RTCW cuts dead bodies too). Renegade removes bodies
  quickly, so check corpse lifetime first.

## 2026-10-20: corpse dismemberment

`CorpseDismemberment=1` (default) keeps dead soldiers in terrain-and-bullet collision,
without making them block movement. Later bullet hits can remove their remaining
parts using the normal damage/chance settings. Close explosions can also cut corpses.
The corpse path avoids damage stats, dialogue, repeated death events and repeated
blood-pool scheduling. A later upper-arm/thigh gib preserves earlier cuts.

Developer regression scenario: `MISSION=M01 SKIPINTRO=1 CORPSETEST`. It kills a victim
with a chest hit, then hits its forearm, upper arm, thigh and head, checks repeat hits
do not duplicate pieces, and traces against it using the bullet collision group.
Results go to `gore_test.log`. This flag is never used by the player launchers.
