/*
**	Command & Conquer Renegade(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Combat                                                       *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/Combat/goremanager.cpp                       $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   GoreManager::Init -- read gore.ini                                                        *
 *   GoreManager::Soldier_Damaged -- blood on every hit, dismemberment on death and corpse hits     *
 *   GoreManager::Update -- move gibs, stop stump fountains, drop blood pools                  *
 *   GoreManager::Level_Unloaded -- remove everything                                          *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "goremanager.h"
#include "hud.h"
#include "chunkio.h"
#include "rawfile.h"
#include <stdint.h>
#include "bullet.h"
#include "weaponmanager.h"
#include "explosion.h"
#include "definitionmgr.h"
#include <float.h>
#include <vector>
#include "soldierobserver.h"		// soldier.h names SoldierObserverClass only as a friend
#include "soldier.h"
#include "damage.h"
#include "combat.h"
#include "gametype.h"
#include "crandom.h"
#include "ccamera.h"
#include "pscene.h"
#include "physcoltest.h"
#include "renderobjphys.h"
#include "assetmgr.h"
#include "rendobj.h"
#include "htree.h"
#include "hlod.h"
#include "hanim.h"
#include "mesh.h"
#include "meshmdl.h"
#include "dx8renderer.h"
#include "part_emt.h"
#include "v3_rnd.h"
#include "texture.h"
#include "surfaceclass.h"
#include "shader.h"
#include "matrix3d.h"
#include "quat.h"
#include "lineseg.h"
#include "gameobjmanager.h"
#include "weapons.h"
#include "smartgameobj.h"
#include "objlibrary.h"
#include "physicalgameobj.h"
#include "wwmath.h"
#include "wwstring.h"
#include "wwdebug.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <set>
#include <string>

/*
** Settings (gore.ini next to the exe, section [Gore]). See Code/Tools/GoreAssets/README.md.
*/
struct GoreSettingsStruct
{
	bool		Enabled;
	bool HeadDestruction, AbdominalWounds, ChestWounds, BodyWounds;
	bool BurnEffects, ChemicalEffects, BlastTorsoWounds;
	float HeadWoundRadius, AbdomenWoundRadius, ChestWoundRadius, BodyWoundRadius;
	bool		Blood;						// sprays, splatter, pools
	bool		Dismemberment;				// heads and limbs come off killed soldiers
	bool		Gibs;							// flying limbs, heads, chunks and organs
	bool		CorpseDismemberment;	// allow further cuts on dead soldiers
	bool		Player;						// gore on the player's own soldier too
	float		HeadshotDecapChance;
	float		HeadshotDecapMinDamage;
	float		LimbCutChance;
	float		LimbCutMinDamage;
	float		ExplosionMinScale;		// explosion falloff needed to tear a body apart
	float		ExplosionBurstScale;		// ... and to blow most of it apart
	int		MaxGibs;
	float		GibLifetime;
	float		StumpBleedTime;
	float		BloodAmount;				// multiplies particle counts
	char		NoGoreWarheads[256];		// comma list; warhead names containing any of these skip gore
};

static GoreSettingsStruct	_Settings;
static bool						_Initialized = false;

#define GORE_INI_NAME		"gore.ini"

/*
** Asset names written by Code/Tools/GoreAssets/convert_sof2_gore.py (all optional)
*/
#define GORE_TEX_SPRAY		"g_blood_spray.tga"
#define GORE_TEX_MIST		"g_blood_mist.tga"
static const char * _SplatTextures[] = { "g_splat1.tga", "g_splat2.tga", "g_splat3.tga", "g_wall_splat.tga" };
static const char * _PoolTextures[]  = { "g_pool1.tga", "g_pool2.tga" };
static const char * _ChunkModels[]   = { "G_CHUNK_SM", "G_CHUNK_SM", "G_CHUNK_LG", "G_CHUNK_GIB_LG" };	// small ones twice as often
static const char * _OrganModels[]   = { "G_GUTS", "G_LUNG", "G_STOMACH", "G_RIBS" };
#define GORE_BRAIN_MODEL	"G_BRAIN"
#define GORE_BONE_MODEL		"G_BONE"

#define ARRAY_COUNT(a)		(sizeof(a) / sizeof((a)[0]))

/*
** Where a soldier can be cut. Bone names are Renegade's human skeleton (Character Studio
** biped, "C ..."), hit boxes are the K_ boxes HumanStateClass::Get_Ouch_Type knows.
** The cap is placed on the cut bone's pivot; its +X points along the bone, toward where
** the cut part was (biped convention: a bone's X runs along the bone).
*/
struct GoreCutStruct
{
	const char *	CutBone;
	const char *	Cap;				// body side; the gib side uses Cap + "_R"
	bool				IsHead;
};

static const GoreCutStruct _Cuts[] = {
	{ "C HEAD",				"G_CAP_NECK",		true },
	{ "C L UPPERARM",		"G_CAP_SHLDR",		false },
	{ "C R UPPERARM",		"G_CAP_SHLDR",		false },
	{ "C L FOREARM",		"G_CAP_ELBOW",		false },
	{ "C R FOREARM",		"G_CAP_ELBOW",		false },
	{ "C L THIGH",			"G_CAP_HIP",		false },
	{ "C R THIGH",			"G_CAP_HIP",		false },
	{ "C L CALF",			"G_CAP_KNEE",		false },
	{ "C R CALF",			"G_CAP_KNEE",		false },
};

enum { CUT_HEAD = 0, CUT_NONE = -1 };

struct GoreHitBoxStruct
{
	const char *	HitBox;
	const char *	Bone;				// where blood comes from
	int				Cut;				// index into _Cuts for a killing shot here, or CUT_NONE
};

static const GoreHitBoxStruct _HitBoxes[] = {
	{ "K_HEAD",			"C HEAD",			0 },
	{ "K_NECK",			"C NECK",			0 },
	{ "K_CHEST",		"C SPINE2",			CUT_NONE },
	{ "K_ABDOMEN",		"C SPINE1",			CUT_NONE },
	{ "K_PELVIS",		"C PELVIS",			CUT_NONE },
	{ "K_L UPPERARM",	"C L UPPERARM",	1 },
	{ "K_R UPPERARM",	"C R UPPERARM",	2 },
	{ "K_L FOREARM",	"C L FOREARM",		3 },
	{ "K_L HAND",		"C L HAND",			3 },
	{ "K_R FOREARM",	"C R FOREARM",		4 },
	{ "K_R HAND",		"C R HAND",			4 },
	{ "K_L THIGH",		"C L THIGH",		5 },
	{ "K_R THIGH",		"C R THIGH",		6 },
	{ "K_L CALF",		"C L CALF",			7 },
	{ "K_L FOOT",		"C L FOOT",			7 },
	{ "K_R CALF",		"C R CALF",			8 },
	{ "K_R FOOT",		"C R FOOT",			8 },
};

/*
** Flying pieces: a body part cut from a copy of the soldier's model, or a gore model.
** world tm = T(Pivot + Offset) * Rotation * T(-Pivot) * BaseTm
*/
struct GoreGibStruct
{
	RenderObjClass *	Model;
	PhysClass *			Wrapper;			// the scene's node for Model, when we added it to the scene
	Matrix3D				BaseTm;
	Vector3				Pivot;
	Vector3				Offset;
	Quaternion			Rotation;
	Vector3				Velocity;
	Vector3				AngularVelocity;
	float					Radius;
	float					Age;
	bool					Resting;
	int					Bounces;
};

#define MAX_GORE_GIBS		96
static GoreGibStruct		_Gibs[MAX_GORE_GIBS];
static int					_GibCount = 0;

static void Remove_Gib(int i)
{
	GoreGibStruct & g = _Gibs[i];
	g.Model->Remove();
	g.Model->Release_Ref();
	if (g.Wrapper != NULL) {
		g.Wrapper->Release_Ref();
	}
	memmove(&_Gibs[i], &_Gibs[i + 1], sizeof(GoreGibStruct) * (_GibCount - i - 1));
	_GibCount--;
}

static bool Is_Finite(const Matrix3D & tm)
{
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 4; c++) {
			if (!_finite(tm[r][c]) || fabs(tm[r][c]) > 100000.0f) {
				return false;
			}
		}
	}
	return true;
}

/*
** Put a gore render object in the world. The scene wraps it in a node that by default
** collides with everything, so a flying limb (a whole copy of the soldier model, hit boxes
** included) would block soldiers, sight lines and falling parachuters. Gore is only for show:
** nothing collides with it. Returns the node (not ref'd).
*/
static PhysClass * Add_To_Scene(RenderObjClass * obj)
{
	PhysicsSceneClass::Get_Instance()->Add_Render_Object(obj);
	PhysClass * node = (PhysClass *)obj->Get_User_Data();
	if (node != NULL && node->As_RenderObjPhysClass() != NULL) {
		node->Set_Collision_Group(UNCOLLIDEABLE_GROUP);
		return node;
	}
	return NULL;
}

// Voxel pieces get render-only physics wrappers: no collision or save state.
static void Add_Voxel_Piece(RenderObjClass *obj)
{
    PhysClass *node=Add_To_Scene(obj);
    if(node)node->Enable_Dont_Save(true);
}
static void Move_Voxel_Piece(RenderObjClass *obj,const Matrix3D &tm)
{
    PhysClass *node=(PhysClass*)obj->Get_User_Data();
    if(node && node->As_RenderObjPhysClass())node->Set_Transform(tm);
    else obj->Set_Transform(tm);
}

/*
** Timed things: emitters to stop and then remove, and blood pools to drop later.
*/
struct GoreTimedStruct
{
	RenderObjClass *	Emitter;			// NULL for a pool
	float					StopTime;
	float					RemoveTime;
	Vector3				Position;		// pools
	float					Size;
};

#define MAX_GORE_TIMED		96
static GoreTimedStruct	_Timed[MAX_GORE_TIMED];
static int					_TimedCount = 0;
static float				_Time = 0.0f;

static bool					_ReportedMissing = false;

// Developer test mode (GORETEST on the command line): every few seconds the nearest living
// soldier is put in front of the player and killed with a head shot, an arm shot, a leg shot
// or a point-blank explosion in turn, so every gore path can be seen without playing.
static bool					_GoreTest = false;
static bool _CorpseTest = false;
static bool _WoundTest=false;
static bool _AimTest=false;
static int _AimVictim=0, _AimCase=-1, _AimHits=0, _AimRoundStart=0, _AimPass=0, _AimFail=0;
static DWORD _AimCaseTick=0;
static Vector3 _AimSide(0,1,0);
static char _AimBox[128];

static int _WoundTestId=0, _WoundControlId=0, _WoundTestStage=-1;
static DWORD _WoundStageTick=0;
static RenderObjClass *_WoundCamera=NULL;
static RenderObjClass *_GeometryPreview=NULL;
static PhysClass *_GeometryWrapper=NULL;
static int _GeometryPresetId=0;
static char _GeometryPreviewName[128]="";
static Vector3 _WoundForward(1,0,0);

static float				_GoreTestTimer = 0.0f;
static int					_GoreTestStep = 0;
static SoldierGameObj *	_GoreTestVictim = NULL;	// spawned, killed on the next tick
static FILE *				_GoreTestLog = NULL;

// FIRETEST: like GORETEST, but the player's own weapon does the killing (real bullets, real
// hit boxes, real gore odds from gore.ini), to exercise the normal kill path.
static bool					_FireTest = false;
static int					_FireTestVictimId = 0;
static bool					_FireTestTrigger = false;
static int					_FireTestKills = 0;

static void Gore_Test_Log(const char * format, ...)
{
	if (_GoreTestLog == NULL) {
		return;
	}
	va_list args;
	va_start(args, format);
	vfprintf(_GoreTestLog, format, args);
	va_end(args);
	fflush(_GoreTestLog);
}


/*
** Helpers
*/
static void Get_Exe_Relative_Path(const char * file_name, char * buffer, int buffer_size)
{
	buffer[0] = 0;
	if (::GetModuleFileNameA(NULL, buffer, buffer_size) != 0) {
		char * slash = strrchr(buffer, '\\');
		if (slash != NULL) {
			slash[1] = 0;
		} else {
			buffer[0] = 0;
		}
	}
	strncat(buffer, file_name, buffer_size - strlen(buffer) - 1);
}

static float Ini_Float(const char * key, float def, const char * path)
{
	char def_str[32];
	char value[32];
	sprintf(def_str, "%g", def);
	::GetPrivateProfileStringA("Gore", key, def_str, value, sizeof(value), path);
	return (float)atof(value);
}

static bool Ini_Bool(const char * key, bool def, const char * path)
{
	return ::GetPrivateProfileIntA("Gore", key, def ? 1 : 0, path) != 0;
}

static RenderObjClass * Create_Gore_Model(const char * name)
{
	RenderObjClass * obj = WW3DAssetManager::Get_Instance()->Create_Render_Obj(name);
	if (obj == NULL && !_ReportedMissing) {
		_ReportedMissing = true;
		WWDEBUG_SAY(("Gore: model %s not found (run Code/Tools/GoreAssets/convert_sof2_gore.py); continuing without it\n", name));
	}
	return obj;
}

static Vector3 Random_Unit_Vector(void)
{
	Vector3 v(FreeRandom.Get_Float(-1.0f, 1.0f), FreeRandom.Get_Float(-1.0f, 1.0f), FreeRandom.Get_Float(-1.0f, 1.0f));
	if (v.Length2() < 0.0001f) {
		v.Set(0.0f, 0.0f, 1.0f);
	}
	v.Normalize();
	return v;
}

static bool Is_Skin(RenderObjClass * obj)
{
	if (obj->Class_ID() != RenderObjClass::CLASSID_MESH) {
		return false;
	}
	MeshModelClass * model = ((MeshClass *)obj)->Peek_Model();
	return (model != NULL) && (model->Get_Flag(MeshGeometryClass::SKIN) != 0);
}

/*
** Ray against static terrain only. Returns true and the hit point / normal on a hit.
*/
static bool Cast_Terrain_Ray(const Vector3 & start, const Vector3 & end, Vector3 * hit, Vector3 * normal)
{
	CastResultStruct res;
	LineSegClass ray(start, end);
	PhysRayCollisionTestClass raytest(ray, &res, TERRAIN_ONLY_COLLISION_GROUP, COLLISION_TYPE_PROJECTILE);
	raytest.CheckDynamicObjs = false;
	PhysicsSceneClass::Get_Instance()->Cast_Ray(raytest);
	if (res.StartBad || res.Fraction >= 1.0f) {
		return false;
	}
	*hit = start + (end - start) * res.Fraction;
	*normal = res.Normal;
	return true;
}

static void Blood_Decal(const Vector3 & point, const Vector3 & normal, const char * texture, float size)
{
	Matrix3D tm(true);
	tm.Look_At(point, point - normal, FreeRandom.Get_Float(0.0f, DEG_TO_RADF(360.0f)));
	PhysicsSceneClass::Get_Instance()->Create_Decal(tm, texture, size);
}

static void Splatter_Behind(const Vector3 & from, const Vector3 & dir, float range, float size)
{
	if (!_Settings.Blood || dir.Length2() < 0.0001f) {
		return;
	}
	Vector3 d = dir;
	d.Normalize();
	// a little scatter, and a slight fall, so it doesn't land as a perfect circle on the wall
	d += 0.25f * Random_Unit_Vector();
	d.Z -= 0.2f;
	d.Normalize();

	Vector3 hit, normal;
	if (Cast_Terrain_Ray(from, from + range * d, &hit, &normal)) {
		Blood_Decal(hit, normal, _SplatTextures[FreeRandom.Get_Int(ARRAY_COUNT(_SplatTextures))], size);
	}
}

static void Add_Timed(RenderObjClass * emitter, float stop_after, float remove_after, const Vector3 & pos, float size)
{
	if (_TimedCount >= MAX_GORE_TIMED) {
		if (emitter != NULL) {
			emitter->Remove();			// out of slots: drop it now rather than leak it
		}
		return;
	}
	GoreTimedStruct & t = _Timed[_TimedCount++];
	t.Emitter = emitter;
	if (emitter != NULL) {
		emitter->Add_Ref();
	}
	t.StopTime = _Time + stop_after;
	t.RemoveTime = _Time + remove_after;
	t.Position = pos;
	t.Size = size;
}

/*
** A blood emitter built in code (W3D emitters normally come from .w3d files).
** velocity is in the emitter's own frame.
*/
static ParticleEmitterClass * Create_Blood_Emitter(	float rate, int burst, const Vector3 & velocity, float velocity_random,
																	float particle_size, float particle_life, const char * texture_name )
{
	TextureClass * tex = WW3DAssetManager::Get_Instance()->Get_Texture(texture_name);
	if (tex == NULL) {
		return NULL;
	}

	ParticlePropertyStruct<Vector3> color;
	color.Start.Set(0.45f, 0.02f, 0.01f);
	color.Rand.Set(0.12f, 0.0f, 0.0f);
	color.NumKeyFrames = 0;
	color.KeyTimes = NULL;
	color.Values = NULL;

	ParticlePropertyStruct<float> opacity;
	opacity.Start = 0.9f;
	opacity.Rand = 0.1f;
	opacity.NumKeyFrames = 0;
	opacity.KeyTimes = NULL;
	opacity.Values = NULL;

	ParticlePropertyStruct<float> size;
	size.Start = particle_size;
	size.Rand = particle_size * 0.5f;
	size.NumKeyFrames = 0;
	size.KeyTimes = NULL;
	size.Values = NULL;

	ParticlePropertyStruct<float> zero;
	zero.Start = 0.0f;
	zero.Rand = 0.0f;
	zero.NumKeyFrames = 0;
	zero.KeyTimes = NULL;
	zero.Values = NULL;

	int count = (int)(burst * _Settings.BloodAmount + 0.5f);
	if (count < 1) {
		count = 1;
	}

	ParticleEmitterClass * emitter = new ParticleEmitterClass(
		rate, count,
		new Vector3SolidSphereRandomizer(0.03f),					// emitter owns the randomizers
		velocity,
		new Vector3SolidSphereRandomizer(velocity_random),
		0.0f, 0.0f,
		color, opacity, size, zero, 3.14f, zero, zero,
		Vector3(0.0f, 0.0f, -9.8f),
		particle_life,
		tex,
		ShaderClass::_PresetAlphaSpriteShader);
	tex->Release_Ref();
	return emitter;
}

/*
** A short burst of blood in the world, along dir.
*/
static void Spray_Blood(const Vector3 & pos, const Vector3 & dir, float speed, int count, float duration)
{
	if (!_Settings.Blood) {
		return;
	}
	// 'count' particles in total: emit 'count' bursts of one over 'duration' seconds
	float rate = (duration > 0.01f) ? (float)count / duration : 60.0f;
	ParticleEmitterClass * emitter = Create_Blood_Emitter(rate, 1, Vector3(speed, 0.0f, 0.0f), speed * 0.35f,
		0.035f, 0.6f, GORE_TEX_SPRAY);
	if (emitter == NULL) {
		return;
	}

	Vector3 d = dir;
	if (d.Length2() < 0.0001f) {
		d.Set(0.0f, 0.0f, 1.0f);
	}
	d.Normalize();

	// The emitter's +X is the spray direction
	Matrix3D tm(true);
	tm.Obj_Look_At(pos, pos + d, 0.0f);
	emitter->Set_Transform(tm);
	emitter->Start();
	Add_To_Scene(emitter);
	Add_Timed(emitter, duration, duration + 1.0f, pos, 0.0f);
	emitter->Release_Ref();
}

/*
** Bones in the subtree of root (root included).
*/
static void Get_Subtree(const HTreeClass * tree, int root, bool * in_subtree)
{
	int count = tree->Num_Pivots();
	for (int i = 0; i < count; i++) {
		in_subtree[i] = false;
		for (int b = i; b > 0; b = tree->Get_Parent_Index(b)) {
			if (b == root) {
				in_subtree[i] = true;
				break;
			}
		}
	}
}

/*
** Hide the skin of every bone where hide[bone] is true, and the rigid parts (heads, helmets,
** gear) attached to those bones.
*/
static void Hide_Bones(RenderObjClass * model, const bool * hide, int bone_count, int collapse_to)
{
	for (int bone = 1; bone < bone_count; bone++) {
		if (!hide[bone]) {
			continue;
		}
		model->Set_Bone_Skin_Hidden(bone, true, collapse_to);

		int subs = model->Get_Num_Sub_Objects_On_Bone(bone);
		for (int s = 0; s < subs; s++) {
			RenderObjClass * sub = model->Get_Sub_Object_On_Bone(s, bone);
			if (sub != NULL) {
				if (!Is_Skin(sub)) {
					sub->Set_Hidden(true);
				}
				sub->Release_Ref();
			}
		}
	}
}

static void Add_Gib(RenderObjClass * model, const Vector3 & pivot, const Vector3 & velocity, float spin, float radius)
{
	if (!_Settings.Gibs || model == NULL) {
		return;
	}
	if (!Is_Finite(model->Get_Transform()) || !_finite(pivot.X) || !_finite(pivot.Y) || !_finite(pivot.Z) ||
			!_finite(velocity.X) || !_finite(velocity.Y) || !_finite(velocity.Z)) {
		return;
	}
	if (model->Peek_Scene() != NULL) {
		return;		// already in the world: not ours to move
	}

	// Full: the oldest one goes
	int max_gibs = (_Settings.MaxGibs < MAX_GORE_GIBS) ? _Settings.MaxGibs : MAX_GORE_GIBS;
	if (max_gibs < 1) {
		return;
	}
	if (_GibCount >= max_gibs) {
		Remove_Gib(0);
	}

	GoreGibStruct & g = _Gibs[_GibCount++];
	g.Model = model;
	g.Model->Add_Ref();
	g.BaseTm = model->Get_Transform();
	g.Pivot = pivot;
	g.Offset.Set(0.0f, 0.0f, 0.0f);
	g.Rotation.Make_Identity();
	g.Velocity = velocity;
	g.AngularVelocity = spin * Random_Unit_Vector();
	g.Radius = radius;
	g.Age = 0.0f;
	g.Resting = false;
	g.Bounces = 0;

	g.Wrapper = Add_To_Scene(model);
	if (g.Wrapper != NULL) {
		g.Wrapper->Add_Ref();
	}
}

/*
** A gore model (chunk, organ) thrown out from pos.
*/
static void Throw_Piece(const char * name, const Vector3 & pos, const Vector3 & velocity)
{
	if (!_Settings.Gibs) {
		return;
	}
	RenderObjClass * piece = Create_Gore_Model(name);
	if (piece == NULL) {
		return;
	}
	Matrix3D tm(true);
	tm.Set(Random_Unit_Vector(), FreeRandom.Get_Float(0.0f, DEG_TO_RADF(360.0f)));
	tm.Set_Translation(pos);
	piece->Set_Transform(tm);
	Add_Gib(piece, pos, velocity, FreeRandom.Get_Float(4.0f, 12.0f), 0.08f);
	piece->Release_Ref();
}

/*
** Attach a cap model and a pumping blood fountain to a bone of model.
*/
static void Dress_Stump(RenderObjClass * model, int bone, const char * cap_name, bool bleed)
{
	if (cap_name != NULL) {
		RenderObjClass * cap = Create_Gore_Model(cap_name);
		if (cap != NULL) {
			model->Add_Sub_Object_To_Bone(cap, bone);
			cap->Release_Ref();
		}
	}

	if (bleed && _Settings.Blood && _Settings.StumpBleedTime > 0.0f) {
		ParticleEmitterClass * fountain = Create_Blood_Emitter(14.0f, 1, Vector3(1.6f, 0.0f, 0.0f), 0.35f,
			0.03f, 0.7f, GORE_TEX_SPRAY);
		if (fountain != NULL) {
			model->Add_Sub_Object_To_Bone(fountain, bone);
			fountain->Start();
			Add_Timed(fountain, _Settings.StumpBleedTime, _Settings.StumpBleedTime + 1.0f, Vector3(0, 0, 0), 0.0f);
			fountain->Release_Ref();
		}
	}
}

/*
** Cut _Cuts[cut] off a soldier's model. push is the velocity given to the piece.
*/
static void Dismember(RenderObjClass * model, int cut, const Vector3 & push)
{
	const HTreeClass * tree = model->Get_HTree();
	if (tree == NULL) {
		return;
	}
	int bone = tree->Get_Bone_Index(_Cuts[cut].CutBone);
	if (bone <= 0 || model->Is_Bone_Skin_Hidden(bone)) {
		return;		// skeleton without this bone, or already cut
	}

	int bone_count = tree->Num_Pivots();
	bool * in_cut = new bool[bone_count];
	bool * not_in_cut = new bool[bone_count];
	Get_Subtree(tree, bone, in_cut);
	for (int i = 0; i < bone_count; i++) {
		// A later upper-arm/thigh cut must not recreate an already missing hand/calf.
		not_in_cut[i] = !in_cut[i] || model->Is_Bone_Skin_Hidden(i);
	}

	Matrix3D cut_tm = model->Get_Bone_Transform(bone);
	Vector3 cut_pos = cut_tm.Get_Translation();

	/*
	** The flying part: a copy of the model, frozen in the same pose, with only the cut part
	*/
	if (_Settings.Gibs) {
		RenderObjClass * piece = WW3DAssetManager::Get_Instance()->Create_Render_Obj(model->Get_Name());
		bool pose_ok = Is_Finite(model->Get_Transform());
		for (int b = 1; pose_ok && b < bone_count; b++) {
			pose_ok = Is_Finite(model->Get_Bone_Transform(b));
		}
		if (pose_ok && piece != NULL && piece->Get_HTree() != NULL && piece->Get_HTree()->Num_Pivots() == bone_count) {
			Matrix3D root_inv;
			model->Get_Transform().Get_Orthogonal_Inverse(root_inv);
			for (int b = 1; b < bone_count; b++) {
				Matrix3D rel;
				Matrix3D::Multiply(root_inv, model->Get_Bone_Transform(b), &rel);
				piece->Capture_Bone(b);
				piece->Control_Bone_Root_Relative(b, rel);
			}
			piece->Set_Transform(model->Get_Transform());
			Hide_Bones(piece, not_in_cut, bone_count, bone);

			StringClass cap_r(_Cuts[cut].Cap, true);
			cap_r += "_R";
			Dress_Stump(piece, bone, cap_r, false);

			Vector3 velocity = push + Vector3(0.0f, 0.0f, FreeRandom.Get_Float(1.5f, 3.5f));
			Add_Gib(piece, cut_pos, velocity, FreeRandom.Get_Float(3.0f, 9.0f), _Cuts[cut].IsHead ? 0.11f : 0.07f);
		}
		if (piece != NULL) {
			piece->Release_Ref();
		}
	}

	/*
	** The body: hide the part, cap the stump, make it pump
	*/
	Hide_Bones(model, in_cut, bone_count, bone);
	Dress_Stump(model, bone, _Cuts[cut].Cap, true);

	Vector3 out = cut_tm.Get_X_Vector();		// along the bone, toward where the part was
	Spray_Blood(cut_pos, out, 3.0f, 18, 0.35f);
	Splatter_Behind(cut_pos, push, 3.0f, FreeRandom.Get_Float(0.5f, 0.9f));

	if (_Cuts[cut].IsHead) {
		Throw_Piece(GORE_BRAIN_MODEL, cut_pos, push * 0.5f + Vector3(0.0f, 0.0f, 2.5f));
	} else if (FreeRandom.Get_Float() < 0.5f) {
		Throw_Piece(GORE_BONE_MODEL, cut_pos, push * 0.4f + Vector3(0.0f, 0.0f, 2.0f));
	}

	delete [] in_cut;
	delete [] not_in_cut;
}

static bool Warhead_Skips_Gore(const OffenseObjectClass & damager)
{
	const char * name = ArmorWarheadManager::Get_Warhead_Name(damager.Get_Warhead());
	if (name == NULL) {
		return false;
	}
	char upper_name[64];
	strncpy(upper_name, name, sizeof(upper_name) - 1);
	upper_name[sizeof(upper_name) - 1] = 0;
	strupr(upper_name);

	char list[256];
	strcpy(list, _Settings.NoGoreWarheads);
	strupr(list);
	for (char * token = strtok(list, ", "); token != NULL; token = strtok(NULL, ", ")) {
		if (strstr(upper_name, token) != NULL) {
			return true;
		}
	}
	return false;
}

enum SurfaceDamageType { SURFACE_DAMAGE_NONE, SURFACE_DAMAGE_BURN, SURFACE_DAMAGE_CHEM };

// A fire *status* is also assigned to rockets, C4 and lasers in stock armor.ini.
// Classify direct hits by warhead first; consult the status only for its None-warhead ticks.
static SurfaceDamageType Surface_Damage_Type(const OffenseObjectClass &offense,
    SoldierGameObj *soldier, const Vector3 &direction, const char *box)
{
    const char *name=ArmorWarheadManager::Get_Warhead_Name(offense.Get_Warhead());
    char upper[80];lstrcpynA(upper,name?name:"",sizeof(upper));strupr(upper);
    if(strstr(upper,"FLAME") || !strcmp(upper,"FIRE")) return SURFACE_DAMAGE_BURN;
    ArmorWarheadManager::SpecialDamageType type=ArmorWarheadManager::Get_Special_Damage_Type(offense.Get_Warhead());
    if(strstr(upper,"CHEM") || type==ArmorWarheadManager::SPECIAL_DAMAGE_TYPE_CHEM ||
        type==ArmorWarheadManager::SPECIAL_DAMAGE_TYPE_CNC_CHEM) return SURFACE_DAMAGE_CHEM;
    if(soldier && (!name || !*name || !stricmp(name,"None")) && !box && direction.Length2()<WWMATH_EPSILON) {
        type=soldier->Get_Special_Damage_Mode();
        if(type==ArmorWarheadManager::SPECIAL_DAMAGE_TYPE_FIRE || type==ArmorWarheadManager::SPECIAL_DAMAGE_TYPE_CNC_FIRE ||
            type==ArmorWarheadManager::SPECIAL_DAMAGE_TYPE_SUPER_FIRE) return SURFACE_DAMAGE_BURN;
        if(type==ArmorWarheadManager::SPECIAL_DAMAGE_TYPE_CHEM || type==ArmorWarheadManager::SPECIAL_DAMAGE_TYPE_CNC_CHEM)
            return SURFACE_DAMAGE_CHEM;
    }
    return SURFACE_DAMAGE_NONE;
}

static const GoreHitBoxStruct * Find_Hit_Box(const char * collision_box_name)
{
	if (collision_box_name == NULL) {
		return NULL;
	}
	const char * base = strchr(collision_box_name, '.');
	base = (base != NULL) ? base + 1 : collision_box_name;
	for (int i = 0; i < ARRAY_COUNT(_HitBoxes); i++) {
		if (stricmp(_HitBoxes[i].HitBox, base) == 0) {
			return &_HitBoxes[i];
		}
	}
	return NULL;
}

// Stock S_A_HUMAN has C SPINE and C SPINE1, but no C SPINE2.
// Preserve longer custom skeletons while resolving legacy chest references.
static int Gore_Bone_Index(RenderObjClass *model,const char *name)
{
    if(!model || !name)return 0;
    int bone=model->Get_Bone_Index(name);
    if(bone<=0 && !stricmp(name,"C SPINE2"))bone=model->Get_Bone_Index("C SPINE1");
    return bone;
}

static Vector3 Bone_Position(RenderObjClass * model, const char * bone_name)
{
	const HTreeClass * tree = model->Get_HTree();
	if (tree != NULL) {
		int bone = Gore_Bone_Index(model,bone_name);
		if (bone > 0) {
			return model->Get_Bone_Transform(bone).Get_Translation();
		}
	}
	return model->Get_Transform().Get_Translation() + Vector3(0.0f, 0.0f, 1.0f);
}

/*
** A body torn apart by an explosion: limbs and maybe the head, chunks and organs.
*/
static bool Carve_Model_Through_Wound(RenderObjClass *model,const Vector3 &point,const Vector3 &direction,float radius,TextureClass *tissue,bool head_only=false)
{
    if(!model || model->Is_Hidden() || (model->Get_Name() && !strnicmp(model->Get_Name(),"G_",2)))return false;
    if(model->Class_ID()==RenderObjClass::CLASSID_MESH) {
        MeshClass *tube=((MeshClass*)model)->Carve_Through_Wound(point,direction,radius,tissue,head_only);
        if(tube){tube->Release_Ref();return true;}
        return false;
    }
    if(model->Class_ID()==RenderObjClass::CLASSID_HLOD) {
        HLodClass *lod=(HLodClass*)model;int level=lod->Get_Lod_Count()-1;
        if(level<0)return false;
        for(int i=0;i<lod->Get_Lod_Model_Count(level);++i)if(Carve_Model_Through_Wound(lod->Peek_Lod_Model(level,i),point,direction,radius,tissue,head_only))return true;
        for(int i=0;i<lod->Get_Additional_Model_Count();++i)if(Carve_Model_Through_Wound(lod->Peek_Additional_Model(i),point,direction,radius,tissue,head_only))return true;
        return false;
    }
    int count=model->Get_Num_Sub_Objects();
    for(int i=0;i<count;++i){RenderObjClass *sub=model->Get_Sub_Object(i);bool done=Carve_Model_Through_Wound(sub,point,direction,radius,tissue,head_only);if(sub)sub->Release_Ref();if(done)return true;}
    return false;
}

static void Blast_Head_Wound(RenderObjClass *model, const Vector3 &incoming, float scale);
static void Blast_Torso_Wounds(SoldierGameObj *soldier, const Vector3 &incoming, float scale);
static void Blast_Scorch(SoldierGameObj *soldier, const Vector3 &incoming, float scale);

static void Blow_Apart(SoldierGameObj *soldier, const Vector3 & blast_dir, float explosion_scale)
{
	RenderObjClass *model=soldier->Peek_Model();
	Vector3 center = Bone_Position(model, "C SPINE1");
	Vector3 away = blast_dir;
	if (away.Length2() < 0.0001f) {
		away = Random_Unit_Vector();
	}
	away.Z = 0.0f;
	if (away.Length2() < 0.0001f) {
		away.Set(1.0f, 0.0f, 0.0f);
	}
	away.Normalize();

	bool burst = explosion_scale >= _Settings.ExplosionBurstScale;
	// Select distinct limb roots. Never hide spine/pelvis or replace the whole corpse.
	const int limb_roots[]={1,2,5,6};
	bool selected[4]={false,false,false,false};
	int max_cuts = burst ? 3 : 2;
	int cuts = 0;

	if (_Settings.Dismemberment) {
		for(int n=0;n<max_cuts;++n) {
			int best=-1;float best_score=-FLT_MAX;
			for(int j=0;j<4;++j) {
				int bone=model->Get_Bone_Index(_Cuts[limb_roots[j]].CutBone);
				if(selected[j] || bone<=0 || model->Is_Bone_Skin_Hidden(bone)) continue;
				Vector3 part=Bone_Position(model,_Cuts[limb_roots[j]].CutBone)-center;
				float score=-Vector3::Dot_Product(part,away)+FreeRandom.Get_Float(0.0f,0.25f);
				if(score>best_score) {best=j;best_score=score;}
			}
			if(best<0) break;
			selected[best]=true;
			Dismember(model,limb_roots[best],FreeRandom.Get_Float(4.0f,9.0f)*explosion_scale*away+1.5f*Random_Unit_Vector());
			++cuts;
		}
	}
	if(_Settings.HeadDestruction && burst) Blast_Head_Wound(model,away,explosion_scale);
	if(_Settings.BlastTorsoWounds) Blast_Torso_Wounds(soldier,away,explosion_scale);
	if(_Settings.BurnEffects) Blast_Scorch(soldier,away,explosion_scale);
	Gore_Test_Log("blast: limbs %d, falloff %.2f, spine hidden %d, pelvis hidden %d\n",cuts,explosion_scale,
		model->Is_Bone_Skin_Hidden(model->Get_Bone_Index("C SPINE1")),model->Is_Bone_Skin_Hidden(model->Get_Bone_Index("C PELVIS")));

	// Guts and chunks
	int chunks = burst ? 5 : 2;
	for (int c = 0; c < chunks; c++) {
		Vector3 v = FreeRandom.Get_Float(3.0f, 8.0f) * explosion_scale * away + 2.5f * Random_Unit_Vector() + Vector3(0.0f, 0.0f, 3.0f);
		Throw_Piece(_ChunkModels[FreeRandom.Get_Int(ARRAY_COUNT(_ChunkModels))], center, v);
	}
	if (burst || FreeRandom.Get_Float() < 0.5f) {
		int organs = burst ? 3 : 1;
		for (int o = 0; o < organs; o++) {
			Vector3 v = FreeRandom.Get_Float(2.0f, 6.0f) * explosion_scale * away + 1.5f * Random_Unit_Vector() + Vector3(0.0f, 0.0f, 2.5f);
			Throw_Piece(_OrganModels[FreeRandom.Get_Int(ARRAY_COUNT(_OrganModels))], center, v);
		}
	}

	Spray_Blood(center, Vector3(0.0f, 0.0f, 1.0f), 4.0f, 40, 0.4f);
	for (int s = 0; s < (burst ? 4 : 2); s++) {
		Splatter_Behind(center, away + 0.6f * Random_Unit_Vector(), 4.0f, FreeRandom.Get_Float(0.6f, 1.2f));
	}
}


/*
** GoreManager
*/
#include "voxel_skin.h"
void GoreManager::Warm_Level_Skins()
{
    if (!IS_MISSION || !VoxelSkin::Is_Enabled()) return;

    std::set<std::string> seen;
    char name[_MAX_FNAME];
    for (auto *node=GameObjManager::Get_Smart_Game_Obj_List()->Head();node;node=node->Next()) {
        SoldierGameObj *soldier=node->Data()->As_SoldierGameObj();
        if (!soldier || soldier==COMBAT_STAR || !soldier->Peek_Model()) continue;
        RenderObjClass *actor=soldier->Peek_Model();
        lstrcpynA(name,actor->Get_Name(),sizeof(name));
        if (seen.insert(_strlwr(name)).second && !VoxelSkin::Prewarm(actor)) return;
    }

    // Some soldiers are spawned by scripts. Warm their models if the level's
    // normal preload already brought them in, without loading unrelated assets.
    auto *assets=WW3DAssetManager::Get_Instance();
    for (auto *definition=DefinitionMgrClass::Get_First(CLASSID_GAME_OBJECT_DEF_SOLDIER); definition;
         definition=DefinitionMgrClass::Get_Next(definition,CLASSID_GAME_OBJECT_DEF_SOLDIER)) {
        const auto *soldier=static_cast<const SoldierGameObjDef*>(definition);
        auto *physical=static_cast<PhysDefClass*>(DefinitionMgrClass::Find_Definition(soldier->Get_Phys_Def_ID()));
        if (!physical) continue;
        const StringClass &configured=physical->Get_Model_Name();
        const char *extension=strrchr(configured,'.');
        if (extension && !_stricmp(extension,".w3d"))
            _splitpath_s(configured,NULL,0,NULL,0,name,sizeof(name),NULL,0);
        else
            lstrcpynA(name,configured,sizeof(name));
        if (!*name || !assets->Render_Obj_Exists(name) || !seen.insert(_strlwr(name)).second) continue;
        RenderObjClass *actor=assets->Create_Render_Obj(name);
        if (!actor) continue;
        const bool room=VoxelSkin::Prewarm(actor);
        actor->Release_Ref();
        if (!room) break;
    }
}

void GoreManager::Init( void )
{
	char path[MAX_PATH];
	Get_Exe_Relative_Path(GORE_INI_NAME, path, sizeof(path));

	_Settings.Enabled						= Ini_Bool("Enabled", true, path);
    VoxelSkin::Configure(_Settings.Enabled && Ini_Bool("VoxelGore",true,path),path);
    VoxelSkin::Set_Piece_Callbacks(Add_Voxel_Piece,Move_Voxel_Piece,Cast_Terrain_Ray);
	_Settings.Blood						= Ini_Bool("Blood", true, path);
	_Settings.Dismemberment				= Ini_Bool("Dismemberment", true, path);
	_Settings.BurnEffects=Ini_Bool("BurnEffects",true,path);
	_Settings.ChemicalEffects=Ini_Bool("ChemicalEffects",true,path);
	_Settings.BlastTorsoWounds=Ini_Bool("BlastTorsoWounds",true,path);
	_Settings.Gibs							= Ini_Bool("Gibs", true, path);
	_Settings.CorpseDismemberment = Ini_Bool("CorpseDismemberment", true, path);
	_Settings.HeadDestruction = Ini_Bool("HeadDestruction", true, path);
    _Settings.AbdominalWounds = Ini_Bool("AbdominalWounds", true, path);
    _Settings.ChestWounds = Ini_Bool("ChestWounds", true, path);
    _Settings.BodyWounds = Ini_Bool("BodyWounds", true, path);
    _Settings.HeadWoundRadius = WWMath::Clamp(Ini_Float("HeadWoundRadius", 0.018f, path), 0.010f, 0.025f);
    _Settings.AbdomenWoundRadius = WWMath::Clamp(Ini_Float("AbdomenWoundRadius", 0.026f, path), 0.012f, 0.045f);
    _Settings.ChestWoundRadius = WWMath::Clamp(Ini_Float("ChestWoundRadius", 0.024f, path), 0.012f, 0.040f);
    _Settings.BodyWoundRadius = WWMath::Clamp(Ini_Float("BodyWoundRadius", 0.045f, path), 0.01f, 0.12f);
    _Settings.Player						= Ini_Bool("Player", false, path);
	_Settings.HeadshotDecapChance		= Ini_Float("HeadshotDecapChance", 0.5f, path);
	_Settings.HeadshotDecapMinDamage	= Ini_Float("HeadshotDecapMinDamage", 0.0f, path);
	_Settings.LimbCutChance				= Ini_Float("LimbCutChance", 0.5f, path);
	_Settings.LimbCutMinDamage			= Ini_Float("LimbCutMinDamage", 0.0f, path);
	_Settings.ExplosionMinScale		= Ini_Float("ExplosionMinScale", 0.35f, path);
	_Settings.ExplosionBurstScale		= Ini_Float("ExplosionBurstScale", 0.8f, path);
	_Settings.MaxGibs						= ::GetPrivateProfileIntA("Gore", "MaxGibs", 48, path);
	_Settings.GibLifetime				= Ini_Float("GibLifetime", 45.0f, path);
	_Settings.StumpBleedTime			= Ini_Float("StumpBleedTime", 6.0f, path);
	_Settings.BloodAmount				= Ini_Float("BloodAmount", 1.0f, path);
	::GetPrivateProfileStringA("Gore", "NoGoreWarheads", "FIRE,CHEM,ELECTRIC,TIBERIUM,LASER",
		_Settings.NoGoreWarheads, sizeof(_Settings.NoGoreWarheads), path);

	char cmd[1024];
	strncpy(cmd, ::GetCommandLineA(), sizeof(cmd) - 1);
	cmd[sizeof(cmd) - 1] = 0;
	strupr(cmd);
	_FireTest = (strstr(cmd, "FIRETEST") != NULL);
	_CorpseTest = (strstr(cmd, "CORPSETEST") != NULL);
    _WoundTest = (strstr(cmd,"WOUNDTEST") != NULL);
    _AimTest = (strstr(cmd,"AIMTEST") != NULL);
	_GoreTest = (strstr(cmd, "GORETEST") != NULL) || _FireTest || _CorpseTest || _WoundTest || _AimTest;
	if (_FireTest) {
		char log_path[MAX_PATH];
		Get_Exe_Relative_Path("gore_test.log", log_path, sizeof(log_path));
		_GoreTestLog = fopen(log_path, "w");
	} else if (_GoreTest) {
		char log_path[MAX_PATH];
		Get_Exe_Relative_Path("gore_test.log", log_path, sizeof(log_path));
		_GoreTestLog = fopen(log_path, "w");
		// every kill in the test is a gory one
		_Settings.HeadshotDecapChance = 1.0f;
		_Settings.LimbCutChance = 1.0f;
		_Settings.HeadshotDecapMinDamage = 0.0f;
		_Settings.LimbCutMinDamage = 0.0f;
	}

	_Initialized = true;
	WWDEBUG_SAY(("Gore: %s%s\n", _Settings.Enabled ? "enabled" : "disabled", _GoreTest ? " (GORETEST)" : ""));
}

void GoreManager::Shutdown( void )
{
	Level_Unloaded();
    VoxelSkin::Shutdown();
	_Initialized = false;
}

static bool _OfflineBotGore=false;
void GoreManager::Set_Offline_Bot_Mode(bool enabled) {_OfflineBotGore=enabled;}

bool GoreManager::Is_Enabled( void )
{
	// Cosmetic only, and kept out of multiplayer, where clients don't see every hit
	return _Initialized && _Settings.Enabled && (IS_SOLOPLAY || (_OfflineBotGore && CombatManager::I_Am_Server()));
}

bool GoreManager::Corpse_Hits_Enabled( void )
{
	return Is_Enabled() && _Settings.CorpseDismemberment;
}

struct WoundBudget {
    int Id; const RenderObjClass *Model; int Craters; int Decals;
    float BurnDose, ChemDose, NextSurfaceTime, ChemMelt, ChemMeltTarget;
    float BurnActiveUntil, NextCorpseHeatTime;
    bool BurnCorpseSpreading;
    int BurnStage, ChemStage, SurfaceDecals, ChemSplats, Blasts;
    int VoxelBlastStage;
    bool ChemResidue;
    bool ChemLiquidCollision, ChemPoseFrozen;
    // Independent heat histories for each hit box and exposed hemisphere.
    // Direct hits are localized; an actively burning corpse spreads heat over time.
    float BurnRegionDose[40];
    Vector3 BurnLocalPoint[40],BurnLocalNormal[40];
    bool BurnAnchored[40],BurnDirty[40];
    int BurnRegionStage[40],BurnDecals[40],BurnDecalCount;
    int LiveBurnEmitters;
    bool DeathBurnEmitters;
};
static WoundBudget _WoundBudgets[128];
static unsigned _NextWoundBudget=0;
struct ThermalSaveRegion {uint32 Region,Anchored;float Dose;Vector3 Point,Normal;};
struct ThermalPendingLoad {SoldierGameObj *Actor;uint32 Flags;std::vector<ThermalSaveRegion> Regions;};
static std::vector<ThermalPendingLoad> _ThermalPendingLoads;

static float _MeltAccumulator=0;

bool GoreManager::Keep_Chemical_Corpse(SoldierGameObj *soldier)
{
    if(!Is_Enabled() || !_Settings.ChemicalEffects || !soldier) return false;
    for(unsigned i=0;i<ARRAY_COUNT(_WoundBudgets);++i)
        if(_WoundBudgets[i].Id==soldier->Get_ID() && _WoundBudgets[i].Model==soldier->Peek_Model() &&
            _WoundBudgets[i].ChemDose>0) return true;
    return false;
}

static void Remove_Thermal_Surface(RenderObjClass *model,const char *name=NULL);
static WoundBudget *Get_Wound_Budget(SoldierGameObj *soldier)
{
    RenderObjClass *model=soldier->Peek_Model();
    for(unsigned i=0;i<ARRAY_COUNT(_WoundBudgets);++i)
        if(_WoundBudgets[i].Id==soldier->Get_ID() && _WoundBudgets[i].Model==model) return &_WoundBudgets[i];
    WoundBudget *budget=&_WoundBudgets[_NextWoundBudget++ % ARRAY_COUNT(_WoundBudgets)];
    // Evict any owned thermal geometry together with its accounting record.
    // Live corpses can outnumber the normal 64-player actor population.
    if(budget->BurnStage){PhysicalGameObj *old=GameObjManager::Find_PhysicalGameObj(budget->Id);
        if(old && old->Peek_Model()==budget->Model)Remove_Thermal_Surface(old->Peek_Model());}
    memset(budget,0,sizeof(*budget));budget->Id=soldier->Get_ID();budget->Model=model;
    for(int region=0;region<ARRAY_COUNT(budget->BurnDecals);++region)budget->BurnDecals[region]=-1;
    return budget;
}

static void Clear_Geometry_Preview(void)
{
    if(_GeometryPresetId) {
        PhysicalGameObj *obj=GameObjManager::Find_PhysicalGameObj(_GeometryPresetId);
        if(obj) obj->Set_Delete_Pending();
    }
    if(_GeometryPreview) {
        if(!_GeometryPresetId) _GeometryPreview->Remove();
        _GeometryPreview->Release_Ref();_GeometryPreview=NULL;
    }
    if(_GeometryWrapper) {_GeometryWrapper->Release_Ref();_GeometryWrapper=NULL;}
    _GeometryPresetId=0;
    _GeometryPreviewName[0]=0;
}

void GoreManager::Level_Unloaded( void )
{
    VoxelSkin::Clear_Level();
    MeshClass::Release_Thermal_Textures();
    _ThermalPendingLoads.clear();
	memset(_WoundBudgets,0,sizeof(_WoundBudgets));
	_NextWoundBudget=0;
    _MeltAccumulator=0;
    if(_WoundCamera) {
        if(COMBAT_CAMERA) COMBAT_CAMERA->Set_Host_Model(NULL);
        _WoundCamera->Release_Ref();_WoundCamera=NULL;
    }
    Clear_Geometry_Preview();
    _WoundTestId=_WoundControlId=0;_WoundTestStage=-1;
    _AimVictim=0;_AimCase=-1;_AimPass=_AimFail=0;
	_GoreTestVictim = NULL;
	_FireTestVictimId = 0;
	_FireTestTrigger = false;
	int i;
	while (_GibCount > 0) {
		Remove_Gib(_GibCount - 1);
	}

	for (i = 0; i < _TimedCount; i++) {
		if (_Timed[i].Emitter != NULL) {
			_Timed[i].Emitter->Remove();
			_Timed[i].Emitter->Release_Ref();
		}
	}
	_TimedCount = 0;
}

static bool Find_Model_Wound_Surface(RenderObjClass *model, const Vector3 &start, const Vector3 &dir,
    float &distance, Vector3 &point, Vector3 &normal, bool intact_bones_only=false)
{
    if(!model || model->Is_Hidden()) return false;
    // Attached stump caps are already dressed; they must not steal a body contact.
    if(model->Get_Name() && !strnicmp(model->Get_Name(),"G_",2)) return false;
    bool found=false;
    if(model->Class_ID()==RenderObjClass::CLASSID_MESH) {
        found=((MeshClass *)model)->Find_Wound_Surface(start,dir,distance,point,normal,intact_bones_only);
        if(_WoundTest) Gore_Test_Log("surface %s: %d distance %.3f\n",model->Get_Name(),found,distance);
    }
    // Collision hitboxes are on the top detail mesh; other LOD silhouettes must
    // never steal the visible surface contact.
    if(model->Class_ID()==RenderObjClass::CLASSID_HLOD) {
        HLodClass *lod=(HLodClass *)model;
        int level=lod->Get_Lod_Count()-1;
        for(int i=0;i<lod->Get_Lod_Model_Count(level);++i)
            found=Find_Model_Wound_Surface(lod->Peek_Lod_Model(level,i),start,dir,distance,point,normal,intact_bones_only) || found;
        for(int i=0;i<lod->Get_Additional_Model_Count();++i)
            found=Find_Model_Wound_Surface(lod->Peek_Additional_Model(i),start,dir,distance,point,normal,intact_bones_only) || found;
        return found;
    }
    for(int i=0;i<model->Get_Num_Sub_Objects();++i) {
        RenderObjClass *sub=model->Get_Sub_Object(i);
        found=Find_Model_Wound_Surface(sub,start,dir,distance,point,normal,intact_bones_only) || found;
        if(sub) sub->Release_Ref();
    }
    return found;
}

static int Carve_Model_Wound(RenderObjClass *model, const Vector3 &point, const Vector3 &normal,
    float radius, float depth, TextureClass *tissue, bool intact_bones_only=false)
{
    if (!model || model->Is_Hidden()) return 0;
    if(model->Get_Name() && !strnicmp(model->Get_Name(),"G_",2)) return 0;
    int affected=0;
    if(model->Class_ID()==RenderObjClass::CLASSID_MESH) {
        affected=((MeshClass *)model)->Carve_Wound(point,normal,radius,depth,tissue,intact_bones_only);
        if(_WoundTest && affected) Gore_Test_Log("carved mesh %s: %d, polygons %d, vertices %d\n",model->Get_Name(),affected,((MeshClass *)model)->Peek_Model()->Get_Polygon_Count(),((MeshClass *)model)->Peek_Model()->Get_Vertex_Count());
    }
    for(int i=0;i<model->Get_Num_Sub_Objects();++i) {
        RenderObjClass *sub=model->Get_Sub_Object(i);
        affected+=Carve_Model_Wound(sub,point,normal,radius,depth,tissue,intact_bones_only);
        if(sub) sub->Release_Ref();
    }
    return affected;
}

static void Blast_Head_Wound(RenderObjClass *model, const Vector3 &incoming, float scale)
{
    int bone=model->Get_Bone_Index("C HEAD");
    if(bone<=0 || model->Is_Bone_Skin_Hidden(bone)) return;
    Vector3 point=Bone_Position(model,"C HEAD"), normal=-incoming;
    float distance=0.8f;
    if(!Find_Model_Wound_Surface(model,point-incoming*0.4f,incoming,distance,point,normal,true)) return;
    TextureClass *tex=WW3DAssetManager::Get_Instance()->Get_Texture("g_tissue.tga",TextureClass::MIP_LEVELS_ALL);
    int carved=Carve_Model_Wound(model,point,normal,0.147f,0.09f,tex,true);
    tex->Release_Ref();
    if(carved) for(int i=0;i<3;++i) Throw_Piece("G_CHUNK_SM",point,normal*(3.0f+scale)+Random_Unit_Vector());
}

static bool Find_Model_Hit_Box_Center(RenderObjClass *model, const char *box, Vector3 &point);

static void Blast_Torso_Wounds(SoldierGameObj *soldier, const Vector3 &incoming, float scale)
{
    WoundBudget *budget=Get_Wound_Budget(soldier);
    // One pair of flank cavities per body. Later blasts can sever remaining
    // limbs and add scorch, without repeatedly rebuilding the torso wounds.
    if(budget->Blasts>=1 || budget->Craters>=16) return;
    ++budget->Blasts;
    RenderObjClass *model=soldier->Peek_Model();
    Vector3 side=model->Get_Transform().Get_Y_Vector();side.Normalize();
    Vector3 center=Bone_Position(model,"C SPINE1")+Vector3(0,0,0.06f);
    Find_Model_Hit_Box_Center(model,"K_ABDOMEN",center);
    Vector3 points[2],normals[2];bool found[2];
    for(int i=0;i<2;++i) {
        Vector3 ray=side*(i==0 ? 1.0f : -1.0f);float distance=1.0f;
        points[i]=center;normals[i]=-ray;
        found[i]=Find_Model_Wound_Surface(model,center-ray*0.65f,ray,distance,points[i],normals[i],true);
    }
    float severity=WWMath::Clamp(scale,0.35f,1.0f);
    float depth=0.075f;
    if(found[0] && found[1]) {
        float width=fabsf(Vector3::Dot_Product(points[1]-points[0],side));
        depth=WWMath::Clamp(width*0.28f,0.015f,0.08f+0.06f*severity);
    }
    TextureClass *tissue=WW3DAssetManager::Get_Instance()->Get_Texture("g_tissue.tga",TextureClass::MIP_LEVELS_ALL);
    for(int i=0;i<2;++i) {
        if(!found[i]) continue;
        int carved=Carve_Model_Wound(model,points[i],normals[i],0.16f+0.06f*severity,depth,tissue,true);
        if(carved) ++budget->Craters;
        Gore_Test_Log("blast torso side %d: %d affected triangles, radius %.3f, depth %.3f\n",i==0?-1:1,carved,
            0.16f+0.06f*severity,depth);
    }
    tissue->Release_Ref();
}

// Project onto the actual visible skin, then let Renegade's skinned decal system
// carry the stain with animation. Shared textures/materials are never modified.
static bool Surface_Stain(SoldierGameObj *soldier, WoundBudget *budget, const Vector3 &center,
    Vector3 incoming, const char *texture, float radius, int *decal_id=NULL)
{
    if(budget->SurfaceDecals>=40) return false;
    if(incoming.Length2()<0.0001f) incoming=-soldier->Peek_Model()->Get_Transform().Get_X_Vector();
    incoming.Normalize();
    Vector3 point=center,normal=-incoming;float distance=1.25f;
    if(!Find_Model_Wound_Surface(soldier->Peek_Model(),center-incoming*0.6f,incoming,distance,point,normal,true)) return false;
    Matrix3D tm(true);tm.Look_At(point,point-normal,FreeRandom.Get_Float(0.0f,6.2831853f));
    int decal=PhysicsSceneClass::Get_Instance()->Create_Decal(tm,texture,radius,false,false,soldier->Peek_Physical_Object());
    if(decal_id) *decal_id=decal;
    if(decal>=0) ++budget->SurfaceDecals;
    return decal>=0;
}

static void Blast_Scorch(SoldierGameObj *soldier, const Vector3 &incoming, float scale)
{
    WoundBudget *budget=Get_Wound_Budget(soldier);
    // A blast mark belongs on the exposed side, not as a whole-body burn dose.
    const char *bones[]={"C SPINE2","C SPINE1"};
    const char *boxes[]={"K_CHEST","K_ABDOMEN"};
    int before=budget->SurfaceDecals;
    for(int i=0;i<2;++i) {
        Vector3 center=Bone_Position(soldier->Peek_Model(),bones[i]);
        Find_Model_Hit_Box_Center(soldier->Peek_Model(),boxes[i],center);
        Surface_Stain(soldier,budget,center,incoming,"g_blast_scorch.tga",0.22f+0.12f*WWMath::Clamp(scale,0.0f,1.0f));
    }
    Gore_Test_Log("blast scorch: exposed side decals %d -> %d\n",before,budget->SurfaceDecals);
}

void GoreManager::Explosion_Hit(SoldierGameObj *soldier,const OffenseObjectClass &offense,
                                const Vector3 &origin,float pressure,float health_before)
{
    if (!IS_SOLOPLAY || !Is_Enabled() || !VoxelSkin::Is_Enabled() ||
        !soldier || soldier == COMBAT_STAR || soldier->Is_In_Vehicle() || Keep_Chemical_Corpse(soldier)) return;
    const float strength = offense.Get_Damage() * pressure;
    if (pressure < .08f || strength < 6.f || Warhead_Skips_Gore(offense)) return;

    RenderObjClass *model = soldier->Peek_Model();
    if (!model || !model->Get_HTree()) return;
    const Vector3 torso = Bone_Position(model,"C SPINE2");
    Vector3 direction = torso-origin;
    if (direction.Length2() <= .0001f) direction = -model->Get_Transform().Get_X_Vector();
    direction.Normalize();
    if (Surface_Damage_Type(offense,soldier,direction,NULL)!=SURFACE_DAMAGE_NONE) return;

    const bool corpse = soldier->Get_Defense_Object()->Get_Health() <= 0.f;
    if (health_before <= 0.f && !_Settings.CorpseDismemberment) return;
    const bool close = corpse && pressure >= _Settings.ExplosionMinScale && strength >= 32.f;
    const int stage = close ? 2 : 1;
    WoundBudget *budget = Get_Wound_Budget(soldier);
    if (budget->VoxelBlastStage >= stage) return;

    const float severity = WWMath::Clamp(strength / 85.f,.18f,1.f);
    const float radius = .056f + .075f * severity;
    bool changed = false;
    if (_Settings.BlastTorsoWounds || _Settings.BodyWounds) {
        // A floor strike often exposes the hips first. Find a surface on
        // the actual pose, including a corpse whose origin is still upright.
        const int start = direction.Z > .45f ? 4 : 2;
        for (int i=0;i<3 && !changed;++i) {
            const GoreHitBoxStruct &part = _HitBoxes[2+(start-2+i)%3];
            const int index = Gore_Bone_Index(model,part.Bone);
            if (index <= 0) continue;
            const char *bone = model->Get_Bone_Name(index);
            Vector3 center = model->Get_Bone_Transform(index).Get_Translation();
            Find_Model_Hit_Box_Center(model,part.HitBox,center);
            Vector3 incoming = center-origin;
            if (incoming.Length2() <= .0001f) incoming = direction;
            incoming.Normalize();
            const Vector3 exposed = center-incoming*.12f;
            changed = VoxelSkin::Blast(model,exposed,incoming,bone,radius,radius*.60f,false,corpse);
            if (changed && _Settings.BurnEffects)
                VoxelSkin::Burn(model,exposed,-incoming,bone,40,radius*1.8f,2,false);
        }
    }
    if (close && _Settings.Dismemberment) {
        struct Joint { const char *root; const char *end; float width; };
        static const Joint joints[] = {
            {"C L UPPERARM","C L FOREARM",.128f}, {"C R UPPERARM","C R FOREARM",.128f},
            {"C L THIGH","C L CALF",.162f}, {"C R THIGH","C R CALF",.162f}
        };
        const Joint *best = NULL;
        float facing = -FLT_MAX;
        for (const Joint &joint : joints) {
            int root = model->Get_Bone_Index(joint.root);
            int end = model->Get_Bone_Index(joint.end);
            if (root<=0 || end<=0 || model->Is_Bone_Skin_Hidden(root)) continue;
            float score = Vector3::Dot_Product(torso-model->Get_Bone_Transform(root).Get_Translation(),direction);
            if (score > facing) { facing = score; best = &joint; }
        }
        if (best) {
            const Vector3 root = Bone_Position(model,best->root);
            Vector3 axis = Bone_Position(model,best->end)-root;
            if (axis.Length2() > .0001f) {
                const Vector3 center = root + axis*.24f;
                axis.Normalize();
                changed = VoxelSkin::Blast(model,center,axis,best->root,best->width,.042f,true,true) || changed;
            }
        }
    }
    if (changed) {
        budget->VoxelBlastStage = stage;
        if (corpse && health_before > 0.f && _Settings.Blood)
            Add_Timed(NULL,0.f,1.8f,Bone_Position(model,"C PELVIS"),FreeRandom.Get_Float(.5f,.85f));
        Gore_Test_Log("voxel blast: actor %d pressure %.2f strength %.1f stage %d\n",
            soldier->Get_ID(),pressure,strength,stage);
    }
}

#include "gore_thermal.inl"

static void Apply_Surface_Damage(SoldierGameObj *soldier, SurfaceDamageType type,
    const Vector3 &direction, const char *box, float damage,
    const Vector3 *contact_point=NULL,const Vector3 *contact_normal=NULL)
{
    bool burn=type==SURFACE_DAMAGE_BURN;
    if((burn && !_Settings.BurnEffects) || (!burn && !_Settings.ChemicalEffects)) return;
    if(!burn && ArmorWarheadManager::Is_Skin_Impervious(ArmorWarheadManager::SPECIAL_DAMAGE_TYPE_CHEM,
        soldier->Get_Defense_Object()->Get_Skin())) return;
    WoundBudget *budget=Get_Wound_Budget(soldier);
    float &dose=burn ? budget->BurnDose : budget->ChemDose;
    const GoreHitBoxStruct *hit=Find_Hit_Box(box);
    dose=WWMath::Clamp(dose+damage,0.0f,1000.0f);
    if(!burn) {
        VoxelSkin::Clear_Burn(soldier->Peek_Model());
        Remove_Thermal_Surface(soldier->Peek_Model());Extinguish_Thermal_Emitters(soldier->Peek_Model());
        budget->BurnStage=0;budget->DeathBurnEmitters=true;
        budget->BurnCorpseSpreading=false;budget->BurnActiveUntil=0;
        memset(budget->BurnRegionStage,0,sizeof(budget->BurnRegionStage));
        memset(budget->BurnRegionDose,0,sizeof(budget->BurnRegionDose));
        memset(budget->BurnAnchored,0,sizeof(budget->BurnAnchored));
        memset(budget->BurnDirty,0,sizeof(budget->BurnDirty));
        budget->ChemMeltTarget=soldier->Get_Defense_Object()->Get_Health()<=0.0f ?
            WWMath::Clamp(dose*0.01f,0.04f,0.90f) : WWMath::Clamp(dose*0.004f,0.0f,0.04f);
        Gore_Test_Log("chemical deformation target %.3f dose %.2f splatters %d\n",budget->ChemMeltTarget,dose,budget->ChemSplats);
        return;
    }
    RenderObjClass *model=soldier->Peek_Model();
    if(!model || budget->ChemDose>0)return;
    const bool voxelBurn = IS_SOLOPLAY && VoxelSkin::Is_Enabled() && soldier!=COMBAT_STAR;
    if(damage>0)budget->BurnActiveUntil=_Time+8.0f;
    int box_index=hit ? (int)(hit-_HitBoxes) : 2; // Unlocalized thermal damage starts at the chest.
    Vector3 thermal_direction=direction;
    if(thermal_direction.Length2()<.0001f)thermal_direction=-model->Get_Transform().Get_X_Vector();
    thermal_direction.Normalize();
    int hemisphere=Vector3::Dot_Product(thermal_direction,model->Get_Transform().Get_X_Vector())>=0 ? 1 : 0;
    int region=box_index*2+hemisphere;
    if(region<0 || region>=ARRAY_COUNT(budget->BurnRegionDose))return;
    float &local_dose=budget->BurnRegionDose[region];local_dose=WWMath::Clamp(local_dose+damage,0.0f,1000.0f);
    int stage=local_dose>=45.0f ? 3 : local_dose>=15.0f ? 2 : 1;
    if(damage>0){
        // Limited visual heat transfer along adjacent anatomy, on the struck
        // side only. This neither changes health nor recursively propagates.
        static const int edges[][2]={{0,1},{1,2},{2,3},{3,4},{2,5},{2,6},{5,7},{7,8},{6,9},{9,10},{4,11},{11,13},{13,14},{4,12},{12,15},{15,16}};
        for(unsigned i=0;i<ARRAY_COUNT(edges);++i){
            int other=edges[i][0]==box_index?edges[i][1]:edges[i][1]==box_index?edges[i][0]:-1;
            if(other<0 || other>=ARRAY_COUNT(_HitBoxes))continue;
            float transfer=(box_index==2 && other==3)?.30f:(box_index==2 && (other==5 || other==6))?.18f:.12f;
            int profile=Thermal_Surface_Profile(soldier,_HitBoxes[other].HitBox);
            transfer*=profile==2?.35f:profile==1?.60f:1.0f;
            int adjacent=other*2+hemisphere;float &heat=budget->BurnRegionDose[adjacent];
            heat=WWMath::Clamp(heat+damage*transfer,0.0f,1000.0f);
            int wanted=heat>=45?3:heat>=15?2:heat>=5?1:0;
            if(wanted>budget->BurnRegionStage[adjacent])budget->BurnDirty[adjacent]=true;
        }
    }
    int previous=budget->BurnRegionStage[region];
    // Replace only this local burn mark when its severity advances. Continuous
    // flame no longer consumes the decal budget by stacking identical stains.
    bool precise_contact=contact_point && contact_normal && !budget->BurnAnchored[region] && !budget->BurnCorpseSpreading;
    if(stage<=previous && !precise_contact){budget->BurnDirty[region]=false;return;}
    budget->NextSurfaceTime=_Time+.12f;
    Vector3 center=Bone_Position(model,hit?hit->Bone:"C SPINE2");
    if(hit)Find_Model_Hit_Box_Center(model,hit->HitBox,center);
    bool head=box_index<=1,torso=box_index>=2 && box_index<=4;
    float radius=head?.065f+stage*.015f:torso?.10f+stage*.025f:.055f+stage*.018f;
    bool whole_region=budget->BurnCorpseSpreading;
    if(whole_region)radius=torso?.42f:head?.19f:.26f;
    // Bound layered geometry to eight independently heated regions per actor.
    int active=0;for(int i=0;i<ARRAY_COUNT(budget->BurnRegionStage);++i)if(budget->BurnRegionStage[i])++active;
    int region_limit=budget->BurnCorpseSpreading ? (int)ARRAY_COUNT(_HitBoxes)*2 : 8;
    if(!previous && active>=region_limit){budget->BurnDirty[region]=false;return;}
    if(!voxelBurn && !Thermal_Geometry_Budget_Available())return;
    Vector3 point=center,normal=-direction;float distance=1.25f;
    Vector3 incoming=thermal_direction;
    if(incoming.Length2()<.0001f)incoming=-model->Get_Transform().Get_X_Vector();
    incoming.Normalize();normal=-incoming;
    int region_bone=Gore_Bone_Index(model,hit?hit->Bone:"C SPINE2");
    if(region_bone<=0 || model->Is_Bone_Skin_Hidden(region_bone))return;
    const Matrix3D bone_tm=model->Get_Bone_Transform(region_bone);
    if(budget->BurnAnchored[region] && !precise_contact && !whole_region){
        point=bone_tm*budget->BurnLocalPoint[region];normal=bone_tm.Rotate_Vector(budget->BurnLocalNormal[region]);
    }else{
        if(precise_contact){center=*contact_point;incoming=-*contact_normal;if(incoming.Length2()>.0001f)incoming.Normalize();}
        if(!Find_Model_Wound_Surface(model,center-incoming*.6f,incoming,distance,point,normal,true))return;
    }
    if(precise_contact){Matrix3D inverse;bone_tm.Get_Orthogonal_Inverse(inverse);
        budget->BurnLocalPoint[region]=inverse*point;budget->BurnLocalNormal[region]=inverse.Rotate_Vector(normal);budget->BurnAnchored[region]=true;}
    char surface_name[32],pending_name[40];sprintf(surface_name,"G_THERMAL_%02d",region);
    sprintf(pending_name,"%s_NEW",surface_name);
    int profile=Thermal_Surface_Profile(soldier,hit?hit->HitBox:"K_CHEST");
    // Keep the previous region until its replacement exists. Resource pressure
    // or a newly severed bone cannot erase a previously valid surface first.
    const char *boneName = model->Get_Bone_Name(region_bone);
    if (voxelBurn && VoxelSkin::Burn(model,point,normal,boneName,region,radius,stage,whole_region)) {
        Remove_Thermal_Surface(model,surface_name);
    } else {
        // Unsupported meshes can still use the original burn renderer.
        if (voxelBurn && VoxelSkin::Manages(model)) {
            budget->BurnDirty[region] = false;
            return;
        }
        if(!Add_Thermal_Surface(model,point,normal,radius,stage,profile,region_bone,pending_name,2,whole_region))return;
        Remove_Thermal_Surface(model,surface_name);Rename_Thermal_Surface(model,pending_name,surface_name);
    }
    budget->BurnRegionStage[region]=stage;budget->BurnDirty[region]=false;
    budget->BurnStage=WWMath::Max(budget->BurnStage,stage);
    if(stage>=3 && previous<3 && budget->LiveBurnEmitters<2 &&
        soldier->Get_Defense_Object()->Get_Health()>0.0f &&
        Attach_Thermal_Remains(model,hit?hit->Bone:"C SPINE2",center,direction,true))
        ++budget->LiveBurnEmitters;
    // Thermal damage does not stamp ballistic organ textures or emit arterial
    // sprays. Exposed dermis lives in the thermally eroded material pass.
    Gore_Test_Log("localized burn: region %d stage %d dose %.2f radius %.3f decals %d\n",
        region,stage,local_dose,radius,budget->SurfaceDecals);
}




static const int _ThermalSpreadEdges[][2]={{0,1},{1,2},{2,3},{3,4},{2,5},{2,6},{5,7},{7,8},{6,9},{9,10},{4,11},{11,13},{13,14},{4,12},{12,15},{15,16}};

static void Begin_Corpse_Burning(SoldierGameObj *soldier,bool final_stage)
{
    if(!GoreManager::Is_Enabled() || !_Settings.BurnEffects || !soldier ||
        soldier==COMBAT_STAR || !soldier->Peek_Model() || soldier->Get_Defense_Object()->Get_Health()>0)return;
    WoundBudget *budget=Get_Wound_Budget(soldier);
    if(budget->ChemDose>0)return; // A dissolved body must never regain a burn shell.
    if(!final_stage && budget->BurnActiveUntil<_Time)return;
    if(!budget->BurnCorpseSpreading){
        budget->BurnCorpseSpreading=true;budget->NextCorpseHeatTime=_Time;
        // Invalidate impact-sized patches so the first corpse update covers the
        // corresponding anatomical region, keeping its existing severity.
        for(int r=0;r<ARRAY_COUNT(_HitBoxes)*2;++r)if(budget->BurnRegionStage[r]){
            budget->BurnRegionStage[r]=0;budget->BurnDirty[r]=true;
        }
    }
    if(final_stage)for(int r=0;r<ARRAY_COUNT(_HitBoxes)*2;++r){
        budget->BurnRegionDose[r]=WWMath::Max(budget->BurnRegionDose[r],45.0f);budget->BurnDirty[r]=true;
    }
}

void GoreManager::Burning_Status_Death(SoldierGameObj *soldier)
{
    Begin_Corpse_Burning(soldier,true);
}

static void Advance_Corpse_Heat(SoldierGameObj *soldier,WoundBudget &budget)
{
    if(!budget.BurnCorpseSpreading || budget.NextCorpseHeatTime>_Time ||
        soldier->Get_Defense_Object()->Get_Health()>0 || budget.ChemDose>0)return;
    // Fixed visual steps avoid frame-rate dependent spread or large catch-up
    // bursts. Both hemispheres and adjacent anatomy heat independently.
    const float interval=.35f;budget.NextCorpseHeatTime=_Time+interval;
    float previous[40];memcpy(previous,budget.BurnRegionDose,sizeof(previous));
    for(int r=0;r<ARRAY_COUNT(_HitBoxes)*2;++r){
        float heat=previous[r];
        if(heat>0)heat+=12.0f*interval;
        if(previous[r^1]>=5.0f)heat+=7.0f*interval;
        int box=r/2;
        for(unsigned e=0;e<ARRAY_COUNT(_ThermalSpreadEdges);++e){
            int other=_ThermalSpreadEdges[e][0]==box?_ThermalSpreadEdges[e][1]:
                _ThermalSpreadEdges[e][1]==box?_ThermalSpreadEdges[e][0]:-1;
            if(other>=0 && previous[other*2+r%2]>=5.0f)heat+=8.0f*interval;
        }
        budget.BurnRegionDose[r]=WWMath::Clamp(heat,0.0f,45.0f);
        int wanted=heat>=45?3:heat>=15?2:heat>=5?1:0;
        if(wanted>budget.BurnRegionStage[r])budget.BurnDirty[r]=true;
    }
}

static void Update_Thermal_Spread()
{
    if(!GoreManager::Is_Enabled() || !_Settings.BurnEffects)return;
    static unsigned cursor=0;int builds=0;
    for(unsigned scanned=0;scanned<ARRAY_COUNT(_WoundBudgets) && builds<8;++scanned){
        unsigned i=cursor++%ARRAY_COUNT(_WoundBudgets);
        WoundBudget &budget=_WoundBudgets[i];if(!budget.Id)continue;
        PhysicalGameObj *object=GameObjManager::Find_PhysicalGameObj(budget.Id);
        SoldierGameObj *soldier=object?object->As_SoldierGameObj():NULL;
        if(!soldier || soldier->Peek_Model()!=budget.Model || soldier->Peek_Model()->Is_Hidden())continue;
        Advance_Corpse_Heat(soldier,budget);
        int updated=0;
        for(int region=0;region<ARRAY_COUNT(_HitBoxes)*2 && updated<2 && builds<8;++region){
            if(!budget.BurnDirty[region])continue;
            int bone=Gore_Bone_Index(soldier->Peek_Model(),_HitBoxes[region/2].Bone);
            if(bone<=0 || soldier->Peek_Model()->Is_Bone_Skin_Hidden(bone)){budget.BurnDirty[region]=false;continue;}
            Vector3 direction=soldier->Peek_Model()->Get_Transform().Get_X_Vector()*(region%2?1.0f:-1.0f);
            Apply_Surface_Damage(soldier,SURFACE_DAMAGE_BURN,direction,_HitBoxes[region/2].HitBox,0);++updated;++builds;
        }
    }
}

static void Refresh_Thermal_Surfaces(SoldierGameObj *soldier)
{
    WoundBudget *budget=Get_Wound_Budget(soldier);if(!budget->BurnStage)return;
    RenderObjClass *model=soldier->Peek_Model();if(!model)return;
    for(int region=0;region<ARRAY_COUNT(budget->BurnRegionStage);++region){
        if(!budget->BurnRegionStage[region])continue;
        int bone=Gore_Bone_Index(model,_HitBoxes[region/2].Bone);
        if(bone<=0 || model->Is_Bone_Skin_Hidden(bone)){
            char name[32];sprintf(name,"G_THERMAL_%02d",region);Remove_Thermal_Surface(model,name);budget->BurnRegionStage[region]=0;continue;
        }
        int stage=budget->BurnRegionStage[region];budget->BurnRegionStage[region]=0;
        Vector3 direction=model->Get_Transform().Get_X_Vector()*(region%2?1.0f:-1.0f);
        Apply_Surface_Damage(soldier,SURFACE_DAMAGE_BURN,direction,_HitBoxes[region/2].HitBox,0);
        if(!budget->BurnRegionStage[region])budget->BurnRegionStage[region]=stage;
    }
}

// Optional soldier chunk BRTH. All fields are written separately at fixed width;
// neither native struct padding nor runtime addresses enter the save stream.
void GoreManager::Save_Thermal_State(SoldierGameObj *soldier,ChunkSaveClass &save)
{
    const WoundBudget *budget=NULL;
    for(unsigned i=0;i<ARRAY_COUNT(_WoundBudgets);++i)
        if(_WoundBudgets[i].Id==soldier->Get_ID() && _WoundBudgets[i].Model==soldier->Peek_Model())budget=&_WoundBudgets[i];
    if(!budget)return;
    uint32 version=3,count=0;
    for(unsigned i=0;i<ARRAY_COUNT(budget->BurnRegionStage);++i)if(budget->BurnRegionDose[i]>0)++count;
    if(!count || count>40)return;
    save.Begin_Chunk(0x42525448u);save.Write(&version,4);save.Write(&count,4);
    uint32 flags=budget->BurnCorpseSpreading?1u:0u;save.Write(&flags,4);
    for(uint32 region=0;region<ARRAY_COUNT(budget->BurnRegionStage);++region){
        if(budget->BurnRegionDose[region]<=0)continue;
        uint32 anchored=(budget->BurnAnchored[region]?1:0)|((budget->BurnRegionStage[region] || budget->BurnDirty[region])?2:0);float dose=budget->BurnRegionDose[region];
        save.Write(&region,4);save.Write(&dose,4);save.Write(&anchored,4);
        for(int k=0;k<3;++k)save.Write(&budget->BurnLocalPoint[region][k],4);
        for(int k=0;k<3;++k)save.Write(&budget->BurnLocalNormal[region][k],4);
    }
    save.End_Chunk();
}
void GoreManager::Load_Thermal_State(SoldierGameObj *soldier,ChunkLoadClass &load)
{
    uint32 version=0,count=0,flags=0;
    if(load.Cur_Chunk_Length()<8 || load.Read(&version,4)!=4 || load.Read(&count,4)!=4 ||
        (version!=1 && version!=2 && version!=3) || count>40 || (version==1 && count>8) ||
        load.Cur_Chunk_Length()!=(version==3?12u:8u)+count*36)return;
    if(version==3 && (load.Read(&flags,4)!=4 || flags>1))return;
    ThermalPendingLoad pending;pending.Actor=soldier;pending.Flags=flags;uint64_t seen=0;
    for(uint32 i=0;i<count;++i){
        ThermalSaveRegion region;
        if(load.Read(&region.Region,4)!=4 || load.Read(&region.Dose,4)!=4 || load.Read(&region.Anchored,4)!=4)return;
        for(int k=0;k<3;++k)if(load.Read(&region.Point[k],4)!=4)return;
        for(int k=0;k<3;++k)if(load.Read(&region.Normal[k],4)!=4)return;
        if(region.Region>=ARRAY_COUNT(_HitBoxes)*2 || region.Anchored>(version==1?1u:3u) ||
            !WWMath::Is_Valid_Float(region.Dose) || region.Dose<=0 || region.Dose>1000 ||
            (seen&((uint64_t)1<<region.Region)))return;
        for(int k=0;k<3;++k)if(!WWMath::Is_Valid_Float(region.Point[k]) || fabsf(region.Point[k])>4 ||
            !WWMath::Is_Valid_Float(region.Normal[k]) || fabsf(region.Normal[k])>1.01f)return;
        if((region.Anchored&1) && (region.Normal.Length2()<.5f || region.Normal.Length2()>1.5f))return;
        if(version==1)region.Anchored|=2;
        seen|=(uint64_t)1<<region.Region;pending.Regions.push_back(region);
    }
    // Duplicate chunks replace, rather than accumulating callbacks for one actor.
    for(unsigned i=0;i<_ThermalPendingLoads.size();++i)if(_ThermalPendingLoads[i].Actor==soldier){_ThermalPendingLoads[i]=pending;return;}
    if(_ThermalPendingLoads.size()<128)_ThermalPendingLoads.push_back(pending);
}
void GoreManager::Restore_Thermal_State(SoldierGameObj *soldier)
{
    for(unsigned i=0;i<_ThermalPendingLoads.size();++i){
        if(_ThermalPendingLoads[i].Actor!=soldier)continue;
        ThermalPendingLoad pending=_ThermalPendingLoads[i];_ThermalPendingLoads.erase(_ThermalPendingLoads.begin()+i);
        RenderObjClass *model=soldier->Peek_Model();if(!model || !model->Get_HTree())return;
        Remove_Thermal_Surface(model);WoundBudget *budget=Get_Wound_Budget(soldier);
        budget->BurnCorpseSpreading=(pending.Flags&1)!=0 && soldier->Get_Defense_Object()->Get_Health()<=0;
        budget->NextCorpseHeatTime=_Time;
        for(unsigned k=0;k<pending.Regions.size();++k){
            const ThermalSaveRegion &entry=pending.Regions[k];int region=entry.Region;
            budget->BurnRegionStage[region]=0;budget->BurnRegionDose[region]=entry.Dose;
            budget->BurnAnchored[region]=(entry.Anchored&1)!=0;budget->BurnLocalPoint[region]=entry.Point;budget->BurnLocalNormal[region]=entry.Normal;
            budget->BurnDirty[region]=(entry.Anchored&2)!=0;
            if(budget->BurnDirty[region] && Is_Enabled() && _Settings.BurnEffects){
                Vector3 direction=model->Get_Transform().Get_X_Vector()*(region%2?1.0f:-1.0f);
                Apply_Surface_Damage(soldier,SURFACE_DAMAGE_BURN,direction,_HitBoxes[region/2].HitBox,0);
            }
        }
        Gore_Test_Log("thermal save restored: actor %d regions %d\n",soldier->Get_ID(),(int)pending.Regions.size());return;
    }
}


static void Thermal_Save_Inspection(SoldierGameObj *soldier,const char *path)
{
    if(!strstr(GetCommandLineA(),"THERMALTEST=1"))return;
    static int previous=0;int request=GetPrivateProfileIntA("SpecialDamage","SaveRoundTrip",0,path);
    if(request<=previous)return;previous=request;
    WoundBudget *budget=Get_Wound_Budget(soldier);WoundBudget before=*budget;
    RawFileClass file("thermal_state_test.dat");
    if(!file.Open(FileClass::WRITE)){Gore_Test_Log("THERMALTEST save roundtrip FAIL open write\n");return;}
    {ChunkSaveClass save(&file);GoreManager::Save_Thermal_State(soldier,save);}file.Close();
    if(!file.Open(FileClass::READ)){Gore_Test_Log("THERMALTEST save roundtrip FAIL open read\n");return;}
    bool found=false;
    {ChunkLoadClass load(&file);while(load.Open_Chunk()){
        if(load.Cur_Chunk_ID()==0x42525448u){GoreManager::Load_Thermal_State(soldier,load);found=true;}
        load.Close_Chunk();}}
    file.Close();
    if(!found){Gore_Test_Log("THERMALTEST save roundtrip FAIL missing chunk\n");return;}
    Remove_Thermal_Surface(soldier->Peek_Model());
    memset(budget->BurnRegionStage,0,sizeof(budget->BurnRegionStage));
    memset(budget->BurnRegionDose,0,sizeof(budget->BurnRegionDose));budget->BurnStage=0;
    GoreManager::Restore_Thermal_State(soldier);
    bool good=true;int regions=0,anchors=0;
    for(unsigned i=0;i<ARRAY_COUNT(budget->BurnRegionStage);++i){
        if(before.BurnRegionStage[i])++regions;if(before.BurnAnchored[i])++anchors;
        if(before.BurnRegionStage[i]!=budget->BurnRegionStage[i] ||
            fabsf(before.BurnRegionDose[i]-budget->BurnRegionDose[i])>.001f ||
            before.BurnAnchored[i]!=budget->BurnAnchored[i])good=false;
    }
    Gore_Test_Log("THERMALTEST save roundtrip %s: regions %d anchored %d vertices %u\n",good?"PASS":"FAIL",regions,anchors,Thermal_Surface_Vertex_Count(soldier->Peek_Model()));
}

static int Melt_Model_Skin(RenderObjClass *model, const Vector3 &origin, float amount, float dissolved)
{
    if(!model || model->Is_Hidden()) return 0;
    const char *name=model->Get_Name();
    if(name && ((!strnicmp(name,"G_",2) && strnicmp(name,"G_CAP",5)) ||
        !strnicmp(name,"W_",2) || !strnicmp(name,"F_",2))) return 0;
    int vertices=0;
    if(model->Class_ID()==RenderObjClass::CLASSID_MESH) vertices=((MeshClass *)model)->Melt_Skin(origin,amount,dissolved);
    for(int i=0;i<model->Get_Num_Sub_Objects();++i) {
        RenderObjClass *sub=model->Get_Sub_Object(i);
        vertices+=Melt_Model_Skin(sub,origin,amount,dissolved);if(sub) sub->Release_Ref();
    }
    return vertices;
}

static void Clear_Projectile_Collision(RenderObjClass *model)
{
    if(!model) return;
    model->Set_Collision_Type(model->Get_Collision_Type() & ~COLLISION_TYPE_PROJECTILE);
    // HLodClass::Cast_Ray goes directly to child meshes/boxes. Parent bits do
    // not propagate, so the original standing/falling hit boxes need clearing.
    for(int i=0;i<model->Get_Num_Sub_Objects();++i) {
        RenderObjClass *sub=model->Get_Sub_Object(i);Clear_Projectile_Collision(sub);if(sub) sub->Release_Ref();
    }
}

static void Update_Chemical_Melting(float dt)
{
    _MeltAccumulator+=dt;
    if(_MeltAccumulator<0.05f || !GoreManager::Is_Enabled()) return;
    float elapsed=WWMath::Clamp(_MeltAccumulator,0.0f,0.15f);_MeltAccumulator=0;
    for(unsigned i=0;i<ARRAY_COUNT(_WoundBudgets);++i) {
        WoundBudget &budget=_WoundBudgets[i];
        if(budget.ChemDose<=0) continue;
        PhysicalGameObj *object=GameObjManager::Find_PhysicalGameObj(budget.Id);
        SoldierGameObj *soldier=object ? object->As_SoldierGameObj() : NULL;
        if(!soldier || soldier->Peek_Model()!=budget.Model) {budget.ChemDose=0;continue;}
        if(soldier->Get_Defense_Object()->Get_Health()<=0)
            budget.ChemMeltTarget=WWMath::Clamp(budget.ChemDose*0.01f,0.04f,0.90f);
        // Let the stock falling animation settle before collapsing the corpse's
        // local geometry. This avoids baking a standing pose into falling limbs.
        float target=budget.ChemMeltTarget;
        if(soldier->Is_Dead()) target=WWMath::Min(target,0.04f);
        float amount=WWMath::Clamp(target-budget.ChemMelt,0.0f,elapsed*0.24f);
        if(amount<=0.00001f) continue;
        Vector3 origin;soldier->Get_Position(&origin);
        if(target>0.04f && !budget.ChemPoseFrozen) {
            RenderObjClass *model=soldier->Peek_Model();Matrix3D inverse;
            model->Get_Transform().Get_Orthogonal_Inverse(inverse);
            std::vector<Matrix3D> pose(model->Get_Num_Bones());
            for(int b=1;b<model->Get_Num_Bones();++b) Matrix3D::Multiply(inverse,model->Get_Bone_Transform(b),&pose[b]);
            for(int b=1;b<model->Get_Num_Bones();++b) {model->Capture_Bone(b);model->Control_Bone_Root_Relative(b,pose[b]);}
            budget.ChemPoseFrozen=true;
        }
        int vertices=Melt_Model_Skin(soldier->Peek_Model(),origin,amount,budget.ChemMelt+amount);
        if(vertices<=0) continue;
        int before=(int)(budget.ChemMelt*10.0f);
        budget.ChemMelt+=amount;
        if(budget.ChemMelt>=0.80f && !budget.ChemLiquidCollision) {
            RenderObjClass *model=soldier->Peek_Model();
            Clear_Projectile_Collision(model);
            budget.ChemLiquidCollision=true;
            Gore_Test_Log("chemical dissolved body: projectile collision disabled, id %d\n",budget.Id);
            if(_WoundTest) {
                Vector3 chest=Bone_Position(model,"C SPINE2");Find_Model_Hit_Box_Center(model,"K_CHEST",chest);
                Vector3 forward=model->Get_Transform().Get_X_Vector();forward.Normalize();
                LineSegClass ray(chest-forward,chest+forward);CastResultStruct result;
                PhysRayCollisionTestClass test(ray,&result,BULLET_COLLISION_GROUP,COLLISION_TYPE_PROJECTILE);
                COMBAT_SCENE->Cast_Ray(test);
                Gore_Test_Log("chemical dissolved collision probe: old chest hit victim %d, fraction %.4f\n",test.CollidedPhysObj==soldier->Peek_Physical_Object(),result.Fraction);
            }
        }
        if(budget.ChemMelt>0.55f && !budget.ChemResidue) {
            Vector3 point,normal;
            if(Cast_Terrain_Ray(origin+Vector3(0,0,0.3f),origin-Vector3(0,0,1.5f),&point,&normal)) {
                Blood_Decal(point,normal,"g_chem_splat.tga",0.42f);
                budget.ChemResidue=true;Gore_Test_Log("chemical ground residue: id %d\n",budget.Id);
            }
        }
        if((int)(budget.ChemMelt*10.0f)!=before) Gore_Test_Log("chemical melt: id %d amount %.3f vertices %d\n",budget.Id,budget.ChemMelt,vertices);
    }
}

bool GoreManager::Prepare_Voxel_Bullet(SoldierGameObj *soldier,const OffenseObjectClass &offense,
    const Vector3 &direction,const Vector3 &point,const Vector3 &normal,const char *box,int ammo_id,bool explosive)
{
    if(!IS_SOLOPLAY || !Is_Enabled() || !soldier || soldier==COMBAT_STAR || explosive || offense.Get_Damage()<=0) return false;
    if(Surface_Damage_Type(offense,soldier,direction,box)!=SURFACE_DAMAGE_NONE || Warhead_Skips_Gore(offense))return false;
    const GoreHitBoxStruct *hit = Find_Hit_Box(box);
    const char *legBone = hit && hit->Cut >= 5 && hit->Cut <= 8 ? hit->Bone : nullptr;
    return VoxelSkin::Hit(soldier->Peek_Model(),point,direction,offense.Get_Damage(),ammo_id,
                          box && strstr(box,"HEAD")!=NULL,&normal,legBone,
                          soldier->Get_Defense_Object()->Get_Health() <= 0.0f);
}
void GoreManager::Bullet_Hit(SoldierGameObj *soldier, const OffenseObjectClass &offense,
    const Vector3 &direction, const Vector3 &point, const Vector3 &normal, const char *box, bool was_dead)
{
    if(_AimTest && soldier && soldier->Get_ID()==_AimVictim) {
        ++_AimHits;strncpy(_AimBox,box?box:"none",sizeof(_AimBox)-1);_AimBox[sizeof(_AimBox)-1]=0;
        Gore_Test_Log("AIMTEST contact case %d: %s at %.4f %.4f %.4f\n",_AimCase,_AimBox,point.X,point.Y,point.Z);
    }
    if(!Is_Enabled() || !soldier || soldier==COMBAT_STAR || offense.Get_Damage()<=0) return;
    SurfaceDamageType surface_type=Surface_Damage_Type(offense,soldier,direction,box);
    if(surface_type!=SURFACE_DAMAGE_NONE) {
        if(surface_type==SURFACE_DAMAGE_BURN && _Settings.BurnEffects && (!was_dead || _Settings.CorpseDismemberment))
            Apply_Surface_Damage(soldier,surface_type,direction,box,0,&point,&normal);
        if(surface_type==SURFACE_DAMAGE_CHEM && _Settings.ChemicalEffects && (!was_dead || _Settings.CorpseDismemberment)) {
            WoundBudget *budget=Get_Wound_Budget(soldier);
            if(_Time>=budget->NextSurfaceTime && budget->ChemSplats<8) {
                float radius=FreeRandom.Get_Float(0.065f,0.11f);
                bool stained=Surface_Stain(soldier,budget,point,direction,"g_chem_splat.tga",radius);
                // Collision boxes sit outside the visible skin and can move
                // during the same damage callback. Retry from that current box.
                if(!stained && box) {
                    Vector3 center=point;
                    const char *part=strrchr(box,'.');
                    if(Find_Model_Hit_Box_Center(soldier->Peek_Model(),part ? part+1 : box,center))
                        stained=Surface_Stain(soldier,budget,center,direction,"g_chem_splat.tga",radius);
                }
                budget->NextSurfaceTime=_Time+(stained ? 0.9f : 0.1f);
                if(stained) ++budget->ChemSplats;
                Gore_Test_Log("chemical impact splatters: %d at %.3f %.3f %.3f\n",budget->ChemSplats,point.X,point.Y,point.Z);
            }
        }
        Gore_Test_Log("SPECIAL projectile: %s, type %d, corpse %d, box %s\n",
            ArmorWarheadManager::Get_Warhead_Name(offense.Get_Warhead()),surface_type,was_dead,box?box:"none");
        return;
    }
    if(Warhead_Skips_Gore(offense)) return;
    if((IS_SOLOPLAY && VoxelSkin::Is_Enabled()) || VoxelSkin::Manages(soldier->Peek_Model()))return;
    if(was_dead && (!_Settings.CorpseDismemberment || Keep_Chemical_Corpse(soldier))) return;
    const GoreHitBoxStruct *hit=Find_Hit_Box(box);
    if(!hit) return;
    if(_WoundTest) Gore_Test_Log("projectile contact %s health %.0f corpse %d\n",box,soldier->Get_Defense_Object()->Get_Health(),was_dead);
    RenderObjClass *model=soldier->Peek_Model();
    if(!model || !model->Get_HTree()) return;
    int bone=model->Get_Bone_Index(hit->Bone);
    if(bone>0 && model->Is_Bone_Skin_Hidden(bone)) return;
    WoundBudget *budget=Get_Wound_Budget(soldier);
    Vector3 n=normal;
    if(n.Length2()<0.0001f) n=-direction;
    if(n.Length2()<0.0001f) return;
    n.Normalize();
    Vector3 surface=point, incoming=direction;
    if(incoming.Length2()>0.0001f) {
        incoming.Normalize();
        float distance=0.8f;
        Find_Model_Wound_Surface(model,point-incoming*0.25f,incoming,distance,surface,n);
    }
    bool head=hit->Cut==CUT_HEAD && !strstr(hit->HitBox,"NECK");
    bool belly=!strcmp(hit->HitBox,"K_ABDOMEN");
    bool chest=!strcmp(hit->HitBox,"K_CHEST");
    bool cavity_enabled=(head && _Settings.HeadDestruction) || (belly && _Settings.AbdominalWounds) || (chest && _Settings.ChestWounds);
    int carved=0;
    if(budget->Craters<16 && cavity_enabled) {
        // The SoF2 gut surface is deliberately also used for exposed brain tissue.
        TextureClass *tex=WW3DAssetManager::Get_Instance()->Get_Texture("g_tissue.tga",TextureClass::MIP_LEVELS_ALL);
        float strength=WWMath::Clamp(sqrtf(offense.Get_Damage()/30.0f),head?.85f:.80f,head?1.2f:1.25f);
        float radius=(head ? _Settings.HeadWoundRadius : chest ? _Settings.ChestWoundRadius : _Settings.AbdomenWoundRadius)*strength;
        radius=WWMath::Clamp(radius,head?.010f:.012f,head?.030f:.045f);
        bool through=offense.Get_Damage()>=30.0f && incoming.Length2()>.0001f && Carve_Model_Through_Wound(model,surface,incoming,radius,tex,head);
        if(through){carved=1;Gore_Test_Log("paired cavity and skinned tunnel created\n");}
        else carved=Carve_Model_Wound(model,surface,incoming.Length2()>0.0001f ? -incoming : n,radius,radius*(head?.45f:.65f),tex);
        tex->Release_Ref();
        if(carved) {
            Refresh_Thermal_Surfaces(soldier);
            ++budget->Craters;
            for(int i=0;i<(head?2:3);++i) Throw_Piece("G_CHUNK_SM",surface,n*FreeRandom.Get_Float(1.5f,3.0f)+Random_Unit_Vector());
            Spray_Blood(surface,n,2.0f,head?12:16,0.15f);
        }
        if(_GoreTest) Gore_Test_Log("wound %s: %d triangles, crater %d\n",head?"head":chest?"chest":"abdomen",carved,budget->Craters);
    }
    if(_Settings.BodyWounds && budget->Decals<32 && !carved) {
        static const char *textures[]={"g_wound1.tga","g_wound2.tga","g_wound3.tga","g_wound4.tga"};
        Matrix3D tm(true);
        tm.Look_At(surface,surface-n,FreeRandom.Get_Float(0.0f,6.2831853f));
        int decal=PhysicsSceneClass::Get_Instance()->Create_Decal(tm,textures[FreeRandom.Get_Int(4)],
            _Settings.BodyWoundRadius,
            false,false,soldier->Peek_Physical_Object());
        if(decal>=0) ++budget->Decals;
        if(_GoreTest) Gore_Test_Log("body wound decal: %d, total %d\n",decal,budget->Decals);
    }
}

void GoreManager::Soldier_Damaged(	SoldierGameObj * soldier, const OffenseObjectClass & damager,
												const Vector3 & direction, const char * collision_box_name,
												float explosion_scale, float health_before, float health_after )
{
	if (!Is_Enabled() || soldier == NULL || damager.Get_Damage() <= 0.0f) {
		return;
	}
	bool is_player = (soldier == COMBAT_STAR);
	if (is_player && !_Settings.Player) {
		return;
	}

	RenderObjClass * model = soldier->Peek_Model();
	if (model == NULL || model->Get_HTree() == NULL) {
		return;
	}
	SurfaceDamageType surface_type=Surface_Damage_Type(damager,soldier,direction,collision_box_name);
    if(health_before>0 && health_after<=0 && surface_type!=SURFACE_DAMAGE_CHEM)
        Begin_Corpse_Burning(soldier,false);
	if(surface_type!=SURFACE_DAMAGE_NONE) {
		if(health_before>0.0f || _Settings.CorpseDismemberment)
			Apply_Surface_Damage(soldier,surface_type,direction,collision_box_name,damager.Get_Damage()*WWMath::Clamp(explosion_scale,0.0f,1.0f));
        if(surface_type==SURFACE_DAMAGE_BURN && health_after<=0)Begin_Corpse_Burning(soldier,false);
		return; // Fire/chemical hits never enter blood, carving or severing paths.
	}
	if(Warhead_Skips_Gore(damager)) return;

	// The killing shot often only takes the last few points of health, so judge the shot by
	// the weapon's own damage too
	float damage = health_before - health_after;
	if (damager.Get_Damage() > damage) {
		damage = damager.Get_Damage();
	}
	bool killed = (health_before > 0.0f) && (health_after <= 0.0f);
	bool corpse_hit = health_before <= 0.0f && _Settings.CorpseDismemberment;
    if(corpse_hit && Keep_Chemical_Corpse(soldier)) return;
    const bool voxel = (IS_SOLOPLAY && VoxelSkin::Is_Enabled()) || VoxelSkin::Manages(model);
    const bool explosion = damager.ExplosionDamage && direction.Length2() > WWMATH_EPSILON;
    if (explosion && voxel && !is_player) return; // ExplosionManager supplies the epicenter and exposure.
	const GoreHitBoxStruct * hit = Find_Hit_Box(collision_box_name);

	Vector3 dir = direction;
	if (dir.Length2() > WWMATH_EPSILON) {
		dir.Normalize();
	}

	/*
	** Every flesh hit: a spray out of the exit side and some on the wall behind
	*/
	if (hit != NULL) {
		Vector3 pos = Bone_Position(model, hit->Bone);
		Spray_Blood(pos, dir + Vector3(0.0f, 0.0f, 0.2f), 2.5f, killed ? 12 : 6, 0.12f);
		Splatter_Behind(pos, dir, 2.5f, FreeRandom.Get_Float(0.35f, 0.7f));
	}

	if ((!killed && !corpse_hit) || is_player) {
		return;		// the player's own body is never taken apart (the camera is in it)
	}

    // The pre-damage precise hit already updated the voxel copy. Legacy bone
    // collapse/tunnels would invalidate its source mesh and skin palette.
    if(voxel)return;

	/*
	** The killing blow or a subsequent corpse hit
	*/
	if (explosion && explosion_scale >= _Settings.ExplosionMinScale) {
		Blow_Apart(soldier, dir, explosion_scale);
        Refresh_Thermal_Surfaces(soldier);
	} else if (hit != NULL && hit->Cut != CUT_NONE && _Settings.Dismemberment) {
		bool head = _Cuts[hit->Cut].IsHead;
		float min_damage = head ? _Settings.HeadshotDecapMinDamage : _Settings.LimbCutMinDamage;
		float chance = head ? _Settings.HeadshotDecapChance : _Settings.LimbCutChance;
		if (!(head && _Settings.HeadDestruction) && damage >= min_damage && FreeRandom.Get_Float() < chance) {
			Dismember(model, hit->Cut, FreeRandom.Get_Float(2.0f, 4.0f) * dir);
		}
	}

	// Queue one pool on death, not a fresh pool for every subsequent corpse hit.
	if (killed && _Settings.Blood) {
		Add_Timed(NULL, 0.0f, 1.8f, Bone_Position(model, "C PELVIS"), FreeRandom.Get_Float(0.5f, 0.85f));
	}
}

static void Gore_Test_Step(void)
{
	SoldierGameObj * star = COMBAT_STAR;
	if (star == NULL) {
		return;
	}
	Vector3 star_pos;
	star->Get_Position(&star_pos);
	Vector3 forward = star->Get_Transform().Get_X_Vector();
	forward.Z = 0.0f;
	if (forward.Length2() < 0.0001f) {
		forward.Set(1.0f, 0.0f, 0.0f);
	}
	forward.Normalize();

	if (_GoreTestVictim == NULL) {
		// Odd ticks: a fresh Nod soldier 5 m ahead, facing the player
		Matrix3D tm(true);
		tm.Obj_Look_At(star_pos + 5.0f * forward, star_pos, 0.0f);
		ScriptableGameObj * obj = (ScriptableGameObj *)ObjectLibraryManager::Create_Object("Nod_Minigunner_0");
		if (obj == NULL || obj->As_SoldierGameObj() == NULL) {
			Gore_Test_Log("could not create Nod_Minigunner_0\n");
			return;
		}
		obj->As_PhysicalGameObj()->Set_Transform(tm);
		obj->Start_Observers();
		_GoreTestVictim = obj->As_SoldierGameObj();
		Gore_Test_Log("spawned %s\n", _GoreTestVictim->Get_Definition().Get_Name());
	}

	// the test is about the victims, not about surviving them
	star->Get_Defense_Object()->Set_Health(star->Get_Defense_Object()->Get_Health_Max());

	// Even ticks: kill it, cycling through the gore cases
	SoldierGameObj * victim = _GoreTestVictim;
	_GoreTestVictim = NULL;
	static const char * boxes[] = { "test.K_HEAD", "test.K_L UPPERARM", "test.K_R THIGH", "test.K_L FOREARM", NULL };
	const char * box = _CorpseTest ? "test.K_CHEST" : boxes[_GoreTestStep % 5];
	_GoreTestStep++;

	OffenseObjectClass offense(10000.0f, 0, NULL);
	offense.ExplosionDamage = box == NULL;
	float before = victim->Get_Defense_Object()->Get_Health();
	victim->Apply_Damage_Extended(offense, 1.0f, forward, box);
	if (!box)
        GoreManager::Explosion_Hit(victim,offense,Bone_Position(victim->Peek_Model(),"C SPINE2")-forward,1.f,before);
	Gore_Test_Log("step %d: %s, health %.0f -> %.0f, gibs %d\n",
		_GoreTestStep, box ? box : "explosion", before, victim->Get_Defense_Object()->Get_Health(), _GibCount);

	if (_CorpseTest) {
		// Exercise the real soldier damage entry point after death. The forearm,
		// then its parent, checks that subsequent gibs keep earlier cuts missing.
		RenderObjClass * model = victim->Peek_Model();
		const char * hit_boxes[] = { "test.K_L FOREARM", "test.K_L UPPERARM", "test.K_R THIGH", "test.K_HEAD" };
		const int cuts[] = { 3, 1, 6, 0 };
		for (int i = 0; i < 4; ++i) {
			int bone = model->Get_Bone_Index(_Cuts[cuts[i]].CutBone);
			bool was_cut = bone > 0 && model->Is_Bone_Skin_Hidden(bone);
			int gibs_before = _GibCount;
			victim->Apply_Damage_Extended(offense, 1.0f, forward, hit_boxes[i]);
			bool is_cut = bone > 0 && model->Is_Bone_Skin_Hidden(bone);
			bool passed = !was_cut && is_cut && victim->Get_Defense_Object()->Get_Health() == 0.0f;
			Gore_Test_Log("corpse %s: %s, health %.0f, gibs %d -> %d, collision group %d\n",
				hit_boxes[i], passed ? "PASS" : "FAIL", victim->Get_Defense_Object()->Get_Health(),
				gibs_before, _GibCount, victim->Peek_Physical_Object()->Get_Collision_Group());
			// A repeated hit must not spawn that limb again.
			gibs_before = _GibCount;
			victim->Apply_Damage_Extended(offense, 1.0f, forward, hit_boxes[i]);
			Gore_Test_Log("repeat %s: %s\n", hit_boxes[i], _GibCount == gibs_before ? "PASS" : "FAIL");
		}
		// Trace with the same collision group/type used by actual bullets.
		Vector3 chest = Bone_Position(model, "C SPINE2");
		CastResultStruct result;
		LineSegClass ray(chest + 2.0f * forward, chest - 2.0f * forward);
		PhysRayCollisionTestClass test(ray, &result, BULLET_COLLISION_GROUP, COLLISION_TYPE_PROJECTILE);
		COMBAT_SCENE->Cast_Ray(test);
		Gore_Test_Log("corpse projectile trace: %s\n",
			test.CollidedPhysObj == victim->Peek_Physical_Object() ? "PASS" : "FAIL");
	}
}

static void Fire_Test_Step(void)
{
	_FireTestTrigger = false;
	SoldierGameObj * star = COMBAT_STAR;
	if (star == NULL) {
		return;
	}
	star->Get_Defense_Object()->Set_Health(star->Get_Defense_Object()->Get_Health_Max());

	PhysicalGameObj * victim = _FireTestVictimId ? GameObjManager::Find_PhysicalGameObj(_FireTestVictimId) : NULL;
	if (victim != NULL && victim->Get_Defense_Object()->Get_Health() > 0.0f) {
		_FireTestTrigger = true;		// keep shooting
		return;
	}
	if (_FireTestVictimId != 0) {
		_FireTestKills++;
		Gore_Test_Log("kill %d done, gibs %d\n", _FireTestKills, _GibCount);
		_FireTestVictimId = 0;
		_GoreTestTimer = -2.0f;			// let the body fall before the next one
		return;
	}

	// a fresh Nod soldier along the player's aim, 4-7 m away
	Vector3 star_pos;
	star->Get_Position(&star_pos);
	Vector3 forward = star->Get_Transform().Get_X_Vector();
	forward.Z = 0.0f;
	if (forward.Length2() < 0.0001f) {
		forward.Set(1.0f, 0.0f, 0.0f);
	}
	forward.Normalize();
	float dist = 4.0f + (float)(_GoreTestStep % 4);
	_GoreTestStep++;
	Matrix3D tm(true);
	tm.Obj_Look_At(star_pos + dist * forward, star_pos, 0.0f);
	ScriptableGameObj * obj = (ScriptableGameObj *)ObjectLibraryManager::Create_Object("Nod_Minigunner_0");
	if (obj == NULL || obj->As_SoldierGameObj() == NULL) {
		Gore_Test_Log("could not create Nod_Minigunner_0\n");
		return;
	}
	obj->As_PhysicalGameObj()->Set_Transform(tm);
	obj->Start_Observers();
	_FireTestVictimId = obj->Get_ID();
	const char * weapon = (star->Get_Weapon() != NULL) ? star->Get_Weapon()->Get_Definition()->Get_Name() : "none";
	Gore_Test_Log("spawned victim %d at %.0f m, player weapon %s\n",_FireTestVictimId, dist, weapon);
	_FireTestTrigger = true;
}

static bool Find_Model_Hit_Box_Center(RenderObjClass *model, const char *box, Vector3 &point)
{
    const char *name=model->Get_Name();
    const char *suffix=name ? strrchr(name,'.') : NULL;
    if(suffix && !stricmp(suffix+1,box)) { point=model->Get_Bounding_Box().Center;return true; }
    bool found=false;
    for(int i=0;i<model->Get_Num_Sub_Objects() && !found;++i) {
        RenderObjClass *sub=model->Get_Sub_Object(i);
        if(sub) { found=Find_Model_Hit_Box_Center(sub,box,point);sub->Release_Ref(); }
    }
    return found;
}

static void Inspection_Stop_Emitters(RenderObjClass *model)
{
    if(!model) return;
    if(model->Class_ID()==RenderObjClass::CLASSID_PARTICLEEMITTER) ((ParticleEmitterClass *)model)->Stop();
    for(int i=0;i<model->Get_Num_Sub_Objects();++i) {
        RenderObjClass *sub=model->Get_Sub_Object(i);
        Inspection_Stop_Emitters(sub);if(sub) sub->Release_Ref();
    }
}

// Native regression for renderer registration keys across model/lighting changes.
// It runs only in an explicitly requested developer inspection process.
static void Mesh_Clone_Registration_Test(void)
{
    static bool done=false;
    if(done || !strstr(GetCommandLineA(),"MESHCLONETEST=1"))return;
    done=true;
    if(strstr(GetCommandLineA(),"THERMALTEST=1")){
        FILE *catalog=fopen("thermal_outfits.tsv","w");if(catalog){
            fprintf(catalog,"id\tpreset\tmodel\n");
            for(DefinitionClass *definition=DefinitionMgrClass::Get_First(CLASSID_GAME_OBJECT_DEF_SOLDIER);definition;
                definition=DefinitionMgrClass::Get_Next(definition,CLASSID_GAME_OBJECT_DEF_SOLDIER)){
                const SoldierGameObjDef *soldier=(const SoldierGameObjDef*)definition;
                PhysDefClass *physical=(PhysDefClass*)DefinitionMgrClass::Find_Definition(soldier->Get_Phys_Def_ID());
                fprintf(catalog,"%u\t%s\t%s\n",definition->Get_ID(),definition->Get_Name(),physical?(const char*)physical->Get_Model_Name():"");
            }
            fclose(catalog);
        }
    }
    for(int iteration=0;iteration<16;++iteration) {
        RenderObjClass *object=Create_Gore_Model("G_CHUNK_SM");
        if(!object || object->Class_ID()!=RenderObjClass::CLASSID_MESH) {
            if(object)object->Release_Ref();Gore_Test_Log("MESHCLONETEST unsupported fixture asset\n");return;
        }
        MeshClass *mesh=(MeshClass *)object;
        TheDX8MeshRenderer.Register_Mesh_Type(mesh);
        mesh->Make_Unique();
        TheDX8MeshRenderer.Register_Mesh_Type(mesh);
        unsigned int *lighting=mesh->Get_User_Lighting_Array(true);
        for(int v=0;v<mesh->Peek_Model()->Get_Vertex_Count();++v)lighting[v]=0xffffffffu;
        TheDX8MeshRenderer.Register_Mesh_Type(mesh);
        object->Release_Ref();
        // Reuse the original asset model after the modified representative dies.
        // Before the lifecycle fix, its old key could still point to that object.
        object=Create_Gore_Model("G_CHUNK_SM");
        TheDX8MeshRenderer.Register_Mesh_Type((MeshClass *)object);
        object->Release_Ref();
    }
    Gore_Test_Log("MESHCLONETEST passed: 16 shared-model clone/lighting/free/reuse cycles\n");
}

// This hook never alters campaign actors or normal launcher sessions. Native
// CanObjectDie keeps the full damage path active; refilling before each hit
// prevents the soldier's unchanged-health early return at the one-health floor.
void GoreManager::Prepare_Inspection_Damage(SoldierGameObj *soldier)
{
    if(!_WoundTest || !soldier || soldier->Get_ID()!=_WoundTestId)return;
    char path[MAX_PATH],mode[32];Get_Exe_Relative_Path("woundtest.ini",path,sizeof(path));
    GetPrivateProfileStringA("SpecialDamage","Mode","",mode,sizeof(mode),path);
    if(!mode[0])return;
    int step=(int)GetPrivateProfileIntA("SpecialDamage","Step",0,path);
    bool immortal=step!=4 && GetPrivateProfileIntA("SpecialDamage","Immortal",1,path)!=0;
    soldier->Get_Defense_Object()->Set_Can_Object_Die(!immortal);
    if(immortal && soldier->Get_Defense_Object()->Get_Health()>0)
        soldier->Get_Defense_Object()->Set_Health(soldier->Get_Defense_Object()->Get_Health_Max());
}

// Opt-in real-projectile inspection scene, never entered by normal launchers.
static void Wound_Test_Step(void)
{
    SoldierGameObj *star=COMBAT_STAR;
    if(!star || !COMBAT_CAMERA) return;
    Mesh_Clone_Registration_Test();
    star->Get_Defense_Object()->Set_Health(star->Get_Defense_Object()->Get_Health_Max());
    // Optional asset inspection within the existing opt-in WOUNDTEST scene.
    // No effect on normal launcher/gameplay paths.
    char geometry_path[MAX_PATH],geometry_name[128];
    Get_Exe_Relative_Path("woundtest.ini",geometry_path,sizeof(geometry_path));
    GetPrivateProfileStringA("Geometry","Preset","",geometry_name,sizeof(geometry_name),geometry_path);
    bool geometry_preset=geometry_name[0]!=0;
    if(!geometry_preset) GetPrivateProfileStringA("Geometry","Model","",geometry_name,sizeof(geometry_name),geometry_path);
    if(geometry_name[0]) {
        if(_stricmp(geometry_name,_GeometryPreviewName)!=0) {
            Clear_Geometry_Preview();
            lstrcpynA(_GeometryPreviewName,geometry_name,sizeof(_GeometryPreviewName));
            if(geometry_preset) {
                PhysicalGameObj *obj=ObjectLibraryManager::Create_Object(geometry_name);
                if(obj) {
                    Vector3 pos;star->Get_Position(&pos);
                    Vector3 forward=star->Get_Transform().Get_X_Vector();forward.Z=0;forward.Normalize();
                    pos+=forward*10.0f;
                    if(obj->As_VehicleGameObj()) pos.Z+=3.0f;
                    else if(obj->Peek_Model()) {
                        AABoxClass spawn_box;obj->Peek_Model()->Get_Obj_Space_Bounding_Box(spawn_box);
                        pos.Z+=spawn_box.Extent.Z-spawn_box.Center.Z;
                    }
                    Matrix3D tm(true);tm.Set_Translation(pos);obj->Set_Transform(tm);obj->Start_Observers();
                    obj->Enable_Cinematic_Freeze(false);
                    if(obj->As_SoldierGameObj()) {
                        SoldierGameObj *inspection_actor=obj->As_SoldierGameObj();
                        inspection_actor->Innate_Disable();inspection_actor->Set_Loiters_Allowed(false);
                        inspection_actor->Set_Animation("S_A_HUMAN.H_A_A0A0",true);
                        inspection_actor->Peek_Physical_Object()->Set_Immovable(true);
                        inspection_actor->Enable_Cinematic_Freeze(true);
                    }
                    obj->Post_Think();_GeometryPresetId=obj->Get_ID();
                    _GeometryPreview=obj->Peek_Model();if(_GeometryPreview) _GeometryPreview->Add_Ref();
                }
            } else {
                _GeometryPreview=Create_Gore_Model(geometry_name);
                if(_GeometryPreview) _GeometryWrapper=Add_To_Scene(_GeometryPreview);
            }
            Gore_Test_Log("GEOMETRY preview %s: %s (%s)\n",geometry_name,_GeometryPreview?"loaded":"FAILED",geometry_preset?"game object":"render asset");
        }
        if(!_GeometryPreview) return;
        if(!_WoundCamera) {_WoundCamera=Create_Gore_Model("G_CHUNK_SM");if(_WoundCamera) COMBAT_CAMERA->Set_Host_Model(_WoundCamera);}
        if(!_WoundCamera) return;
        AABoxClass box;_GeometryPreview->Get_Obj_Space_Bounding_Box(box);
        Vector3 origin;star->Get_Position(&origin);
        Vector3 pos=origin+Vector3(0,5.0f,0);pos.Z=origin.Z-box.Center.Z+box.Extent.Z;
        if(_GeometryPresetId) {
            PhysicalGameObj *obj=GameObjManager::Find_PhysicalGameObj(_GeometryPresetId);
            if(obj) obj->Get_Position(&pos);
        } else {
            Matrix3D model_tm(true);model_tm.Set_Translation(pos);_GeometryPreview->Set_Transform(model_tm);
        }
        {
            char animation_name[128];
            GetPrivateProfileStringA("Geometry","Animation","",animation_name,sizeof(animation_name),geometry_path);
            if(animation_name[0]) {
                HAnimClass *animation=WW3DAssetManager::Get_Instance()->Get_HAnim(animation_name);
                if(animation) {
                    _GeometryPreview->Set_Animation(animation,(float)(int)GetPrivateProfileIntA("Geometry","Frame",0,geometry_path),RenderObjClass::ANIM_MODE_MANUAL);
                    animation->Release_Ref();
                }
            }
        }
        Vector3 target=pos+box.Center;
        target.Z+=(float)(int)GetPrivateProfileIntA("Geometry","FocusZCM",0,geometry_path)*0.01f;
        float yaw=(float)(int)GetPrivateProfileIntA("Geometry","YawDegrees",45,geometry_path)*0.01745329252f;
        float distance=(float)(int)GetPrivateProfileIntA("Geometry","DistanceCM",500,geometry_path)*0.01f;
        float height=(float)(int)GetPrivateProfileIntA("Geometry","HeightCM",80,geometry_path)*0.01f;
        Matrix3D camera(true);camera.Look_At(target+Vector3(cosf(yaw)*distance,sinf(yaw)*distance,height),target,0);
        _WoundCamera->Set_Transform(camera);
        return;
    }
    if(_GeometryPreviewName[0]) Clear_Geometry_Preview();
    SoldierGameObj *victim=_WoundTestId ? (SoldierGameObj *)GameObjManager::Find_PhysicalGameObj(_WoundTestId) : NULL;
    // Finish after the inspected corpse expires; do not spawn overlapping controls.
    if(!victim && _WoundTestId!=0) return;
    if(!victim) {
        Vector3 origin;star->Get_Position(&origin);
        _WoundForward=star->Get_Transform().Get_X_Vector();_WoundForward.Z=0;_WoundForward.Normalize();
        Vector3 side(-_WoundForward.Y,_WoundForward.X,0);
        for(int i=0;i<2;++i) {
            char inspection_preset[128],inspection_path[MAX_PATH];
            Get_Exe_Relative_Path("woundtest.ini",inspection_path,sizeof(inspection_path));
            GetPrivateProfileStringA("Test","VictimPreset","Nod_Minigunner_0",inspection_preset,sizeof(inspection_preset),inspection_path);
            PhysicalGameObj *obj=ObjectLibraryManager::Create_Object(inspection_preset);
            if(!obj || !obj->As_SoldierGameObj()) return;
            SoldierGameObj *actor=obj->As_SoldierGameObj();
            Matrix3D tm(true);Vector3 pos=origin+_WoundForward*3.0f+side*(i?1.0f:0.0f);
            tm.Obj_Look_At(pos,pos-_WoundForward,0);
            actor->Set_Transform(tm);actor->Start_Observers();actor->Innate_Disable();
            actor->Set_Animation("S_A_HUMAN.H_A_A0A0",true);
            actor->Set_Loiters_Allowed(false);actor->Peek_Physical_Object()->Set_Immovable(true);
            actor->Get_Defense_Object()->Set_Health_Max(10000);actor->Get_Defense_Object()->Set_Health(10000);
            actor->Post_Think();
            if(i==0) {_WoundTestId=actor->Get_ID();victim=actor;} else _WoundControlId=actor->Get_ID();
            Gore_Test_Log("WOUNDTEST spawned %s id %d model %s\n",actor->Get_Definition().Get_Name(),actor->Get_ID(),actor->Peek_Model()->Get_Name());
        }
        _WoundCamera=Create_Gore_Model("G_CHUNK_SM");
        if(!_WoundCamera) return;
        COMBAT_CAMERA->Set_Host_Model(_WoundCamera);
        _WoundTestStage=0;_WoundStageTick=GetTickCount();
        Gore_Test_Log("WOUNDTEST stage 0: undamaged comparison\n");
    }
    DWORD elapsed=GetTickCount()-_WoundStageTick;
    char test_path[MAX_PATH];Get_Exe_Relative_Path("woundtest.ini",test_path,sizeof(test_path));
    // Special weapon inspection shares the existing opt-in scene and its control.
    char special_mode[32];GetPrivateProfileStringA("SpecialDamage","Mode","",special_mode,sizeof(special_mode),test_path);
    if(special_mode[0]) {
        int step=(int)GetPrivateProfileIntA("SpecialDamage","Step",0,test_path);
        if(step>_WoundTestStage) {
            _WoundTestStage=step;
            bool blast=!stricmp(special_mode,"blast") || !stricmp(special_mode,"splash");
            if(blast) {
                const ExplosionDefinitionClass *def=(const ExplosionDefinitionClass *)DefinitionMgrClass::Find_Typed_Definition(
                    "Explosion_Rocket_Launcher_01",CLASSID_DEF_EXPLOSION);
                if(def) {
                    if(step==1) {victim->Get_Defense_Object()->Set_Health(25);victim->Get_Defense_Object()->Set_Shield_Strength(0);}
                    Vector3 center=Bone_Position(victim->Peek_Model(),"C SPINE1");
                    ExplosionManager::Create_Explosion_At(def->Get_ID(),center-_WoundForward*0.15f,star,
                        Vector3(0,0,-1),!stricmp(special_mode,"blast") ? victim : NULL);
                    victim->Enable_Cinematic_Freeze(true);
                    Gore_Test_Log("SPECIAL actual explosion %s: forced %d, health %.0f, gibs %d\n",def->Get_Name(),
                        !stricmp(special_mode,"blast"),victim->Get_Defense_Object()->Get_Health(),_GibCount);
                }
            } else {
                const AmmoDefinitionClass *ammo=WeaponManager::Find_Ammo_Definition(!stricmp(special_mode,"burn") ?
                    "Ammo_Flamethrower_Player" : "Ammo_ChemSprayer_Player");
                if(ammo) {
                    if(step==4) {
                        victim->Enable_Cinematic_Freeze(false);
                        victim->Peek_Physical_Object()->Set_Immovable(false);
                        victim->Get_Defense_Object()->Set_Health(1.0f);
                        victim->Get_Defense_Object()->Set_Shield_Strength(0.0f);
                        OffenseObjectClass kill(2.0f,(int)ammo->Warhead,star);
                        char kill_box[80],hit_box[48];GetPrivateProfileStringA("SpecialDamage","HitBox","K_CHEST",hit_box,sizeof(hit_box),test_path);
                        sprintf(kill_box,"test.%s",hit_box);
                        victim->Apply_Damage_Extended(kill,1.0f,_WoundForward,kill_box);
                        victim->Enable_Cinematic_Freeze(true);
                    } else {
                        Vector3 target=Bone_Position(victim->Peek_Model(),"C SPINE2");
                        char hit_box[48];GetPrivateProfileStringA("SpecialDamage","HitBox",step==5?"K_HEAD":"K_CHEST",hit_box,sizeof(hit_box),test_path);
                        const GoreHitBoxStruct *inspection_hit=Find_Hit_Box(hit_box);
                        if(inspection_hit)target=Bone_Position(victim->Peek_Model(),inspection_hit->Bone);
                        Find_Model_Hit_Box_Center(victim->Peek_Model(),hit_box,target);
                        int shots=step==1 ? 1 : step==2 ? 8 : 24;
                        if(GetPrivateProfileIntA("SpecialDamage","ExactRegion",0,test_path)){
                            // Broad native flame projectiles can hit the chest before an
                            // aimed forearm. This explicit material fixture isolates a
                            // named region through the native damage callbacks instead.
                            Vector3 chest=Bone_Position(victim->Peek_Model(),"C SPINE2");
                            Vector3 outward=target-chest;outward.Z=0;
                            if(outward.Length2()<.0001f)outward=-_WoundForward;outward.Normalize();
                            char exact_box[80];sprintf(exact_box,"test.%s",hit_box);
                            for(int i=0;i<shots;++i){OffenseObjectClass heat(2.0f,(int)ammo->Warhead,star);
                                victim->Apply_Damage_Extended(heat,1.0f,-outward,exact_box);
                                GoreManager::Bullet_Hit(victim,heat,-outward,target,outward,exact_box,false);}
                            Gore_Test_Log("THERMALTEST exact region %s: %d native damage callbacks\n",hit_box,shots);
                        }else for(int i=0;i<shots;++i) BulletManager::Create_Bullet(ammo,target-_WoundForward*0.65f,
                            _WoundForward*(float)ammo->Velocity,star,0,target,victim);
                    }
                    Gore_Test_Log("SPECIAL %s step %d: ammo %s warhead %s, health %.0f, gibs %d\n",special_mode,step,
                        ammo->Get_Name(),ArmorWarheadManager::Get_Warhead_Name((int)ammo->Warhead),victim->Get_Defense_Object()->Get_Health(),_GibCount);
                }
            }
        }
        if(GetPrivateProfileIntA("SpecialDamage","Quiet",0,test_path)) Inspection_Stop_Emitters(victim->Peek_Model());
        if(GetPrivateProfileIntA("SpecialDamage","ReleasePose",0,test_path)) {
            victim->Enable_Cinematic_Freeze(false);
            // Inspection immobilization must not prevent corpse floor settling.
            if(victim->Get_Defense_Object()->Get_Health()<=0 && victim->Peek_Physical_Object())
                victim->Peek_Physical_Object()->Set_Immovable(false);
        }
        if(GetPrivateProfileIntA("SpecialDamage","HideControl",0,test_path)){
            PhysicalGameObj *control=GameObjManager::Find_PhysicalGameObj(_WoundControlId);
            if(control && control->Peek_Model())control->Peek_Model()->Set_Hidden(true);
        }
        if(GetPrivateProfileIntA("SpecialDamage","HideHUD",0,test_path))HUDClass::Enable(false);
        Thermal_Save_Inspection(victim,test_path);
        Matrix3D camera(true);Vector3 target=Bone_Position(victim->Peek_Model(),"C SPINE1")+Vector3(0,0,0.18f);
        if(!stricmp(special_mode,"goo")) {
            Vector3 origin;victim->Get_Position(&origin);
            target.Z=origin.Z+(target.Z-origin.Z)*expf(-2.3f*Get_Wound_Budget(victim)->ChemMelt);
        }
        if(GetPrivateProfileIntA("SpecialDamage","FocusHitBox",0,test_path) && victim->Get_Defense_Object()->Get_Health()>0){
            char focus_box[48];GetPrivateProfileStringA("SpecialDamage","HitBox","K_CHEST",focus_box,sizeof(focus_box),test_path);
            const GoreHitBoxStruct *focus=Find_Hit_Box(focus_box);
            if(focus){target=Bone_Position(victim->Peek_Model(),focus->Bone);Find_Model_Hit_Box_Center(victim->Peek_Model(),focus_box,target);}
        }
        float angle=(float)(int)GetPrivateProfileIntA("SpecialDamage","YawDegrees",0,test_path)*0.01745329252f;
        Vector3 side(-_WoundForward.Y,_WoundForward.X,0);
        float camera_distance=WWMath::Clamp((float)GetPrivateProfileIntA("SpecialDamage","CameraDistanceCM",225,test_path)*.01f,.6f,6.0f);
        float camera_height=(int)GetPrivateProfileIntA("SpecialDamage","CameraHeightCM",12,test_path)*.01f;
        camera.Look_At(target-_WoundForward*(camera_distance*cosf(angle))+side*(camera_distance*sinf(angle))+Vector3(0,0,camera_height),target,0);
        _WoundCamera->Set_Transform(camera);
        return;
    }
    int requested=GetPrivateProfileIntA("Test","Stage",-1,test_path);
    if(((requested<0 && elapsed>20000) || requested>_WoundTestStage) && _WoundTestStage<6) {
        ++_WoundTestStage;_WoundStageTick=GetTickCount();
        if(_WoundTestStage==6) {
            victim->Enable_Cinematic_Freeze(false);
            PhysicalGameObj *control=GameObjManager::Find_PhysicalGameObj(_WoundControlId);
            if(control) control->Enable_Cinematic_Freeze(false);
        }
        const char *bone=_WoundTestStage<=2 ? "C HEAD" : _WoundTestStage==3 ? "C SPINE1" : "C SPINE2";
        if(_WoundTestStage==5) {
            OffenseObjectClass kill(100000.0f,0,NULL);
            victim->Apply_Damage_Extended(kill,1.0f,_WoundForward,"test.K_CHEST");
            Gore_Test_Log("WOUNDTEST death health %.0f\n",victim->Get_Defense_Object()->Get_Health());
            bone="C HEAD";
        }
        Vector3 target=Bone_Position(victim->Peek_Model(),bone);
        // Skeleton pivots can lie at a joint; bias shots toward the visible body region.
        Find_Model_Hit_Box_Center(victim->Peek_Model(),(_WoundTestStage<=2 || _WoundTestStage==5) ? "K_HEAD" : _WoundTestStage==3 ? "K_ABDOMEN" : "K_CHEST",target);
        WeaponClass *weapon=star->Get_Weapon();
        const AmmoDefinitionClass *ammo=weapon ? (const AmmoDefinitionClass *)DefinitionMgrClass::Find_Definition(weapon->Get_Definition()->PrimaryAmmoDefID) : NULL;
        int shots=_WoundTestStage>=6 ? 0 : (_WoundTestStage==2 || _WoundTestStage==4) ? 3 : 1;
        if(_WoundTestStage==5) {
            shots=(int)GetPrivateProfileIntA("Test","CorpseHeadShots",1,test_path);
            if(shots<1) shots=1;if(shots>24) shots=24;
        }
        if(_WoundTestStage<6 && ammo) for(int i=0;i<shots;++i) {
            Vector3 offset(0,0,0.018f*i);
            BulletManager::Create_Bullet(ammo,target+offset-_WoundForward*0.65f,_WoundForward*(float)ammo->Velocity,star,0,target+offset,victim);
        }
        Gore_Test_Log("WOUNDTEST stage %d: real bullets %d, target %.3f %.3f %.3f, ammo %s\n",
            _WoundTestStage,shots,target.X,target.Y,target.Z,ammo?ammo->Get_Name():"none");
    }
    int view=GetPrivateProfileIntA("Test","View",_WoundTestStage,test_path);
    const char *focus=view<=2 || view==5 ? "C HEAD" : view==3 ? "C SPINE1" : "C SPINE2";
    Vector3 target=Bone_Position(victim->Peek_Model(),focus);
    Find_Model_Hit_Box_Center(victim->Peek_Model(),(view<=2 || view==5) ? "K_HEAD" : view==3 ? "K_ABDOMEN" : "K_CHEST",target);
    Matrix3D camera(true);
    if(view==0 || view==6) {
        Vector3 side(-_WoundForward.Y,_WoundForward.X,0);
        target=Bone_Position(victim->Peek_Model(),"C SPINE2")+side*0.5f;
        camera.Look_At(target-_WoundForward*2.1f+Vector3(0,0,0.1f),target,0);
    } else camera.Look_At(target-_WoundForward*0.6f+Vector3(0,0,0.015f),target,0);
    _WoundCamera->Set_Transform(camera);
}

// Normal trigger -> player control -> weapon muzzle/spread -> collision test.
// Unlike WOUNDTEST, this does not inject projectiles or override the weapon target.
static void Aim_Test_Step(void)
{
    _FireTestTrigger=false;
    SoldierGameObj *star=COMBAT_STAR;
    if(!star || !COMBAT_CAMERA || !star->Get_Weapon() || _AimCase>=15) return;
    WeaponClass *weapon=star->Get_Weapon();
    SoldierGameObj *victim=_AimVictim ? (SoldierGameObj *)GameObjManager::Find_PhysicalGameObj(_AimVictim) : NULL;
    if(!victim) {
        if(_AimVictim) { Gore_Test_Log("AIMTEST FAIL: victim vanished\n");_AimCase=15;return; }
        Vector3 origin;star->Get_Position(&origin);
        Vector3 forward=star->Get_Transform().Get_X_Vector();forward.Z=0;forward.Normalize();
        _AimSide=Vector3(-forward.Y,forward.X,0);
        PhysicalGameObj *obj=ObjectLibraryManager::Create_Object("Nod_Minigunner_0");
        if(!obj || !obj->As_SoldierGameObj()) return;
        victim=obj->As_SoldierGameObj();
        Vector3 pos=origin+forward*8.0f,ground,normal;
        if(Cast_Terrain_Ray(pos+Vector3(0,0,2),pos-Vector3(0,0,5),&ground,&normal)) pos.Z=ground.Z+0.05f;
        Matrix3D tm(true);tm.Obj_Look_At(pos,pos-forward,0);
        victim->Set_Transform(tm);victim->Start_Observers();victim->Innate_Disable();
        victim->Set_Loiters_Allowed(false);victim->Set_Animation("S_A_HUMAN.H_A_A0A0",true);
        victim->Peek_Physical_Object()->Set_Immovable(true);
        victim->Get_Defense_Object()->Set_Health_Max(2000);victim->Get_Defense_Object()->Set_Health(2000);
        GameObjManager::Activate_Cinematic_Freeze(true);
        star->Enable_Cinematic_Freeze(false);victim->Enable_Cinematic_Freeze(false);
        CombatManager::Set_First_Person(true);
        _AimVictim=victim->Get_ID();_AimCase=0;_AimCaseTick=GetTickCount();
        _AimRoundStart=weapon->Get_Total_Rounds_Fired();_AimHits=0;_AimBox[0]=0;
        weapon->Set_Clip_Rounds(12);
        Gore_Test_Log("AIMTEST started: normal player weapon %s\n",weapon->Get_Name());
    }
    int difficulty=_AimCase/5, sample=_AimCase%5;
    CombatManager::Set_Difficulty_Level(difficulty);
    // Attempts to reactivate legacy settings must have no effect.
    COMBAT_CAMERA->Enable_Weapon_Help(true);WeaponManager::Set_Weapon_Help_Disabled(false);
    star->Get_Defense_Object()->Set_Health(star->Get_Defense_Object()->Get_Health_Max());
    Vector3 target;
    const char *box=sample==0 ? "K_HEAD" : sample==1 ? "K_ABDOMEN" : "K_CHEST";
    if(!Find_Model_Hit_Box_Center(victim->Peek_Model(),box,target)) return;
    if(sample>=3) target+=_AimSide*(sample==3 ? -0.5f : 0.5f);
    COMBAT_CAMERA->Force_Look(target);
    DWORD elapsed=GetTickCount()-_AimCaseTick;
    int fired=weapon->Get_Total_Rounds_Fired()-_AimRoundStart;
    if(elapsed>1500 && fired==0 && elapsed<4500) _FireTestTrigger=true;
    if((elapsed>2500 && fired>0) || elapsed>5000) {
        Vector3 cam=COMBAT_CAMERA->Get_Transform().Get_Translation();
        Vector3 dir=-COMBAT_CAMERA->Get_Transform().Get_Z_Vector();
        Vector3 offset=star->Get_Targeting_Pos()-cam,perpendicular;
        Vector3::Cross_Product(offset,dir,&perpendicular);
        float error=perpendicular.Length();
        bool assist_off=!COMBAT_CAMERA->Is_Weapon_Help_Enabled() && WeaponManager::Is_Weapon_Help_Disabled();
        bool hit_ok=sample>=3 ? _AimHits==0 : (_AimHits>0 && strstr(_AimBox,box)!=NULL);
        bool passed=assist_off && error<0.02f && fired==1 && hit_ok;
        if(passed) ++_AimPass;else ++_AimFail;
        Gore_Test_Log("AIMTEST %s difficulty %d case %d expected %s fired %d contacts %d box %s ray_error %.6f assist_off %d\n",
            passed?"PASS":"FAIL",difficulty,sample,sample>=3?"miss":box,fired,_AimHits,_AimBox,error,assist_off);
        _FireTestTrigger=false;++_AimCase;
        if(_AimCase>=15) {
            Gore_Test_Log("AIMTEST COMPLETE: %d PASS, %d FAIL\n",_AimPass,_AimFail);
            return;
        }
        _AimCaseTick=GetTickCount();_AimRoundStart=weapon->Get_Total_Rounds_Fired();
        _AimHits=0;_AimBox[0]=0;weapon->Set_Clip_Rounds(12);
    }
}

bool GoreManager::Test_Fire( void )
{
	return (_FireTest || _AimTest) && _FireTestTrigger;
}

void GoreManager::Update( float dt )
{
	if (!_Initialized) {
		return;
	}
	_Time += dt;
    VoxelSkin::Tick(dt);
    Update_Chemical_Melting(dt);
    Update_Thermal_Spread();
    Update_Thermal_Remains();

	if (_AimTest) {
        Aim_Test_Step();
    } else if (_WoundTest) {
        Wound_Test_Step();
    } else if (_FireTest) {
		_GoreTestTimer += dt;
		if (_GoreTestTimer > 0.25f) {
			_GoreTestTimer = 0.0f;
			Fire_Test_Step();
		}
	} else if (_GoreTest) {
		_GoreTestTimer += dt;
		if (_GoreTestTimer > 3.0f) {
			_GoreTestTimer = 0.0f;
			if (Is_Enabled()) {
				Gore_Test_Step();
			} else {
				Gore_Test_Log("gore not enabled here (Enabled=%d, solo play=%d)\n", _Settings.Enabled ? 1 : 0, IS_SOLOPLAY ? 1 : 0);
			}
		}
	}

	/*
	** Timed emitters and pools
	*/
	int i = 0;
	while (i < _TimedCount) {
		GoreTimedStruct & t = _Timed[i];
		if (t.Emitter != NULL && _Time >= t.StopTime) {
			((ParticleEmitterClass *)t.Emitter)->Stop();
		}
		if (_Time >= t.RemoveTime) {
			if (t.Emitter != NULL) {
				t.Emitter->Remove();
				t.Emitter->Release_Ref();
			} else {
				Vector3 hit, normal;
				if (Cast_Terrain_Ray(t.Position + Vector3(0.0f, 0.0f, 0.5f), t.Position - Vector3(0.0f, 0.0f, 2.0f), &hit, &normal)) {
					Blood_Decal(hit, normal, _PoolTextures[FreeRandom.Get_Int(ARRAY_COUNT(_PoolTextures))], t.Size);
				}
			}
			_Timed[i] = _Timed[--_TimedCount];
			continue;
		}
		i++;
	}

	/*
	** Gibs: gravity, spin, bounce off the terrain, settle, expire
	*/
	if (dt > 0.1f) {
		dt = 0.1f;
	}
	i = 0;
	while (i < _GibCount) {
		GoreGibStruct & g = _Gibs[i];
		g.Age += dt;
		if (g.Age > _Settings.GibLifetime) {
			Remove_Gib(i);
			continue;
		}

		// A piece that ever gets a non-finite position or speed is dropped: a NaN transform
		// in the scene breaks the LOD and visibility code
		if (!(_finite(g.Offset.X) && _finite(g.Offset.Y) && _finite(g.Offset.Z) &&
				_finite(g.Velocity.X) && _finite(g.Velocity.Y) && _finite(g.Velocity.Z))) {
			Remove_Gib(i);
			continue;
		}

		if (!g.Resting) {
			g.Velocity.Z -= 9.8f * dt;
			Vector3 from = g.Pivot + g.Offset;
			Vector3 to = from + g.Velocity * dt;

			Vector3 hit, normal;
			if (Cast_Terrain_Ray(from, to + g.Radius * g.Velocity * (1.0f / (g.Velocity.Length() + 0.001f)), &hit, &normal)) {
				to = hit + g.Radius * normal;
				float into = Vector3::Dot_Product(g.Velocity, normal);
				if (into < 0.0f) {
					g.Velocity -= (1.35f * into) * normal;		// restitution 0.35
				}
				g.Velocity *= 0.6f;									// friction
				g.AngularVelocity *= 0.5f;
				if (g.Bounces++ < 2 && _Settings.Blood) {
					Blood_Decal(hit, normal, _SplatTextures[FreeRandom.Get_Int(ARRAY_COUNT(_SplatTextures))], FreeRandom.Get_Float(0.2f, 0.45f));
				}
				if (g.Velocity.Length2() < 0.25f && normal.Z > 0.6f) {
					g.Resting = true;
				}
			}
			g.Offset = to - g.Pivot;

			float spin = g.AngularVelocity.Length();
			if (spin > 0.001f) {
				Quaternion dq = Axis_To_Quat(g.AngularVelocity * (1.0f / spin), spin * dt);
				g.Rotation = Normalize(dq * g.Rotation);
			}

			Matrix3D a = g.BaseTm;
			a.Adjust_Translation(-g.Pivot);
			Matrix3D tm;
			Matrix3D::Multiply(Build_Matrix3D(g.Rotation), a, &tm);
			tm.Adjust_Translation(g.Pivot + g.Offset);
			if (!Is_Finite(tm)) {
				Remove_Gib(i);
				continue;
			}
			// through the scene node, so its cull box follows the piece
			if (g.Wrapper != NULL) {
				g.Wrapper->Set_Transform(tm);
			} else {
				g.Model->Set_Transform(tm);
			}
		}
		i++;
	}
}
