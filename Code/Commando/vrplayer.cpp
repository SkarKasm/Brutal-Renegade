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

/*
** RENEGADE_VR gameplay. See vrplayer.h.
**
** Settings (renegade_vr.ini, [VR] section; all optional):
**
**   TurnMode=0              ; 0 = snap turn, 1 = smooth turn (right stick left/right)
**   SnapTurnAngle=45        ; degrees per snap
**   SmoothTurnSpeed=150     ; degrees per second
**   MoveRelativeTo=0        ; 0 = walk where the head looks, 1 = where the left hand points
**   HandAim=1               ; 1 = aim with the right controller, 0 = with the head/mouse
**   LaserSight=1            ; red beam from the gun to what it hits
**   WristHUD=1              ; mission map, heading, life, armour and ammo on the EVA forearm device
**   WristOffset=0.08,0.0,0.15    ; display centre from the left controller grip (metres; +X back of hand, +Z toward the elbow)
**   WristMapRadius=100      ; map range in metres; screen fits the authored EVA surface
**   WristBack=0.07          ; from the controller grip back to the wrist joint of the arms model (metres)
**   ArmRoll=0               ; turns the left arm model about the forearm (degrees)
**   ManualReload=1          ; 1 = the player's weapons never reload themselves
**   ReloadMode=3            ; A eject, off-hand grip at chest pouch, insert at the gun
**                           ; grip takes a replacement, trigger works the empty action
**   ReloadReach=0.20        ; how close (metres) the left hand must come to the right hand
**   BeltDrop=0.45           ; how far (metres) below the head the left hand must be to take a magazine
**   WeaponOffset=0,0,0      ; extra weapon offset in the hand: forward, left, up (metres)
**
** Controls:
**   left stick move; right stick left/right turn, up jump, down crouch
**   hold right grip + right stick up/down to select next/previous weapon
**   right trigger  fire          left trigger  secondary fire
**   right stick up jump / down crouch; A magazine eject; B pistol slide release
**   X / right click use; Y recenter; dominant grip + vertical stick selects weapons
**   right stick click use; left stick click or left Menu opens the pause panel
**   off-hand grip near measured support point latches support; release to detach
*/

#include "vrplayer.h"
#include "vrmanager.h"
#include "combatgmode.h"
#include "gametype.h"
#include "ww3d.h"
#include "dx8wrapper.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8fvf.h"
#include "vertmaterial.h"
#include "shader.h"
#include "texture.h"
#include "render2d.h"
#include "camera.h"
#include "ccamera.h"
#include "combat.h"
#include "backgroundmgr.h"
#include "soldier.h"
#include "humanphys.h"
#include "weapons.h"
#include "weaponmanager.h"
#include "weaponview.h"
#include "vr_forearm.h"
#include "weaponbag.h"
#include "weaponicons.h"
#include "translatedb.h"
#include "pscene.h"
#include "physcoltest.h"
#include "decophys.h"
#include "radar.h"
#include "mapmgr.h"
#include "objectives.h"
#include "hud.h"
#include "assetmgr.h"
#include "font3d.h"
#include "damage.h"
#include "mesh.h"
#include "meshmdl.h"
#include <vector>
#include "input.h"
#include "vehicle.h"
#include "dialogmgr.h"
#include "rendobj.h"
#include "htree.h"
#include "matrix3d.h"
#include "quat.h"
#include "rect.h"
#include "wwmath.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/*
** Settings
*/
static int					_TurnMode				= 0;
static float				_SnapTurnAngle			= 45.0f;
static float				_SmoothTurnSpeed		= 150.0f;
static int					_MoveRelativeTo		= 0;
static bool					_HandAim					= true;
static bool					_LaserSight				= true;
static bool					_WristHUD				= true;
static Vector3				_WristOffset(0.08f, 0.0f, 0.15f);
static float				_WristHeight			= 0.075f;
static float				_WristBack				= 0.07f;
static float				_ArmRoll					= 0.0f;
static bool					_ManualReload			= true;
static int					_ReloadMode				= 3;
static float				_ReloadReach			= 0.20f;
static float				_BeltDrop				= 0.45f;
static Vector3				_WeaponOffset(0.0f, 0.0f, 0.0f);

/*
** State
*/
static RVR_Frame			_Frame;
static bool					_HaveFrame				= false;
static bool					_InGame					= false;
static RVR_Hand			_Previous[RVR_HAND_COUNT];
static bool					_SnapArmed				= true;
static bool _WheelVisible=false,_WheelValid=false,_WheelBlockRight=false,_WheelBlockLeft=false;
static bool _WheelStickBlocked=false;
static int _WheelPage=0,_WheelHover=-1;
static bool _WheelHasQuickSave=false;
static const unsigned VR_MENU_BUTTONS=RVR_BUTTON_MENU | RVR_BUTTON_STICK;
static std::vector<int> _WheelIDs;
static Matrix3D _WheelTracking(true),_WheelWorld(true);
static TextureClass *_WheelTexture=NULL;
static Render2DTextClass *_WheelText=NULL;
static bool Update_Weapon_Wheel(const RVR_Frame &frame);
static bool					_MagInHand				= false;
static float				_MagTimer				= 0.0f;
static int					_LastRoundsFired		= -1;
static TextureClass *_EvaRadarTexture=NULL;
static TextureClass *	_WristTexture			= NULL;
static TextureClass *_ScopeTexture=NULL;
static IDirect3DSurface8 *_ScopeDepth=NULL;
static CameraClass *_ScopeCamera=NULL;
static Matrix3D _ScopePose(true);
static float _ScopeRadius=.028f;
static bool _ScopeValid=false;
static bool					_WristValid				= false;
static FILE *				_Log						= NULL;

enum { WRIST_TEXTURE_SIZE = 1024 };
static DecorationPhysClass *_PlayerBody=NULL;
static RenderObjClass *_BodySource=NULL;
static StringClass _TestBodyModel;
static bool _BodyIK=true;
static Vector3 _LastBodyForward(1,0,0);

static bool _HaveForearm=false;
static bool _HaveElbowPole[2]={false,false};
static Vector3 _ElbowPole[2];
static bool _SupportLatched=false,_SupportValid=false;
static Vector3 _SupportSpot(0,0,0);
static RenderObjClass *_SupportWeapon=NULL;
static Matrix3D _MagazineRelative(true),_MagazineHandRelative(true);
static Vector3 _MagazineCenter(0,0,0);
static bool _MagazineGeometry=false;
struct VRDroppedMagazine {DecorationPhysClass *Object;Vector3 Velocity;float Life;};
static std::vector<VRDroppedMagazine> _DroppedMagazines;
static TextureClass *_MissionMap=NULL;
static Vector2 _MapSourceSize(512,512);
static StringClass _MissionMapName;
static Render2DTextClass *_EvaText=NULL;
static float _MapRadius=100.0f;
static void Release_Body()
{
    if(_PlayerBody) {
        if(COMBAT_SCENE && COMBAT_SCENE->Contains(_PlayerBody)) COMBAT_SCENE->Remove_Object(_PlayerBody);
        _PlayerBody->Release_Ref();_PlayerBody=NULL;
    }
    _BodySource=NULL;_HaveForearm=false;_HaveElbowPole[0]=_HaveElbowPole[1]=false;
}

static int _ReloadStage=0; // 0 ready, 1 magazine out, 2 replacement held, 3 inserted/action
static WeaponClass *_ReloadWeapon=NULL;
static bool _ReloadWasEmpty=false,_ActionHeld=false,_RequireAction=true;
static Vector3 _ActionStart(0,0,0);
static DecorationPhysClass *_HeldMagazine=NULL;

static void Release_Magazine()
{
    if(_HeldMagazine) {
        if(COMBAT_SCENE && COMBAT_SCENE->Contains(_HeldMagazine)) COMBAT_SCENE->Remove_Object(_HeldMagazine);
        _HeldMagazine->Release_Ref();_HeldMagazine=NULL;
    }
}

static void Release_Dropped_Magazines()
{
    for(unsigned i=0;i<_DroppedMagazines.size();++i){
        DecorationPhysClass *object=_DroppedMagazines[i].Object;
        if(COMBAT_SCENE && COMBAT_SCENE->Contains(object))COMBAT_SCENE->Remove_Object(object);
        object->Release_Ref();
    }
    _DroppedMagazines.clear();
}

static void Reset_Reload()
{
    _ReloadStage=0;_MagInHand=false;_MagTimer=0;_ActionHeld=false;_ReloadWeapon=NULL;
    WeaponClass::VRReloadLockedForPlayer=false;WeaponViewClass::Set_VR_Magazine_Removed(false);Release_Magazine();
    _MagazineGeometry=false;WeaponViewClass::Set_VR_Action_State(0);
}

static void Pose_Magazine()
{
    if(!_MagInHand || !_Frame.Hand[RVR_HAND_LEFT].Active) {Release_Magazine();return;}
    if(!_HeldMagazine) {
        RenderObjClass *model=WeaponViewClass::Clone_VR_Magazine();
        if(!model) return;
        _HeldMagazine=new DecorationPhysClass();_HeldMagazine->Enable_Dont_Save(true);_HeldMagazine->Set_Collision_Group(UNCOLLIDEABLE_GROUP);
        _HeldMagazine->Set_Model(model);model->Release_Ref();COMBAT_SCENE->Add_Dynamic_Object(_HeldMagazine);
    }
    // Model placement is updated below once the tracking-space helpers exist.
}



static void VR_Log(const char * format, ...)
{
	if (_Log == NULL) {
		char path[MAX_PATH];
		VRManagerClass::Get_Exe_Relative_Path("renegade_vr_game.log", path, sizeof(path));
		_Log = fopen(path, "w");
		if (_Log == NULL) {
			return;
		}
	}
	va_list args;
	va_start(args, format);
	vfprintf(_Log, format, args);
	va_end(args);
	fflush(_Log);
}

static float Ini_Float(const char * key, float def, const char * path)
{
	char buffer[64], text[64];
	_snprintf(text, sizeof(text), "%g", def);
	::GetPrivateProfileStringA("VR", key, text, buffer, sizeof(buffer), path);
	return (float)atof(buffer);
}

static Vector3 Ini_Vector3(const char * key, const Vector3 & def, const char * path)
{
	char buffer[96], text[96];
	_snprintf(text, sizeof(text), "%g,%g,%g", def.X, def.Y, def.Z);
	::GetPrivateProfileStringA("VR", key, text, buffer, sizeof(buffer), path);
	Vector3 v = def;
	sscanf(buffer, "%f,%f,%f", &v.X, &v.Y, &v.Z);
	return v;
}



/*
** Poses. Tracking space and a W3D camera's local frame share axes (+Y up, -Z forward),
** so a pose composes with VRManagerClass::Get_Tracking_To_World directly. The result is in
** "camera convention" (-Z forward); objects use X forward, Y left, Z up.
*/
static Matrix3D Pose_World(const RVR_Pose & pose)
{
	Matrix3D local = Build_Matrix3D(Quaternion(pose.Orientation[0], pose.Orientation[1], pose.Orientation[2], pose.Orientation[3]));
	local.Set_Translation(Vector3(pose.Position[0], pose.Position[1], pose.Position[2]));
	Matrix3D world;
	Matrix3D::Multiply(VRManagerClass::Get_Tracking_To_World(), local, &world);
	return world;
}

static Matrix3D To_Object_Convention(const Matrix3D & camera_convention)
{
	// Columns: object X (forward) = camera -Z, object Y (left) = camera -X, object Z (up) = camera Y
	Matrix3D r(true);
	r[0][0] = 0.0f;	r[0][1] = -1.0f;	r[0][2] = 0.0f;
	r[1][0] = 0.0f;	r[1][1] = 0.0f;	r[1][2] = 1.0f;
	r[2][0] = -1.0f;	r[2][1] = 0.0f;	r[2][2] = 0.0f;
	Matrix3D out;
	Matrix3D::Multiply(camera_convention, r, &out);
	return out;
}

static Vector3 Forward(const Matrix3D & camera_convention)
{
	Vector3 f(-camera_convention[0][2], -camera_convention[1][2], -camera_convention[2][2]);
	f.Normalize();
	return f;
}

static Vector3 Pose_Position(const RVR_Pose & pose)
{
	return Vector3(pose.Position[0], pose.Position[1], pose.Position[2]);
}

static SoldierGameObj * On_Foot_Star(void)
{
	SoldierGameObj * star = COMBAT_STAR;
	if (star == NULL || star->Is_Dead() || star->Get_Vehicle() != NULL || star->Peek_Human_Phys() == NULL) {
		return NULL;
	}
	return star;
}


/*
** Hooks the combat code calls
*/
static bool Aim_Ray(Vector3 * start, Vector3 * direction)
{
	if (!_HaveFrame || !_Frame.PoseValid || !_InGame || !_HandAim || !_Frame.Hand[RVR_HAND_RIGHT].Active || !COMBAT_STAR || COMBAT_STAR->Is_Dead()) {
		return false;
	}
    if(COMBAT_STAR->Get_Vehicle()){
        Matrix3D aim=Pose_World(_Frame.Hand[RVR_HAND_RIGHT].Aim);
        *start=Pose_World(_Frame.Hand[RVR_HAND_RIGHT].Grip).Get_Translation();*direction=Forward(aim);return true;
    }
    Matrix3D muzzle;
    if(WeaponViewClass::Get_VR_Muzzle(&muzzle)) {
        *start=muzzle.Get_Translation();*direction=muzzle.Get_X_Vector();direction->Normalize();return true;
    }
	Matrix3D aim = Pose_World(_Frame.Hand[RVR_HAND_RIGHT].Aim);
	*start = aim.Get_Translation();
	*direction = Forward(aim);
	return true;
}

static int Find_Bone(RenderObjClass * model, const char * part)
{
	for (int i = 0; i < model->Get_Num_Bones(); i++) {
		const char * name = model->Get_Bone_Name(i);
		if (name != NULL && strstr(name, part) != NULL) {
			return i;
		}
	}
	return -1;
}

static void Release_Arm_Controls(RenderObjClass *hands)
{
    const char *names[]={"L FOREARM","L HAND","R FOREARM","R HAND"};
    for(int i=0;i<4;++i) {int bone=Find_Bone(hands,names[i]);if(bone>0 && hands->Is_Bone_Captured(bone)) hands->Release_Bone(bone);}
}

static Matrix3D Mul(const Matrix3D & a, const Matrix3D & b)
{
	Matrix3D out;
	Matrix3D::Multiply(a, b, &out);
	return out;
}

static Matrix3D Inverse(const Matrix3D & m)
{
	Matrix3D out;
	m.Get_Orthogonal_Inverse(out);
	return out;
}

// Small folded sleeve rings use the outfit's own elbow UVs and texture atlas.
class VRCuffModelClass : public MeshModelClass
{
public:
    void Finish(Vector2 *uv,int count) {
        CurMatDesc->Install_UV_Array(0,0,uv,count);
        CurMatDesc->Set_DCG_Source(0,VertexMaterialClass::COLOR1);
        unsigned *colors=Get_Color_Array(0);
        for(int i=0;i<count;++i) colors[i]=0xFFB8B8B8;
    }
};

class VRCuffClass : public MeshClass
{
public:
    VRCuffClass(MeshModelClass *outfit,int bone) {
        const int sides=20,rings=5,count=sides*rings;
        const float xs[]={.233f,.238f,.255f,.267f,.271f};
        const float radii[]={.058f,.065f,.068f,.065f,.057f};
        std::vector<Vector3> positions(count);std::vector<Vector2> uv(count);std::vector<TriIndex> faces;
        const Vector3 *source=outfit->Get_Vertex_Array();const Vector2 *source_uv=outfit->Get_UV_Array();
        const uint16 *links=outfit->Get_Vertex_Bone_Links();
        for(int ring=0;ring<rings;++ring) for(int n=0;n<sides;++n) {
            float a=n*6.283185307f/sides;int v=ring*sides+n;
            positions[v]=Vector3(xs[ring],-.012f+cosf(a)*radii[ring],sinf(a)*radii[ring]);
            float best=1e10f;int nearest=-1;
            if(source_uv && links) for(int j=0;j<outfit->Get_Vertex_Count();++j) {
                if(links[j]!=bone) continue;
                Vector3 sample=positions[v];sample.X=.18f;float distance=(source[j]-sample).Length2();
                if(distance<best) {best=distance;nearest=j;}
            }
            uv[v]=nearest>=0 ? source_uv[nearest] : Vector2(n/(float)sides,ring/(float)rings);
            if(ring<rings-1) {
                int next=ring*sides+(n+1)%sides;TriIndex t;
                t.I=v;t.J=next;t.K=v+sides;faces.push_back(t);
                t.I=next;t.J=next+sides;t.K=v+sides;faces.push_back(t);
            }
        }
        Model=NEW_REF(VRCuffModelClass,());Model->Reset((int)faces.size(),count,1);Model->Set_Name("VR_ROLLED_SLEEVE");
        memcpy(Model->Get_Vertex_Array(),&positions[0],count*sizeof(Vector3));
        memcpy(const_cast<TriIndex*>(Model->Get_Polygon_Array()),&faces[0],faces.size()*sizeof(TriIndex));
        ((VRCuffModelClass*)Model)->Finish(&uv[0],count);
        Model->Set_Flag(MeshModelClass::DIRTY_VNORMALS,true);Model->Set_Flag(MeshModelClass::DIRTY_BOUNDS,true);Model->Set_Flag(MeshModelClass::DIRTY_PLANES,true);
        Model->Set_Flag(MeshModelClass::TWO_SIDED,true);
        VertexMaterialClass *material=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
        Model->Set_Single_Material(material);material->Release_Ref();
        Model->Set_Single_Texture(outfit->Peek_Texture(0));
        ShaderClass shader=ShaderClass::_PresetOpaqueShader;shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);Model->Set_Single_Shader(shader);
        Set_Collision_Type(0);
    }
};

static void Add_Sleeve_Cuffs(RenderObjClass *body)
{
    const char *name=body->Get_Name();
    if(!stricmp(name,"c_ag_havoc")) return; // Default Havoc already has bare upper arms.
    MeshModelClass *skin=NULL;
    for(int i=0;i<body->Get_Num_Sub_Objects();++i) {
        RenderObjClass *sub=body->Get_Sub_Object(i);
        if(sub && sub->Class_ID()==RenderObjClass::CLASSID_MESH && strstr(sub->Get_Name(),".BODY_0")) {skin=((MeshClass*)sub)->Get_Model();}
        if(sub) sub->Release_Ref();if(skin) break;
    }
    if(!skin) return;
    for(int side=0;side<2;++side) {
        int upper=Find_Bone(body,side==0?"C L UPPERARM":"C R UPPERARM");if(upper<=0) continue;
        RenderObjClass *cuff=new VRCuffClass(skin,upper);body->Add_Sub_Object_To_Bone(cuff,upper);cuff->Release_Ref();
    }
    VR_Log("body IK: rolled sleeve cuffs sampled from %s\n",name);skin->Release_Ref();
}

// Keep all gameplay collision on the original soldier; this clone is visual only.
static void Hide_Body_Parts(RenderObjClass *model)
{
    const HTreeClass *tree=model->Get_HTree();if(!tree) return;
    int head=Find_Bone(model,"C HEAD");
    int left=Find_Bone(model,"C L FOREARM"),right=Find_Bone(model,"C R FOREARM");
    for(int b=1;b<tree->Num_Pivots();++b) {
        int collapse=-1;
        for(int p=b;p>0;p=tree->Get_Parent_Index(p)) {
            if(p==head || p==left || p==right) {collapse=p;break;}
        }
        const char *name=tree->Get_Bone_Name(b);
        bool gear=name && (strstr(name,"GUNBONE") || strstr(name,"BAG"));
        if(collapse<0 && !gear) continue;
        if(collapse>=0) model->Set_Bone_Skin_Hidden(b,true,collapse);
        for(int i=0;i<model->Get_Num_Sub_Objects_On_Bone(b);++i) {
            RenderObjClass *sub=model->Get_Sub_Object_On_Bone(i,b);
            if(sub) {sub->Set_Hidden(true);sub->Release_Ref();}
        }
    }
}

// X is the authored elbow-to-wrist direction. The wrist has its own control,
// preserving the animated hand/finger grip while the forearm follows the elbow.
static Matrix3D Segment_Pose(const Vector3 &a,const Vector3 &b,const Vector3 &roll_hint)
{
    Vector3 x=b-a;if(x.Length2()<0.000001f) x.Set(1,0,0);x.Normalize();
    Vector3 y=roll_hint-x*Vector3::Dot_Product(x,roll_hint);
    if(y.Length2()<0.00001f) {y=Vector3(0,0,1)-x*x.Z;}
    if(y.Length2()<0.00001f) {y=Vector3(0,1,0)-x*x.Y;}
    y.Normalize();Vector3 z;Vector3::Cross_Product(x,y,&z);
    Matrix3D out(true);
    for(int r=0;r<3;++r) {out[r][0]=x[r];out[r][1]=y[r];out[r][2]=z[r];}
    out.Set_Translation(a);return out;
}

static Vector3 Elbow_Pole(const Matrix3D &body,const Vector3 &axis,int side)
{
    Vector3 pole = body.Rotate_Vector(Vector3(-.20f,side == 0 ? .52f : -.52f,-.83f));
    pole -= axis*Vector3::Dot_Product(axis,pole);
    if (pole.Length2() < .06f) {
        const Vector3 fallback = _HaveElbowPole[side] ? body.Rotate_Vector(_ElbowPole[side]) :
            body.Get_Y_Vector()*(side == 0 ? 1.f : -1.f);
        Vector3 projected = fallback-axis*Vector3::Dot_Product(axis,fallback);
        if (projected.Length2() > .0001f) {
            projected.Normalize();
            if (Vector3::Dot_Product(pole,projected) < 0.f) pole = Vector3(0,0,0);
            pole += projected*(.25f-pole.Length());
        }
    }
    if (pole.Length2() < .0001f)
        pole = body.Get_X_Vector()-axis*Vector3::Dot_Product(axis,body.Get_X_Vector());
    pole.Normalize();
    _ElbowPole[side] = body.Inverse_Rotate_Vector(pole);
    _HaveElbowPole[side] = true;
    return pole;
}

static bool Is_Pistol(WeaponClass *weapon)
{
    if(!weapon)return false;char name[128];lstrcpynA(name,weapon->Get_First_Person_Model_Name(),sizeof(name));
    CharUpperBuffA(name,(DWORD)strlen(name));return strstr(name,"PIST")!=NULL;
}

static void Pose_Player_Arms(RenderObjClass *hands,const Matrix3D &placed)
{
    SoldierGameObj *star=On_Foot_Star();
    if(!_BodyIK || !star || star->Is_On_Ladder() || (COMBAT_CAMERA && COMBAT_CAMERA->Is_Using_Host_Model()) || !_Frame.PoseValid || !_Frame.Hand[0].Active || !_Frame.Hand[1].Active) {
        Release_Body();
        Release_Arm_Controls(hands);
        return;
    }
    RenderObjClass *source=star->Peek_Model();
    if(!source || !source->Get_HTree() || source->Get_Num_Bones()>128 || Find_Bone(source,"C HEAD")<=0) {Release_Body();return;}
    if(_BodySource!=source) {
        Release_Body();RenderObjClass *clone=WW3DAssetManager::Get_Instance()->Create_Render_Obj(_TestBodyModel.Is_Empty()?source->Get_Name():(const char*)_TestBodyModel);if(!clone) return;
        if(clone->Get_Num_Bones()!=source->Get_Num_Bones()) {clone->Release_Ref();return;}
        clone->Set_Hidden(VRManagerClass::Is_Simulated() && strstr(::GetCommandLineA(),"VRBODYHIDE=1")!=NULL);Hide_Body_Parts(clone);Add_Sleeve_Cuffs(clone);
        _PlayerBody=new DecorationPhysClass();_PlayerBody->Enable_Dont_Save(true);_PlayerBody->Set_Collision_Group(UNCOLLIDEABLE_GROUP);
        _PlayerBody->Set_Model(clone);clone->Release_Ref();COMBAT_SCENE->Add_Dynamic_Object(_PlayerBody);_BodySource=source;
        VR_Log("body IK: visual clone %s, %d bones; separate wrist joints; bare Havoc forearms\n",source->Get_Name(),source->Get_Num_Bones());
    }
    RenderObjClass *body=_PlayerBody->Peek_Model();
    Matrix3D relative[128],source_inverse=Inverse(source->Get_Transform());
    for(int b=1;b<source->Get_Num_Bones();++b) relative[b]=Mul(source_inverse,source->Get_Bone_Transform(b));
    HTreeClass neutral(*source->Get_HTree());Matrix3D neutral_root(true);
    for(int b=0;b<neutral.Num_Pivots();++b)neutral.Release_Bone(b);
    neutral.Base_Update(neutral_root);
    int pelvis=Find_Bone(source,"C PELVIS");Vector3 pelvis_shift(0,0,0);
    if(pelvis>0)pelvis_shift=relative[pelvis].Get_Translation()-neutral.Get_Transform(pelvis).Get_Translation();
    for(int b=1;b<source->Get_Num_Bones();++b){
        const char *name=source->Get_Bone_Name(b);
        // Remove gun-facing torso/shoulder twist while retaining the crouch/locomotion hip height.
        if(name && (strstr(name,"SPINE") || strstr(name,"CHEST") || strstr(name,"NECK") ||
                    strstr(name,"CLAV") || strstr(name,"UPPERARM") || b==pelvis)){
            relative[b]=neutral.Get_Transform(b);
            relative[b].Set_Translation(relative[b].Get_Translation()+pelvis_shift);
        }
    }
    Matrix3D root=source->Get_Transform();
    Vector3 head=Pose_World(_Frame.Head).Get_Translation();
    // Follow room-scale lean while leaving the feet/locomotion pose authored.
    Vector3 shift=head-source->Get_Bone_Transform(Find_Bone(source,"C HEAD")).Get_Translation();
    shift.Z=0;float d=shift.Length();if(d>0.35f) shift*=0.35f/d;
    Vector3 body_forward=Forward(Pose_World(_Frame.Head));body_forward.Z=0;
    if(body_forward.Length2()>0.001f) {body_forward.Normalize();_LastBodyForward=body_forward;}
    else body_forward=_LastBodyForward;
    root=Matrix3D(true);root.Rotate_Z(atan2f(body_forward.Y,body_forward.X));
    root.Set_Translation(source->Get_Transform().Get_Translation()+shift-body_forward*0.20f);_PlayerBody->Set_Transform(root);
    for(int b=1;b<body->Get_Num_Bones();++b) {body->Capture_Bone(b);body->Control_Bone_Root_Relative(b,relative[b]);}
    _HaveForearm = false;
    for(int side=0;side<2;++side) {
        bool left=side==0;const RVR_Hand &controller=_Frame.Hand[left?RVR_HAND_LEFT:RVR_HAND_RIGHT];
        int upper=Find_Bone(body,left?"C L UPPERARM":"C R UPPERARM");
        int fore=Find_Bone(hands,left?"L FOREARM":"R FOREARM"),hand=Find_Bone(hands,left?"L HAND":"R HAND");
        const int body_fore = Find_Bone(body,left?"C L FOREARM":"C R FOREARM");
        if(upper<=0 || body_fore<=0 || fore<=0 || hand<=0) continue;
        Matrix3D rest_hand,rest_fore;
        if(!WeaponViewClass::Get_VR_Hand_Pose(hand,&rest_hand) || !WeaponViewClass::Get_VR_Hand_Pose(fore,&rest_fore)) continue;
        const float rest_lower = (rest_hand.Get_Translation()-rest_fore.Get_Translation()).Length();
        const float rest_upper = (neutral.Get_Transform(body_fore).Get_Translation() -
                                  neutral.Get_Transform(upper).Get_Translation()).Length();
        if (rest_lower < .05f || rest_upper < .05f) continue;
        float lower=WWMath::Clamp(rest_lower,.20f,.32f);
        bool support=left && _SupportLatched && !_MagInHand && !_ActionHeld && _ReloadStage==0;
        Vector3 wrist=support ? (Is_Pistol(star->Get_Weapon()) ? _SupportSpot : placed*rest_hand.Get_Translation()) :
            Pose_World(controller.Grip)*Vector3(0,0,_WristBack);
        const Vector3 shoulder = root*relative[upper].Get_Translation();
        Vector3 axis = wrist-shoulder;
        float distance = axis.Length();
        if (distance < .001f) continue;
        axis /= distance;
        // Keep the shoulder seated and share the extension between both segments.
        const float extension = WWMath::Max(0.f,distance-rest_upper-lower+.005f);
        const float upper_length = rest_upper+extension*.45f;
        lower += extension*.55f;
        distance = WWMath::Max(distance,fabsf(upper_length-lower)+.001f);
        const float along = (upper_length*upper_length-lower*lower+distance*distance)/(2.f*distance);
        const float bend = sqrtf(WWMath::Max(0.f,upper_length*upper_length-along*along));
        const Vector3 pole = Elbow_Pole(root,axis,side);
        const Vector3 elbow = shoulder+axis*along+pole*bend;
        const Vector3 hinge = Vector3::Cross_Product(axis,pole);
        Matrix3D arm = Segment_Pose(shoulder,elbow,hinge);
        Matrix3D forearm = Segment_Pose(elbow,wrist,hinge);
        Matrix3D wrist_pose=Mul(placed,rest_hand);
        if(left && !support) {
            // Calibrate the actual finger/thumb directions, not the forearm's bend axis.
            // A forearm-to-hand delta alone put the free palm facing forwards.
            Matrix3D finger,thumb;int fi=Find_Bone(hands,"L FINGER2"),th=Find_Bone(hands,"L FINGER1");
            Matrix3D anatomy=rest_hand;anatomy.Set_Translation(Vector3(0,0,0));
            if(fi>0 && th>0 && WeaponViewClass::Get_VR_Hand_Pose(fi,&finger) && WeaponViewClass::Get_VR_Hand_Pose(th,&thumb)){
                Vector3 x=finger.Get_Translation()-rest_hand.Get_Translation();
                Vector3 z=thumb.Get_Translation()-finger.Get_Translation();
                if(x.Length2()>.00001f){x.Normalize();z-=x*Vector3::Dot_Product(x,z);
                    if(z.Length2()>.00001f){z.Normalize();Vector3 y;Vector3::Cross_Product(z,x,&y);
                        for(int r=0;r<3;++r){anatomy[r][0]=x[r];anatomy[r][1]=y[r];anatomy[r][2]=z[r];}}}
            }
            Matrix3D grip=To_Object_Convention(Pose_World(controller.Grip));
            wrist_pose=Mul(Mul(grip,Inverse(anatomy)),rest_hand);
        }
        wrist_pose.Set_Translation(wrist);
        // Derive a bounded roll from this pose, independently of prior controller motion.
        const Vector3 grip_roll = Mul(wrist_pose,Mul(Inverse(rest_hand),rest_fore)).Get_Y_Vector();
        const float twist = DEG_TO_RADF(23.f)*Vector3::Dot_Product(grip_roll,forearm.Get_Z_Vector());
        const float roll = WWMath::Clamp(twist+(left ? DEG_TO_RADF(_ArmRoll) : 0.f),-.48f,.48f);
        arm.Rotate_X(roll*.40f);
        forearm.Rotate_X(roll);
        const float upper_scale = (elbow-shoulder).Length()/rest_upper;
        const float forearm_scale = (wrist-elbow).Length()/rest_lower;
        for (int row=0;row<3;++row) {
            arm[row][0] *= upper_scale;
            forearm[row][0] *= forearm_scale;
        }
        const Matrix3D world_to_body = Inverse(root);
        body->Control_Bone_Root_Relative(upper,Mul(world_to_body,arm));
        body->Control_Bone_Root_Relative(body_fore,Mul(world_to_body,forearm));
        hands->Capture_Bone(fore);hands->Control_Bone_Root_Relative(fore,Mul(Inverse(placed),forearm));
        hands->Capture_Bone(hand);hands->Control_Bone_Root_Relative(hand,Mul(Inverse(placed),wrist_pose));
        if(VRManagerClass::Is_Simulated()) {
            hands->Set_Transform(placed);
            static unsigned audit_counter=0;
            if((audit_counter++%120)<2) {
                float wrist_error=(hands->Get_Bone_Transform(hand).Get_Translation()-wrist).Length();
                float elbow_error=(hands->Get_Bone_Transform(fore).Get_Translation()-body->Get_Bone_Transform(body_fore).Get_Translation()).Length();
                VR_Log("IK_CHECK %s side %d wrist_error %.6f elbow_error %.6f upper %.3f lower %.3f %s\n",body->Get_Name(),side,wrist_error,elbow_error,(elbow-shoulder).Length(),(wrist-elbow).Length(),wrist_error<.001f && elbow_error<.001f ? "PASS":"FAIL");
            }
        }
        if(left) _HaveForearm=true;
    }
}

/*
** The first-person hands + weapon model goes in the right hand: rotated with the controller's
** aim ray and moved so that the model's right wrist lands on the controller.
*/
static bool Hand_Transform(RenderObjClass * hands, RenderObjClass * weapon, Matrix3D * tm)
{
	if (!_HaveFrame || !_Frame.PoseValid || !_InGame || !_HandAim || !_Frame.Hand[RVR_HAND_RIGHT].Active) {
        Release_Body();Release_Arm_Controls(hands);
		return false;
	}
	const RVR_Hand & right = _Frame.Hand[RVR_HAND_RIGHT];
	Matrix3D placed = To_Object_Convention(Pose_World(right.Aim));
	Vector3 grip = Pose_World(right.Grip) * Vector3(0.0f, 0.0f, _WristBack);

	/*
	** Where the model's right hand is, in the model's own space
	*/
	static RenderObjClass * logged_model = NULL;
	Vector3 anchor(0.0f, 0.0f, 0.0f);
	const char * anchor_name = "origin";
	int bone = Find_Bone(hands, "R HAND");
	if (bone >= 0) {
		anchor_name = hands->Get_Bone_Name(bone);
	}
	Vector3 world_anchor;
	if (bone >= 0) {
		Matrix3D authored;
        if(WeaponViewClass::Get_VR_Hand_Pose(bone,&authored)) world_anchor=hands->Get_Transform()*authored.Get_Translation();
        else world_anchor = hands->Get_Bone_Transform(bone).Get_Translation();
	} else if (weapon != NULL) {
		world_anchor = weapon->Get_Transform().Get_Translation();
		anchor_name = "weapon origin";
	} else {
		world_anchor = hands->Get_Transform().Get_Translation();
	}
	Matrix3D::Inverse_Transform_Vector(hands->Get_Transform(), world_anchor, &anchor);

	if (logged_model != hands) {
		logged_model = hands;
		VR_Log("hands model '%s', %d bones, anchor %s at (%.3f %.3f %.3f)\n", hands->Get_Name(), hands->Get_Num_Bones(),
			anchor_name, anchor.X, anchor.Y, anchor.Z);
		for (int i = 0; i < hands->Get_Num_Bones(); i++) {
			VR_Log("  bone %d %s\n", i, hands->Get_Bone_Name(i));
		}
	}

    if(_SupportWeapon!=weapon){_SupportWeapon=weapon;_SupportLatched=false;_SupportValid=false;}
    if(_SupportLatched && !_MagInHand && !_ActionHeld && _ReloadStage==0){
        Vector3 target=Pose_World(_Frame.Hand[RVR_HAND_LEFT].Grip).Get_Translation();
        Vector3 direction=target-grip;
        // Pistols can be supported with close palms without noisy short-baseline
        // aim. Long weapons use both tracked positions and dominant-hand roll.
        if(!Is_Pistol(COMBAT_STAR?COMBAT_STAR->Get_Weapon():NULL) && direction.Length()>.12f){
            direction.Normalize();Vector3 old=placed.Get_X_Vector(),up=placed.Get_Z_Vector();
            Vector3 reference=Vector3(0,0,1)-old*old.Z;
            if(reference.Length2()>.001f){reference.Normalize();Vector3 crossed;Vector3::Cross_Product(reference,up,&crossed);
                float roll=atan2f(Vector3::Dot_Product(crossed,old),Vector3::Dot_Product(reference,up));
                Vector3 z=Vector3(0,0,1)-direction*direction.Z;
                if(z.Length2()>.001f){z.Normalize();Vector3 y;Vector3::Cross_Product(z,direction,&y);
                    Vector3 rolled=z*cosf(roll)-y*sinf(roll);Vector3::Cross_Product(rolled,direction,&y);
                    for(int r=0;r<3;++r){placed[r][0]=direction[r];placed[r][1]=y[r];placed[r][2]=rolled[r];}}
            }
        }
    }
	placed.Set_Translation(grip + placed.Rotate_Vector(_WeaponOffset - anchor));
    Matrix3D support_pose;int support_bone=Find_Bone(hands,"L HAND");
    _SupportValid=support_bone>0 && WeaponViewClass::Get_VR_Hand_Pose(support_bone,&support_pose);
    if(_SupportValid)_SupportSpot=placed*support_pose.Get_Translation();
    if(Is_Pistol(COMBAT_STAR?COMBAT_STAR->Get_Weapon():NULL)){
        _SupportValid=true;_SupportSpot=grip+placed.Rotate_Vector(Vector3(-.025f,.035f,-.025f));
    }
	Pose_Player_Arms(hands, placed);
	*tm = placed;
	return true;
}


void VRPlayerClass::Init(const char * ini_path)
{
    Release_Body();_TestBodyModel="";
	_TurnMode = ::GetPrivateProfileIntA("VR", "TurnMode", 0, ini_path);
	_SnapTurnAngle = Ini_Float("SnapTurnAngle", 45.0f, ini_path);
	_SmoothTurnSpeed = Ini_Float("SmoothTurnSpeed", 150.0f, ini_path);
	_MoveRelativeTo = ::GetPrivateProfileIntA("VR", "MoveRelativeTo", 0, ini_path);
	_HandAim = ::GetPrivateProfileIntA("VR", "HandAim", 1, ini_path) != 0;
	_LaserSight = ::GetPrivateProfileIntA("VR", "LaserSight", 1, ini_path) != 0;
	_WristHUD = ::GetPrivateProfileIntA("VR", "WristHUD", 1, ini_path) != 0;
	_WristOffset = Ini_Vector3("WristOffset", _WristOffset, ini_path);
	_WristHeight = Ini_Float("WristHeight", 0.075f, ini_path);
    _BodyIK=::GetPrivateProfileIntA("VR","BodyIK",1,ini_path)!=0;
    _MapRadius=WWMath::Clamp(Ini_Float("WristMapRadius",100,ini_path),10.0f,200.0f);
	_WristBack = Ini_Float("WristBack", 0.07f, ini_path);
	_ArmRoll = Ini_Float("ArmRoll", _ArmRoll, ini_path);
	_ManualReload = ::GetPrivateProfileIntA("VR", "ManualReload", 1, ini_path) != 0;
	_ReloadMode = ::GetPrivateProfileIntA("VR", "ReloadMode", 3, ini_path);
	_ReloadReach = Ini_Float("ReloadReach", 0.20f, ini_path);
	_BeltDrop = Ini_Float("BeltDrop", 0.45f, ini_path);
    _RequireAction=::GetPrivateProfileIntA("VR","ReloadAction",1,ini_path)!=0;
    Reset_Reload();
	_WeaponOffset = Ini_Vector3("WeaponOffset", _WeaponOffset, ini_path);

	memset(&_Frame, 0, sizeof(_Frame));
	memset(_Previous, 0, sizeof(_Previous));
	_HaveFrame = false;

	CCameraClass::Set_VR_Aim_Ray(&Aim_Ray);
	WeaponViewClass::Set_VR_Hand_Transform(&Hand_Transform);
	WeaponClass::ManualReloadForPlayer = _ManualReload;

	VR_Log("VR player: turn %s, hand aim %d, wrist HUD %d, manual reload %d (mode %d)\n",
		_TurnMode ? "smooth" : "snap", _HandAim, _WristHUD, _ManualReload, _ReloadMode);
}


void VRPlayerClass::Shutdown(void)
{
    Release_Dropped_Magazines();_SupportLatched=false;_SupportValid=false;_SupportWeapon=NULL;
    Release_Body();
    Reset_Reload();
	CCameraClass::Set_VR_Aim_Ray(NULL);
	WeaponViewClass::Set_VR_Hand_Transform(NULL);
	WeaponClass::ManualReloadForPlayer = false;
	Input::Clear_VR_Values();
	Release_Surfaces();
	if (_Log != NULL) {
		fclose(_Log);
		_Log = NULL;
	}
}


void VRPlayerClass::Release_Surfaces(void)
{
	REF_PTR_RELEASE(_WristTexture);
    REF_PTR_RELEASE(_WheelTexture);delete _WheelText;_WheelText=NULL;_WheelVisible=_WheelValid=false;_WheelIDs.clear();
    REF_PTR_RELEASE(_ScopeTexture);REF_PTR_RELEASE(_ScopeCamera);
    if(_ScopeDepth){_ScopeDepth->Release();_ScopeDepth=NULL;}_ScopeValid=false;
    REF_PTR_RELEASE(_EvaRadarTexture);
    REF_PTR_RELEASE(_MissionMap);_MissionMapName="";
    delete _EvaText;_EvaText=NULL;
	_WristValid = false;
}


bool VRPlayerClass::Has_Visible_Body() {return _PlayerBody!=NULL;}

void VRPlayerClass::Set_Test_Body_Model(const char *name)
{
    if(!VRManagerClass::Is_Simulated()) return;
    if(_TestBodyModel!=name) {_TestBodyModel=name;Release_Body();VR_Log("body audit outfit %s\n",name);}
}

void VRPlayerClass::Set_Arm_Roll(float degrees)
{
	_ArmRoll = degrees;
}

bool VRPlayerClass::Is_Holding_Magazine(void)
{
	return _MagInHand;
}


static void Start_Reload(WeaponClass * weapon)
{
    int kind=WeaponViewClass::Get_VR_Reload_Kind();
    weapon->Force_Reload();
    if(!weapon->Is_Reloading()) return;
    weapon->Set_VR_Reload_Limit(kind==2 ? 1 : 0);
    Reset_Reload();
	VRManagerClass::Vibrate(RVR_HAND_LEFT, 0.6f, 0.08f);
	VRManagerClass::Vibrate(RVR_HAND_RIGHT, 0.6f, 0.08f);
	VR_Log("reload: %s clip %d, inventory %d\n", weapon->Get_Name(), weapon->Get_Clip_Rounds(), weapon->Get_Inventory_Rounds());
}


/*
** Manual reload. Returns nothing; sends the reload input when the gesture completes.
*/
static void Capture_Magazine_Geometry()
{
    _MagazineGeometry=WeaponViewClass::Get_VR_Magazine_Geometry(&_MagazineRelative,&_MagazineCenter);
    if(!_MagazineGeometry){_MagazineRelative=Matrix3D(true);_MagazineCenter.Set(0,0,0);}
    Matrix3D gun;if(WeaponViewClass::Get_VR_Gun_Frame(&gun)){
        Matrix3D hand=To_Object_Convention(Pose_World(_Frame.Hand[RVR_HAND_RIGHT].Aim));
        _MagazineHandRelative=Mul(Mul(Inverse(hand),gun),_MagazineRelative);_MagazineHandRelative.Set_Translation(Vector3(0,0,0));
    }else _MagazineHandRelative=_MagazineRelative;
}
static Matrix3D Magazine_In_Hand(const Vector3 &feed)
{
    Matrix3D hand=To_Object_Convention(Pose_World(_Frame.Hand[RVR_HAND_LEFT].Aim));
    Vector3 palm=Pose_World(_Frame.Hand[RVR_HAND_LEFT].Grip).Get_Translation();
    Matrix3D held=Mul(hand,_MagazineHandRelative),gun;
    if(WeaponViewClass::Get_VR_Gun_Frame(&gun)){
        Matrix3D seated=Mul(gun,_MagazineRelative);
        float blend=WWMath::Clamp((.30f-(feed-palm).Length())/.20f,0.0f,1.0f);
        Quaternion q;Slerp(q,Build_Quaternion(held),Build_Quaternion(seated),blend);held=Build_Matrix3D(q);
    }
    held.Set_Translation(palm-held.Rotate_Vector(_MagazineCenter));return held;
}
static void Drop_Magazine(const Vector3 &feed)
{
    if(!COMBAT_SCENE)return;
    if(_DroppedMagazines.size()>=8){DecorationPhysClass *old=_DroppedMagazines[0].Object;
        if(COMBAT_SCENE->Contains(old))COMBAT_SCENE->Remove_Object(old);
        old->Release_Ref();_DroppedMagazines.erase(_DroppedMagazines.begin());}
    RenderObjClass *model=WeaponViewClass::Clone_VR_Magazine();if(!model)return;
    DecorationPhysClass *object=new DecorationPhysClass();object->Enable_Dont_Save(true);
    object->Set_Collision_Group(UNCOLLIDEABLE_GROUP);object->Set_Model(model);model->Release_Ref();
    Matrix3D gun(true);WeaponViewClass::Get_VR_Gun_Frame(&gun);Matrix3D pose=Mul(gun,_MagazineRelative);
    pose.Set_Translation(feed-pose.Rotate_Vector(_MagazineCenter));object->Set_Transform(pose);COMBAT_SCENE->Add_Dynamic_Object(object);
    VRDroppedMagazine drop;drop.Object=object;drop.Velocity=gun.Rotate_Vector(Vector3(-.65f,0,0));drop.Life=2.0f;_DroppedMagazines.push_back(drop);
}
static void Update_Dropped_Magazines(float dt)
{
    for(int i=(int)_DroppedMagazines.size()-1;i>=0;--i){VRDroppedMagazine &drop=_DroppedMagazines[i];drop.Life-=dt;
        if(drop.Life<=0){if(COMBAT_SCENE && COMBAT_SCENE->Contains(drop.Object))COMBAT_SCENE->Remove_Object(drop.Object);
            drop.Object->Release_Ref();_DroppedMagazines.erase(_DroppedMagazines.begin()+i);continue;}
        drop.Velocity.Z-=9.81f*dt;Matrix3D pose=drop.Object->Get_Transform();pose.Set_Translation(pose.Get_Translation()+drop.Velocity*dt);drop.Object->Set_Transform(pose);}
}
static void Finish_Physical_Reload(WeaponClass *weapon)
{
    Reset_Reload();_ReloadWeapon=weapon;
    VRManagerClass::Vibrate(RVR_HAND_LEFT,.6f,.06f);VRManagerClass::Vibrate(RVR_HAND_RIGHT,.6f,.06f);
}
static void Update_Reload(const RVR_Frame &frame,float dt)
{
    SoldierGameObj *star=On_Foot_Star();WeaponClass *weapon=star?star->Get_Weapon():NULL;
    if(_ReloadWeapon!=weapon){Reset_Reload();_ReloadWeapon=weapon;}
    if(!weapon || !weapon->Get_Definition())return;
    const RVR_Hand &left=frame.Hand[RVR_HAND_LEFT],&right=frame.Hand[RVR_HAND_RIGHT];if(!left.Active || !right.Active)return;
    bool grip=left.Squeeze>.6f,grip_pressed=grip && _Previous[RVR_HAND_LEFT].Squeeze<=.6f;
    bool eject=(right.Buttons&RVR_BUTTON_PRIMARY) && !(_Previous[RVR_HAND_RIGHT].Buttons&RVR_BUTTON_PRIMARY);
    bool slide_release=(right.Buttons&RVR_BUTTON_SECONDARY) && !(_Previous[RVR_HAND_RIGHT].Buttons&RVR_BUTTON_SECONDARY);
    int kind=WeaponViewClass::Get_VR_Reload_Kind();if(kind==0)return;Vector3 feed,action;
    if(!WeaponViewClass::Get_VR_Reload_Points(&feed,&action))return;
    Vector3 palm=Pose_World(left.Grip).Get_Translation();Matrix3D head=Pose_World(frame.Head);Vector3 forward=Forward(head);forward.Z=0;
    if(forward.Length2()>.0001f)forward.Normalize();
    Vector3 pouch=head.Get_Translation()+forward*.12f-Vector3(0,0,_BeltDrop);
    bool at_pouch=(palm-pouch).Length()<.35f,at_gun=(palm-feed).Length()<_ReloadReach;
    if(kind!=1){
        if(eject && weapon->Is_Reload_OK() && !weapon->Is_Reloading())Start_Reload(weapon);
        else if(grip_pressed && at_pouch && weapon->Is_Reload_OK()){_MagInHand=true;_MagTimer=0;Capture_Magazine_Geometry();}
        else if(_MagInHand && !grip){_MagInHand=false;Release_Magazine();}
        else if(_MagInHand && at_gun){_MagInHand=false;Start_Reload(weapon);}
        return;
    }
    if(weapon->Is_Reloading())return;
    // A ejects even with a full magazine, no reserve or the off hand elsewhere.
    if(eject && _ReloadStage==0){
        Capture_Magazine_Geometry();Drop_Magazine(feed);int clip=weapon->Get_Clip_Rounds();_ReloadWasEmpty=clip<=0;
        int chamber=clip>0?1:0,returned=WWMath::Max(0,clip-chamber);
        if(weapon->Get_Inventory_Rounds()>=0)weapon->Set_Inventory_Rounds(weapon->Get_Inventory_Rounds()+returned);
        weapon->Set_Clip_Rounds(chamber);_ReloadStage=1;_SupportLatched=false;_MagInHand=false;_ActionHeld=false;
        WeaponViewClass::Set_VR_Magazine_Removed(true);VRManagerClass::Vibrate(RVR_HAND_RIGHT,.45f,.05f);
        VR_Log("reload: A eject; chamber %d reserve %d\n",chamber,weapon->Get_Inventory_Rounds());
    }
    if(_ReloadStage==1 && grip_pressed && at_pouch && weapon->Is_Reload_OK()){
        _ReloadStage=2;_MagInHand=true;VRManagerClass::Vibrate(RVR_HAND_LEFT,.4f,.05f);
    }else if(_ReloadStage==2){
        if(!grip){_ReloadStage=1;_MagInHand=false;Release_Magazine();}
        else if(at_gun){
            _ReloadWasEmpty=weapon->Get_Clip_Rounds()==0;weapon->Do_Reload();weapon->Play_Reload_Sound();
            _MagInHand=false;Release_Magazine();WeaponViewClass::Set_VR_Magazine_Removed(false);
            bool has_action=WeaponViewClass::Has_VR_Manual_Action(); // never demand an invisible stock handle
            if(_ReloadWasEmpty && _RequireAction && has_action){_ReloadStage=3;_ActionHeld=false;}else Finish_Physical_Reload(weapon);
            VRManagerClass::Vibrate(RVR_HAND_RIGHT,.55f,.06f);
        }
    }else if(_ReloadStage==3){
        if(slide_release && Is_Pistol(weapon))Finish_Physical_Reload(weapon);
        else{
            bool trigger=left.Trigger>.5f,pressed=trigger && _Previous[RVR_HAND_LEFT].Trigger<=.5f;
            Matrix3D gun;bool have=WeaponViewClass::Get_VR_Gun_Frame(&gun);
            if(pressed && (palm-action).Length()<.14f && have){Matrix3D::Inverse_Transform_Vector(gun,palm,&_ActionStart);_ActionHeld=true;}
            if(!trigger){_ActionHeld=false;WeaponViewClass::Set_VR_Action_Progress(0);}
            if(_ActionHeld && have){Vector3 local;Matrix3D::Inverse_Transform_Vector(gun,palm,&local);
                WeaponViewClass::Set_VR_Action_Progress((local.Y-_ActionStart.Y)/.038f);
                if(local.Y-_ActionStart.Y>=.038f*.6f)Finish_Physical_Reload(weapon);}
        }
    }
    WeaponViewClass::Set_VR_Action_State(_ReloadStage);
    WeaponClass::VRReloadLockedForPlayer=_ReloadStage==3 || ((_ReloadStage==1 || _ReloadStage==2) && weapon->Get_Clip_Rounds()==0);
}


void VRPlayerClass::Update(const RVR_Frame * frame, bool in_game, float dt)
{
	_Frame = *frame;
	_HaveFrame = true;
	_InGame = in_game;

	Input::Clear_VR_Values();
    const RVR_Hand &menu_hand = frame->Hand[RVR_HAND_LEFT];
    if (in_game && frame->PoseValid && menu_hand.Active &&
        (menu_hand.Buttons & VR_MENU_BUTTONS & ~_Previous[RVR_HAND_LEFT].Buttons)) {
        Input::Set_VR_Value(INPUT_FUNCTION_MENU_TOGGLE, 1.0f);
        _WheelVisible=false;
        memcpy(_Previous, frame->Hand, sizeof(_Previous));
        return;
    }
	SoldierGameObj * star = COMBAT_STAR;
	CCameraClass * camera = COMBAT_CAMERA;
	if (!in_game || !frame->PoseValid || star == NULL || camera == NULL) {
        _WheelVisible=false;
        Release_Body();
        _SupportLatched=false;_SupportValid=false;Release_Dropped_Magazines();
        if(!star || star->Is_Dead() || !On_Foot_Star())Reset_Reload();
		_LastRoundsFired = -1;
		memcpy(_Previous, frame->Hand, sizeof(_Previous));
		return;
	}

	if(!On_Foot_Star() || !frame->Hand[RVR_HAND_LEFT].Active || !frame->Hand[RVR_HAND_RIGHT].Active || star->Is_On_Ladder() || camera->Is_Using_Host_Model()) Release_Body();
	const RVR_Hand & left = frame->Hand[RVR_HAND_LEFT];
	const RVR_Hand & right = frame->Hand[RVR_HAND_RIGHT];
	const RVR_Hand & prev_left = _Previous[RVR_HAND_LEFT];
    if(CombatManager::Is_Game_Paused()) {
        _WheelVisible=false;
        _SupportLatched=false;memcpy(_Previous,frame->Hand,sizeof(_Previous));return;
    }

	// The headset is the player's eyes: no third-person camera on foot
	if (!CombatManager::Is_First_Person()) {
		CombatManager::Set_First_Person(true);
	}

	/*
	** Walking: the stick is read relative to the head (or the left hand), then turned into
	** the soldier's own forward/left, since he faces wherever the gun points.
	*/
	bool wheel_input=Update_Weapon_Wheel(*frame);
    bool wheel_stick_input=wheel_input || _WheelStickBlocked;
    float sx = (WWMath::Fabs(left.StickX) > 0.15f) ? left.StickX : 0.0f;
	float sy = (WWMath::Fabs(left.StickY) > 0.15f) ? left.StickY : 0.0f;
	if(star->Get_Vehicle()){
        Input::Set_VR_Value(sy>=0?INPUT_FUNCTION_MOVE_FORWARD:INPUT_FUNCTION_MOVE_BACKWARD,fabsf(sy));
        Input::Set_VR_Value(sx>=0?INPUT_FUNCTION_VEHICLE_TURN_RIGHT:INPUT_FUNCTION_VEHICLE_TURN_LEFT,fabsf(sx));
    }else if(star->Is_On_Ladder()){
        Input::Set_VR_Value(sy>=0?INPUT_FUNCTION_MOVE_FORWARD:INPUT_FUNCTION_MOVE_BACKWARD,fabsf(sy));
    }else if ((sx != 0.0f || sy != 0.0f) && star->Peek_Human_Phys() != NULL) {
		const RVR_Pose & basis_pose = (_MoveRelativeTo == 1 && left.Active) ? left.Aim : frame->Head;
		Vector3 fwd = Forward(Pose_World(basis_pose));
		fwd.Z = 0.0f;
		if (fwd.Length2() < 0.0001f) {
			fwd = Forward(VRManagerClass::Get_Tracking_To_World());
			fwd.Z = 0.0f;
		}
		fwd.Normalize();
		Vector3 right_dir(fwd.Y, -fwd.X, 0.0f);
		Vector3 want = sy * fwd + sx * right_dir;

		float heading = star->Peek_Human_Phys()->Get_Heading();
		Vector3 soldier_fwd(WWMath::Cos(heading), WWMath::Sin(heading), 0.0f);
		Vector3 soldier_left(-soldier_fwd.Y, soldier_fwd.X, 0.0f);
		float f = Vector3::Dot_Product(want, soldier_fwd);
		float l = Vector3::Dot_Product(want, soldier_left);
		Input::Set_VR_Value(f > 0.0f ? INPUT_FUNCTION_MOVE_FORWARD : INPUT_FUNCTION_MOVE_BACKWARD, WWMath::Fabs(f));
		Input::Set_VR_Value(l > 0.0f ? INPUT_FUNCTION_MOVE_LEFT : INPUT_FUNCTION_MOVE_RIGHT, WWMath::Fabs(l));
	}

	/*
	** Turning (the game camera's heading is the play space's heading)
	*/
	if (!wheel_stick_input && _TurnMode == 1) {
		if (WWMath::Fabs(right.StickX) > 0.2f) {
			camera->Force_Heading(camera->Get_Heading() - right.StickX * DEG_TO_RADF(_SmoothTurnSpeed) * dt);
		}
	} else if(!wheel_stick_input) {
		if (_SnapArmed && WWMath::Fabs(right.StickX) > 0.7f) {
			float angle = DEG_TO_RADF(_SnapTurnAngle) * ((right.StickX > 0.0f) ? -1.0f : 1.0f);
			camera->Force_Heading(camera->Get_Heading() + angle);
			_SnapArmed = false;
		} else if (WWMath::Fabs(right.StickX) < 0.3f) {
			_SnapArmed = true;
		}
	}

    // Right grip owns the radial selector; unmodified stick remains jump/crouch.
    if(!wheel_stick_input && !star->Get_Vehicle() && right.StickY>.7f && _Previous[RVR_HAND_RIGHT].StickY<=.7f)
        Input::Set_VR_Value(INPUT_FUNCTION_JUMP,1.0f);
    if(!wheel_stick_input && !star->Get_Vehicle() && right.StickY<-.7f)Input::Set_VR_Value(INPUT_FUNCTION_CROUCH,1.0f);

	/*
	** Buttons
	*/
	if (!wheel_input && !_WheelBlockRight && right.Trigger > 0.5f) Input::Set_VR_Value(INPUT_FUNCTION_FIRE_WEAPON_PRIMARY, 1.0f);
	if (!wheel_input && !_WheelBlockLeft && left.Trigger > 0.5f && _ReloadStage==0 && !_MagInHand && !_ActionHeld) {
        WeaponClass *active=COMBAT_STAR?COMBAT_STAR->Get_Weapon():NULL;
        if(active && (active->Get_Style()==WEAPON_HOLD_STYLE_C4 || active->Get_Can_Snipe())) {
            if(_Previous[RVR_HAND_LEFT].Trigger<=.5f)Input::Queue_VR_Weapon_Use();
        }else Input::Set_VR_Value(INPUT_FUNCTION_FIRE_WEAPON_SECONDARY,1.0f);
    }
	if (!wheel_input && (((left.Buttons&RVR_BUTTON_PRIMARY) && !(prev_left.Buttons&RVR_BUTTON_PRIMARY)) ||
        ((right.Buttons&RVR_BUTTON_STICK) && !(_Previous[RVR_HAND_RIGHT].Buttons&RVR_BUTTON_STICK))))
        Input::Queue_VR_Action();
	if ((left.Buttons & RVR_BUTTON_SECONDARY) && !(prev_left.Buttons & RVR_BUTTON_SECONDARY)) VRManagerClass::Recenter();

	/*
	** Feel the gun
	*/
	WeaponClass * weapon = star->Get_Vehicle() ? star->Get_Vehicle()->Get_Weapon() : (On_Foot_Star() ? star->Get_Weapon() : NULL);
	if (weapon != NULL) {
		int fired = weapon->Get_Total_Rounds_Fired();
		if (_LastRoundsFired >= 0 && fired > _LastRoundsFired) {
			VRManagerClass::Vibrate(RVR_HAND_RIGHT, 0.35f, 0.04f);
		}
		_LastRoundsFired = fired;
		bool trigger_pressed = right.Trigger > 0.5f && !(_Previous[RVR_HAND_RIGHT].Trigger > 0.5f);
		if (!wheel_input && trigger_pressed && weapon->Get_Clip_Rounds() == 0) {
			VRManagerClass::Vibrate(RVR_HAND_RIGHT, 0.15f, 0.02f);		// empty
		}
	} else {
		_LastRoundsFired = -1;
	}

    if(!wheel_input && _ManualReload && On_Foot_Star()) Update_Reload(*frame,dt);
    else if(!On_Foot_Star())Reset_Reload();
    bool grip_pressed=left.Squeeze>.6f && prev_left.Squeeze<=.6f;
    if(wheel_input || left.Squeeze<=.6f || !left.Active || !right.Active || _MagInHand || _ActionHeld || _ReloadStage!=0 || !On_Foot_Star())_SupportLatched=false;
    if(COMBAT_STAR && COMBAT_STAR->Get_Weapon() && (COMBAT_STAR->Get_Weapon()->Get_Style()==WEAPON_HOLD_STYLE_C4 || COMBAT_STAR->Get_Weapon()->Get_Style()==WEAPON_HOLD_STYLE_BEACON))_SupportLatched=false;
    else if(!wheel_input && grip_pressed && _SupportValid && (Pose_World(left.Grip).Get_Translation()-_SupportSpot).Length()<.14f)_SupportLatched=true;
    Pose_Magazine();
    if(_HeldMagazine){Vector3 feed,action;if(WeaponViewClass::Get_VR_Reload_Points(&feed,&action))
        _HeldMagazine->Set_Transform(Magazine_In_Hand(feed));}
    Update_Dropped_Magazines(dt);

	memcpy(_Previous, frame->Hand, sizeof(_Previous));
}


// Compact EVA layout. World-to-map conversion is identical to MapCtrlClass.
static const RectClass EVA_ATLAS(0,0,768,384);
static const RectClass EVA_MAIN_UV(0,0,640.f/768.f,1);
static const RectClass EVA_COMPASS_UV(644.f/768.f,4.f/384.f,764.f/768.f,124.f/384.f);
static void Eva_Label(const char *text,float x,float y,unsigned long color=0xFF80FF40)
{
    if(!_EvaText) return;
    _EvaText->Reset();_EvaText->Set_Coordinate_Range(EVA_ATLAS);
    _EvaText->Set_Location(Vector2(x,y));_EvaText->Draw_Text(text,color);_EvaText->Render();
}

static bool Map_Point(const Vector3 &position,const RectClass &view,const Vector2 &origin,
    const Vector2 &span,Vector2 *out)
{
    const Vector2 &scale=MapMgrClass::Get_Map_Scale(),&center=MapMgrClass::Get_Map_Center();
    out->X=view.Left+(center.X+position.X*scale.X-origin.X)*view.Width()/span.X;
    out->Y=view.Top+(center.Y-position.Y*scale.Y-origin.Y)*view.Height()/span.Y;
    return out->X>=view.Left+6 && out->X<=view.Right-6 && out->Y>=view.Top+6 && out->Y<=view.Bottom-6;
}

static void Eva_Art(const RectClass &box,const RectClass &pixels,unsigned long color=0xFFFFFFFF)
{
    Render2DClass art;art.Set_Coordinate_Range(EVA_ATLAS);
    art.Set_Texture("HUD_MAIN.TGA");art.Get_Shader()->Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    art.Add_Quad(box,RectClass(pixels.Left/256,pixels.Top/256,pixels.Right/256,pixels.Bottom/256),color);art.Render();
}

// Draw digits at the render target's native resolution, using the EVA's
// green phosphor styling. The logical layout stays independent of target size.
static void Eva_Number(Render2DClass &shapes,int value,float x,float y,float height,float width_limit,unsigned long color)
{
    static const unsigned char digits[] = {0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    char text[16];
    if (value >= 0) _snprintf(text,sizeof(text),"%d",value);
    else strcpy(text,"INF");
    const float count = float(strlen(text));
    const float width = WWMath::Min(height*.59f,width_limit/(count+(count-1.f)*.22f));
    const float stroke = WWMath::Min(width*.19f,height*.12f);
    for (const char *c=text;*c;++c,x+=width+stroke*1.15f) {
        const unsigned mask = (*c>='0' && *c<='9') ? digits[*c-'0'] :
            *c=='I' ? 0x06 : *c=='N' ? 0x54 : 0x71;
        const float half=height*.5f;
        const RectClass segments[] = {
            RectClass(x+stroke,y,x+width-stroke,y+stroke),
            RectClass(x+width-stroke,y+stroke,x+width,y+half-stroke*.5f),
            RectClass(x+width-stroke,y+half+stroke*.5f,x+width,y+height-stroke),
            RectClass(x+stroke,y+height-stroke,x+width-stroke,y+height),
            RectClass(x,y+half+stroke*.5f,x+stroke,y+height-stroke),
            RectClass(x,y+stroke,x+stroke,y+half-stroke*.5f),
            RectClass(x+stroke,y+half-stroke*.5f,x+width-stroke,y+half+stroke*.5f)
        };
        for (int i=0;i<7;++i)
            if (mask & (1u<<i)) shapes.Add_Quad(segments[i],color);
    }
}

static void Draw_EVA_Compass()
{
    const Vector2 center(704,55);
    const Vector2 uv(0,0);
    const unsigned long accent=0xFFC8FF7B;
    Render2DClass shapes;
    shapes.Set_Coordinate_Range(EVA_ATLAS);
    shapes.Enable_Texturing(false);
    shapes.Get_Shader()->Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    // The border fills the eagle's original footprint and reads as an inset display.
    shapes.Add_Quad(RectClass(641,1,767,127),0xFF142119);
    shapes.Add_Rect(RectClass(645,5,763,123),2,0xFF66846A,0xFF081510);
    for (int i=0;i<24;++i) {
        const float a=i*(6.283185307f/24), b=(i+1)*(6.283185307f/24);
        const Vector2 from(sinf(a),-cosf(a)), to(sinf(b),-cosf(b));
        shapes.Add_Line(center+from*45,center+to*45,2,0xFF36584B);
        if (i%6==0) shapes.Add_Line(center+from*40,center+from*45,2,0xFF799B77);
    }
    const int count=ObjectiveManager::Get_Num_HUD_Objectives();
    if (!count) {
        shapes.Render();
        Eva_Label("NO OBJ",654,99,0xFF809585);
        return;
    }

    int index=HUDClass::Get_Current_Objective_Index();
    if (index<0 || index>=count) index=0;
    Vector3 player;
    COMBAT_STAR->Get_Position(&player);
    const Vector3 delta=ObjectiveManager::Get_HUD_Objectives_Location(index)-player;
    Matrix3D head=Pose_World(_Frame.Head);
    Vector3 forward=Forward(head);
    Vector2 heading(forward.X,forward.Y);
    if (heading.Length2()<.001f) {
        // Head yaw is still defined by the right axis when looking straight up/down.
        const Vector3 right=head.Get_X_Vector();
        heading.Set(-right.Y,right.X);
    }
    heading.Normalize();
    Vector2 direction(delta.X*heading.Y-delta.Y*heading.X,
        -(delta.X*heading.X+delta.Y*heading.Y));
    if (direction.Length2()>.0025f) {
        direction.Normalize();
        const Vector2 side(-direction.Y,direction.X);
        shapes.Add_Tri(center+direction*44,center+direction*26+side*8,
            center+direction*26-side*8,uv,uv,uv,accent);
    }
    shapes.Render();

    const char *icon=ObjectiveManager::Get_HUD_Objectives_Pog_Texture_Name(index);
    if (icon && *icon) {
        Render2DClass pog;
        pog.Set_Coordinate_Range(EVA_ATLAS);
        pog.Set_Texture(icon);
        pog.Get_Shader()->Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
        pog.Add_Quad(RectClass(682,33,726,77),RectClass(0,0,1,1));
        pog.Render();
    }
    char range[24];
    _snprintf(range,sizeof(range),"%d M",int(delta.Length()+.5f));
    if (_EvaText) {
        WideStringClass text(range);
        Eva_Label(range,center.X-_EvaText->Get_Text_Extents(text).X*.5f,99,accent);
    }
}

static void Draw_EVA_Readouts()
{
    Render2DClass shapes;
    shapes.Set_Coordinate_Range(EVA_ATLAS);
    shapes.Enable_Texturing(false);
    shapes.Get_Shader()->Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    const DefenseObjectClass *defense = COMBAT_STAR->Get_Defense_Object();
    const float health = defense->Get_Health(), armour = defense->Get_Shield_Strength();
    const float health_fraction = health/WWMath::Max(1.f,defense->Get_Health_Max());
    const float armour_fraction = armour/WWMath::Max(1.f,defense->Get_Shield_Strength_Max());
    const unsigned long life_color = health_fraction < .25f ? 0xFFFF8254 : 0xFFC8FF7B;
    const unsigned long armour_color = 0xFFA4EAFF;

    for (int row=0;row<3;++row) {
        const float y = 8.f+row*124.f;
        shapes.Add_Rect(RectClass(8,y,224,y+120.f),2,0xFF315949,0xFF081510);
    }
    Eva_Number(shapes,int(ceilf(health)),22,49,53,186,life_color);
    Eva_Number(shapes,int(ceilf(armour)),22,173,53,186,armour_color);
    const float fractions[] = {health_fraction,armour_fraction};
    const unsigned long colors[] = {life_color,armour_color};
    for (int row=0;row<2;++row) {
        const float y = 112.f+row*124.f;
        shapes.Add_Quad(RectClass(22,y,210,y+6),0xFF27433A);
        const float fraction = WWMath::Clamp(fractions[row],0.f,1.f);
        if (fraction > 0.f) shapes.Add_Quad(RectClass(22,y,22+188*fraction,y+6),colors[row]);
    }

    WeaponClass *weapon = COMBAT_STAR->Get_Weapon();
    const int clip = weapon ? weapon->Get_Clip_Rounds() : 0;
    const int reserve = weapon ? weapon->Get_Inventory_Rounds() : 0;
    const unsigned long ammo_color = clip==0 ? 0xFFFF8254 : clip>0 && clip<=3 ? 0xFFFFCE6C : 0xFFF8FFD5;
    shapes.Add_Quad(RectClass(131,268,133,363),0xFF315949);
    if (weapon) {
        Eva_Number(shapes,clip,22,298,57,102,ammo_color);
        Eva_Number(shapes,reserve,143,309,39,71,0xFFCDE7D0);
    }
    shapes.Render();
    Eva_Art(RectClass(21,15,45,40),RectClass(33,199,63,226),life_color);
    Eva_Art(RectClass(21,139,45,164),RectClass(66,97,96,132),armour_color);
    Eva_Label("HEALTH",54,15,life_color);
    Eva_Label("ARMOUR",54,139,armour_color);
    Eva_Label("AMMO",22,267,ammo_color);
    Eva_Label("RES",143,271,0xFFCDE7D0);
}

static void Draw_EVA_Map()
{
    StringClass name;MapMgrClass::Get_Map_Texture_Filename(name);
    if(name!=_MissionMapName) {
        REF_PTR_RELEASE(_MissionMap);_MissionMapName=name;
        if(!name.Is_Empty()) {
            _MissionMap=WW3DAssetManager::Get_Instance()->Get_Texture(name,TextureClass::MIP_LEVELS_1);
            if(_MissionMap) {_MissionMap->Init();_MapSourceSize.Set((float)_MissionMap->Get_Width(),(float)_MissionMap->Get_Height());}
            // Optional registered overhead capture: do not change pause-map coordinates.
            StringClass detailed("eva_");detailed+=name;
            TextureClass *capture=WW3DAssetManager::Get_Instance()->Get_Texture(detailed,TextureClass::MIP_LEVELS_1);
            if(capture) {
                capture->Init();
                if(!capture->Is_Missing_Texture()) {REF_PTR_RELEASE(_MissionMap);_MissionMap=capture;capture=NULL;VR_Log("EVA registered overhead override: %s\n",(const char*)detailed);}
                REF_PTR_RELEASE(capture);
            }
            if(_MissionMap) {_MissionMap->Init();VR_Log("EVA texture %d x %d missing %d\n",_MissionMap->Get_Width(),_MissionMap->Get_Height(),_MissionMap->Is_Missing_Texture());_MissionMap->Set_U_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);_MissionMap->Set_V_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);}
        }
        VR_Log("EVA map: %s, scale %.3f %.3f, center %.3f %.3f\n",(const char*)name,MapMgrClass::Get_Map_Scale().X,MapMgrClass::Get_Map_Scale().Y,MapMgrClass::Get_Map_Center().X,MapMgrClass::Get_Map_Center().Y);
    }
    RectClass view(232,8,632,376);
    Render2DClass shapes;shapes.Set_Coordinate_Range(EVA_ATLAS);shapes.Enable_Texturing(false);shapes.Get_Shader()->Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    shapes.Add_Rect(view,2,0xFF367A37,0xFF020803);shapes.Render();shapes.Reset();
    Vector3 position;COMBAT_STAR->Get_Position(&position);
    const Vector2 &scale=MapMgrClass::Get_Map_Scale(),&center=MapMgrClass::Get_Map_Center();
    bool map=_MissionMap && fabsf(scale.X)>0.0001f && fabsf(scale.Y)>0.0001f;
    Vector2 span(2*_MapRadius*fabsf(scale.X)*view.Width()/view.Height(),2*_MapRadius*fabsf(scale.Y));
    Vector2 origin(center.X+position.X*scale.X-span.X*.5f,center.Y-position.Y*scale.Y-span.Y*.5f);
    if(map) {
        float width=_MapSourceSize.X,height=_MapSourceSize.Y;
        if(width>0 && height>0) {
            span.X=WWMath::Clamp(span.X,1.0f,width);span.Y=WWMath::Clamp(span.Y,1.0f,height);
            origin.X=WWMath::Clamp(center.X+position.X*scale.X-span.X*.5f,0.0f,width-span.X);
            origin.Y=WWMath::Clamp(center.Y-position.Y*scale.Y-span.Y*.5f,0.0f,height-span.Y);
            Render2DClass image(_MissionMap);image.Set_Coordinate_Range(EVA_ATLAS);
            image.Add_Quad(view,RectClass(origin.X/width,origin.Y/height,(origin.X+span.X)/width,(origin.Y+span.Y)/height),0xFFFFFFFF);image.Render();
            // Use the mission's existing exploration cloud; no undiscovered map reveal.
            for(int x=0;x<MapMgrClass::CLOUD_WIDTH;++x) for(int y=0;y<MapMgrClass::CLOUD_HEIGHT;++y) {
                if(MapMgrClass::Is_Cell_Visible(x,y)) continue;
                float l=WWMath::Max(view.Left,view.Left+(x*width/20-origin.X)*view.Width()/span.X);
                float r=WWMath::Min(view.Right,view.Left+((x+1)*width/20-origin.X)*view.Width()/span.X);
                float t=WWMath::Max(view.Top,view.Top+(y*height/20-origin.Y)*view.Height()/span.Y);
                float b=WWMath::Min(view.Bottom,view.Top+((y+1)*height/20-origin.Y)*view.Height()/span.Y);
                if(r>l && b>t) shapes.Add_Quad(RectClass(l,t,r,b),0xF80A1910);
            }
        } else map=false;
    }
    if(!map) {
        // Local navigation grid remains useful on a level without a mission-map asset.
        for(int n=1;n<6;++n) {
            float x=view.Left+view.Width()*n/6,y=view.Top+view.Height()*n/6;
            shapes.Add_Line(Vector2(x,view.Top),Vector2(x,view.Bottom),1,0xFF214D35);
            shapes.Add_Line(Vector2(view.Left,y),Vector2(view.Right,y),1,0xFF214D35);
        }
    }
    shapes.Render();shapes.Reset();
    if(map) for(int i=0;i<ObjectiveManager::Get_Objective_Count();++i) {
        Objective *o=ObjectiveManager::Get_Objective(i);Vector2 point;
        if(!o || o->Status!=ObjectiveManager::STATUS_IS_PENDING || !Map_Point(o->Get_Position(),view,origin,span,&point)) continue;
        unsigned long color=VRGB_TO_INT32(o->Type_To_Color());
        shapes.Add_Outline(RectClass(point.X-5,point.Y-5,point.X+5,point.Y+5),2,color);
    }
    Matrix3D head=Pose_World(_Frame.Head);Vector3 forward=Forward(head);
    float heading=atan2f(forward.X,forward.Y);if(heading<0) heading+=6.283185307f;
    Vector2 player(432,192);bool visible=true;
    if(map) visible=Map_Point(position,view,origin,span,&player);
    if(visible) {
        Vector2 f(sinf(heading),-cosf(heading)),r(cosf(heading),sinf(heading));
        Vector2 uv(0,0);
        shapes.Add_Tri(player+f*11,player-f*7+r*6,player-f*7-r*6,uv,uv,uv,0xFFACFF58);
    }
    shapes.Add_Outline(view,2,0xFF72CF56);shapes.Render();
    const RectClass &res=Render2DClass::Get_Screen_Resolution();
    if (_EvaRadarTexture && res.Width()>128 && res.Height()>180) {
        Render2DClass radar(_EvaRadarTexture);
        radar.Set_Coordinate_Range(EVA_ATLAS);
        radar.Add_Quad(RectClass(514,251,628,375),
            RectClass(0,(res.Bottom-180)/res.Height(),128/res.Width(),(res.Bottom-36)/res.Height()));
        radar.Render();
    }
    Eva_Label("N",600,14,0xFFACFF58);
    char line[40];_snprintf(line,sizeof(line),"%dm",(int)_MapRadius);Eva_Label(line,244,346);

}

// One bounded 512px color scene pass shared by the two eyes. No green HUD tint.
void VRPlayerClass::Prepare_Scope()
{
    _ScopeValid=false;
    if(!_InGame || !_Frame.PoseValid || !On_Foot_Star() || !COMBAT_SCENE || !COMBAT_CAMERA ||
       !WeaponViewClass::Get_VR_Scope(&_ScopePose,&_ScopeRadius))return;
    Vector3 head=Pose_World(_Frame.Head).Get_Translation(),forward=_ScopePose.Get_X_Vector();
    Vector3 to_lens=_ScopePose.Get_Translation()-head;float distance=to_lens.Length();
    if(distance>.75f || distance<.025f || Vector3::Dot_Product(to_lens,forward)<=0)return;
    if(!_ScopeTexture)_ScopeTexture=DX8Wrapper::Create_Render_Target(512,512,WW3D_FORMAT_X8R8G8B8);
    IDirect3DDevice8 *device=DX8Wrapper::_Get_D3D_Device8();
    if(!_ScopeTexture || !device)return;
    if(!_ScopeDepth){
        IDirect3DSurface8 *depth=NULL;D3DSURFACE_DESC desc;D3DFORMAT format=D3DFMT_D16;
        if(SUCCEEDED(device->GetDepthStencilSurface(&depth))){if(SUCCEEDED(depth->GetDesc(&desc)))format=desc.Format;depth->Release();}
        if(FAILED(device->CreateDepthStencilSurface(512,512,format,D3DMULTISAMPLE_NONE,&_ScopeDepth)))return;
    }
    if(!_ScopeCamera)_ScopeCamera=new CameraClass;
    Matrix3D camera(true);Vector3 right=-_ScopePose.Get_Y_Vector(),up=_ScopePose.Get_Z_Vector();
    for(int r=0;r<3;++r){camera[r][0]=right[r];camera[r][1]=up[r];camera[r][2]=-forward[r];}
    camera.Set_Translation(_ScopePose.Get_Translation()+forward*.04f);
    float near_clip,far_clip;COMBAT_CAMERA->Get_Clip_Planes(near_clip,far_clip);
    _ScopeCamera->Set_Transform(camera);_ScopeCamera->Set_Clip_Planes(.03f,far_clip);
    float zoom=COMBAT_CAMERA->Get_Sniper_Zoom();_ScopeCamera->Set_View_Plane(DEG_TO_RADF(20.0f-14.0f*zoom));
    _ScopeCamera->Set_Viewport(Vector2(0,0),Vector2(1,1));
    IDirect3DSurface8 *surface=_ScopeTexture->Get_D3D_Surface_Level();if(!surface)return;
    // Hide only visual player geometry for this pass, then restore it before the eye passes.
    WeaponViewClass::Hide_VR_For_Scope(true);
    RenderObjClass *body=_PlayerBody?_PlayerBody->Peek_Model():NULL;bool body_hidden=body && body->Is_Hidden();
    if(body)body->Set_Hidden(true);
    DX8Wrapper::Set_Texture(0,NULL);
    COMBAT_SCENE->Prepare_View(*_ScopeCamera, false);
    DX8Wrapper::Set_Render_Target(_ScopeTexture);
    HRESULT hr=device->SetRenderTarget(surface,_ScopeDepth);surface->Release();
    if(SUCCEEDED(hr)){
        WW3D::Begin_Render(true,true,BackgroundMgrClass::Get_Clear_Color());
        WW3D::Render(CombatManager::Get_Background_Scene(),_ScopeCamera);
        WW3D::Render(COMBAT_SCENE,_ScopeCamera);WW3D::End_Render(false);_ScopeValid=true;
    }
    DX8Wrapper::Set_Render_Target((IDirect3DSurface8*)NULL);
    if(body)body->Set_Hidden(body_hidden);WeaponViewClass::Hide_VR_For_Scope(false);
}

void VRPlayerClass::Prepare_Overlays(void)
{
    _WristValid=false;
    if(!_WristHUD || !_Frame.PoseValid || !On_Foot_Star() || !_Frame.Hand[RVR_HAND_LEFT].Active) return;
    if(!_WristTexture) _WristTexture=DX8Wrapper::Create_Render_Target(WRIST_TEXTURE_SIZE,WRIST_TEXTURE_SIZE,WW3D_FORMAT_A8R8G8B8);
    if(!_WristTexture) _WristTexture=DX8Wrapper::Create_Render_Target(WRIST_TEXTURE_SIZE,WRIST_TEXTURE_SIZE,WW3D_FORMAT_X8R8G8B8);
    if(!_WristTexture) _WristTexture=DX8Wrapper::Create_Render_Target(512,512,WW3D_FORMAT_X8R8G8B8);
    if(!_WristTexture) return;
    _WristTexture->Set_U_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);
    _WristTexture->Set_V_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);
    if(!_EvaText) {
        Font3DInstanceClass *font=WW3DAssetManager::Get_Instance()->Get_Font3DInstance("FONT12x16.TGA");
        if(font) {font->Set_Scale(1.5f);_EvaText=new Render2DTextClass(font);font->Release_Ref();}
    }
    if(!_EvaRadarTexture) _EvaRadarTexture=DX8Wrapper::Create_Render_Target(1024,1024,WW3D_FORMAT_A8R8G8B8);
    if(_EvaRadarTexture) {
        DX8Wrapper::Set_Render_Target(_EvaRadarTexture);
        Render2DClass::Set_Render_Target_Size(1024,1024);
        WW3D::Begin_Render(true,false,Vector3(0,0,0));
        RadarManager::Render();
        WW3D::End_Render(false);
    }
    DX8Wrapper::Set_Render_Target(_WristTexture);
    Render2DClass::Set_Render_Target_Size(_WristTexture->Get_Width(),_WristTexture->Get_Height());
    WW3D::Begin_Render(true,false,Vector3(0.002f,0.008f,0.003f));
    Draw_EVA_Map();
    Draw_EVA_Readouts();
    Draw_EVA_Compass();
    WW3D::End_Render(false);Render2DClass::Set_Render_Target_Size(0,0);
    DX8Wrapper::Set_Render_Target((IDirect3DSurface8*)NULL);_WristValid=true;
}


static bool _FlatPanelVisible=false,_FlatPanelPlaced=false,_PanelBackDown=false,_PanelRecenterDown=false;
static Matrix3D _FlatPanelTracking(true),_FlatPanelWorld(true);
static float _FlatPanelWidth=1.6f,_FlatPanelHeight=.9f;
void VRPlayerClass::Set_Flat_Panel(bool enabled,int width,int height)
{
    bool first=enabled && !_FlatPanelVisible;_FlatPanelVisible=enabled;
    if(!enabled){_FlatPanelPlaced=false;DialogMgrClass::Set_VR_Pointer(false,Vector3(0,0,0),false);return;}
    if(!_FlatPanelPlaced){
        Matrix3D head=Build_Matrix3D(Quaternion(_Frame.Head.Orientation[0],_Frame.Head.Orientation[1],_Frame.Head.Orientation[2],_Frame.Head.Orientation[3]));
        Vector3 forward=-head.Get_Z_Vector();forward.Y=0;if(forward.Length2()<.001f)forward.Set(0,0,-1);forward.Normalize();
        Vector3 up(0,1,0),right;Vector3::Cross_Product(forward,up,&right);
        for(int r=0;r<3;++r){_FlatPanelTracking[r][0]=right[r];_FlatPanelTracking[r][1]=up[r];_FlatPanelTracking[r][2]=-forward[r];}
        _FlatPanelTracking.Set_Translation(Pose_Position(_Frame.Head)+forward*1.5f);_FlatPanelPlaced=true;
    }
    _FlatPanelWidth=_FlatPanelHeight*(float)width/WWMath::Max(height,1);
    _FlatPanelWorld=Mul(VRManagerClass::Get_Tracking_To_World(),_FlatPanelTracking);
    const RVR_Hand &controller=_Frame.Hand[RVR_HAND_RIGHT];Matrix3D aim=Pose_World(controller.Aim);
    Vector3 origin=Pose_World(controller.Grip).Get_Translation(),direction=Forward(aim),normal=_FlatPanelWorld.Get_Z_Vector();
    float denominator=Vector3::Dot_Product(direction,normal);Vector3 cursor=DialogMgrClass::Get_Mouse_Pos();bool hit=false;
    if(controller.Active && fabsf(denominator)>.0001f){
        float distance=Vector3::Dot_Product(_FlatPanelWorld.Get_Translation()-origin,normal)/denominator;
        if(distance>0){Vector3 local=Inverse(_FlatPanelWorld)*(origin+direction*distance);
            float u=.5f+local.X/_FlatPanelWidth,v=.5f-local.Y/_FlatPanelHeight;
            hit=u>=0 && u<=1 && v>=0 && v<=1;if(hit)cursor.Set(u*width,v*height,0);}
    }
    DialogMgrClass::Set_VR_Pointer(true,cursor,hit && controller.Trigger>.5f);
    const RVR_Hand &menu_hand = _Frame.Hand[RVR_HAND_LEFT];
    bool back=(controller.Active && (controller.Buttons & RVR_BUTTON_SECONDARY)) ||
        (menu_hand.Active && (menu_hand.Buttons & VR_MENU_BUTTONS));
    bool recenter=(_Frame.Hand[RVR_HAND_LEFT].Buttons&RVR_BUTTON_SECONDARY)!=0;
    if(!first && back && !_PanelBackDown)DialogMgrClass::VR_Back();
    if(!first && recenter && !_PanelRecenterDown){VRManagerClass::Recenter();_FlatPanelPlaced=false;}
    _PanelBackDown=back;_PanelRecenterDown=recenter;
}

struct OverlayVertex
{
	Vector3	Position;
	Vector2	UV;
	unsigned long Color;
};

static void Draw_Quads(const OverlayVertex * verts, int quad_count, const ShaderClass & shader, TextureClass * texture)
{
	int vert_count = quad_count * 4;
	DynamicVBAccessClass vb(BUFFER_TYPE_DYNAMIC_DX8, dynamic_fvf_type, vert_count);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vb);
		const FVFInfoClass & fi = vb.FVF_Info();
		unsigned char * va = (unsigned char *)lock.Get_Formatted_Vertex_Array();
		for (int i = 0; i < vert_count; i++) {
			*(Vector3 *)(va + fi.Get_Location_Offset()) = verts[i].Position;
			*(unsigned int *)(va + fi.Get_Diffuse_Offset()) = verts[i].Color;
			*(Vector2 *)(va + fi.Get_Tex_Offset(0)) = verts[i].UV;
			va += fi.Get_FVF_Size();
		}
	}
	DynamicIBAccessClass ib(BUFFER_TYPE_DYNAMIC_DX8, quad_count * 6);
	{
		DynamicIBAccessClass::WriteLockClass lock(&ib);
		unsigned short * mem = lock.Get_Index_Array();
		for (int q = 0; q < quad_count; q++) {
			unsigned short b = (unsigned short)(q * 4);
			mem[q * 6 + 0] = b;		mem[q * 6 + 1] = b + 1;	mem[q * 6 + 2] = b + 2;
			mem[q * 6 + 3] = b;		mem[q * 6 + 4] = b + 2;	mem[q * 6 + 5] = b + 3;
		}
	}

	VertexMaterialClass * vm = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vm);
	REF_PTR_RELEASE(vm);
	DX8Wrapper::Set_Shader(shader);
	DX8Wrapper::Set_Texture(0, texture);
	DX8Wrapper::Set_World_Identity();
	DX8Wrapper::Set_Vertex_Buffer(vb);
	DX8Wrapper::Set_Index_Buffer(ib, 0);
	DX8Wrapper::Draw_Triangles(0, quad_count * 2, 0, vert_count);
}

static void Set_Quad(OverlayVertex * v, const Vector3 & center, const Vector3 & right, const Vector3 & up,
	float half_w, float half_h, const RectClass & uv, unsigned long color)
{
	// top-left, top-right, bottom-right, bottom-left
	v[0].Position = center - right * half_w + up * half_h;	v[0].UV.Set(uv.Left, uv.Top);
	v[1].Position = center + right * half_w + up * half_h;	v[1].UV.Set(uv.Right, uv.Top);
	v[2].Position = center + right * half_w - up * half_h;	v[2].UV.Set(uv.Right, uv.Bottom);
	v[3].Position = center - right * half_w - up * half_h;	v[3].UV.Set(uv.Left, uv.Bottom);
	for (int i = 0; i < 4; i++) {
		v[i].Color = color;
	}
}

#include "vrweaponwheel.inl"

void VRPlayerClass::Render_Flat_Panel(CameraClass *eye,TextureClass *texture)
{
    if(!_FlatPanelVisible || !texture || !eye)return;eye->Apply();
    OverlayVertex panel[4];Set_Quad(panel,_FlatPanelWorld.Get_Translation(),_FlatPanelWorld.Get_X_Vector(),
        _FlatPanelWorld.Get_Y_Vector(),_FlatPanelWidth*.5f,_FlatPanelHeight*.5f,RectClass(0,0,1,1),0xffffffff);
    ShaderClass shader=ShaderClass::_PresetOpaqueShader;shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    Draw_Quads(panel,1,shader,texture);DX8Wrapper::Set_Texture(0,NULL);DX8Wrapper::Set_World_Identity();
}

static void Render_EVA_Surface(bool compass, const RectClass &uv)
{
    const VREvaVertex *surface;
    Matrix3D pose;
    const int count=WeaponViewClass::Get_VR_EVA_Surface(compass,&surface,&pose);
    if (!count) return;

    std::vector<OverlayVertex> vertices(count/3*4);
    for (int t=0;t<count/3;++t) {
        for (int j=0;j<4;++j) {
            const VREvaVertex &point=surface[t*3+(j<3 ? j : 2)];
            OverlayVertex &vertex=vertices[t*4+j];
            vertex.Position=pose*point.Position;
            vertex.UV.Set(uv.Left+point.UV.X*uv.Width(),uv.Top+point.UV.Y*uv.Height());
            vertex.Color=0xFFFFFFFF;
        }
    }
    ShaderClass screen=ShaderClass::_PresetOpaqueShader;
    screen.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    Draw_Quads(vertices.data(),count/3,screen,_WristTexture);
}

static void Render_Wrist()
{
    if (!_WristValid || !_WristTexture || !_HaveForearm) return;
    Render_EVA_Surface(false,EVA_MAIN_UV);
    Render_EVA_Surface(true,EVA_COMPASS_UV);
}

static void Render_Laser(CameraClass * eye_camera)
{
	Vector3 start, dir;
	SoldierGameObj * star = On_Foot_Star();
	if (!_LaserSight || star == NULL || star->Get_Weapon() == NULL || !Aim_Ray(&start, &dir)) {
		return;
	}
    if(star->Get_Weapon()->Get_Style()==WEAPON_HOLD_STYLE_C4 || star->Get_Weapon()->Get_Style()==WEAPON_HOLD_STYLE_BEACON) return;
    float range=WWMath::Clamp(star->Get_Weapon()->Get_Range(),1.0f,300.0f);
    LineSegClass ray(start,start+dir*range);CastResultStruct result;
    PhysRayCollisionTestClass test(ray,&result,BULLET_COLLISION_GROUP,COLLISION_TYPE_PROJECTILE);
    star->Peek_Physical_Object()->Inc_Ignore_Counter();COMBAT_SCENE->Cast_Ray(test);star->Peek_Physical_Object()->Dec_Ignore_Counter();
    Vector3 end=start+dir*(range*result.Fraction);
    bool hit=result.Fraction<1.0f && !result.StartBad;

	Vector3 to_eye = eye_camera->Get_Transform().Get_Translation() - start;
	Vector3 side;
	Vector3::Cross_Product(dir, to_eye, &side);
	if (side.Length2() < 0.000001f) {
		return;
	}
	side.Normalize();
	float half = 0.0015f;

	OverlayVertex v[4];
	RectClass no_uv(0.0f, 0.0f, 0.0f, 0.0f);
	v[0].Position = start + side * half;	v[0].Color = 0xFFB01010;
	v[1].Position = end + side * half;		v[1].Color = 0xFF300404;
	v[2].Position = end - side * half;		v[2].Color = 0xFF300404;
	v[3].Position = start - side * half;	v[3].Color = 0xFFB01010;
	for (int i = 0; i < 4; i++) {
		v[i].UV.Set(0.0f, 0.0f);
	}
	ShaderClass additive = ShaderClass::_PresetAdditiveSolidShader;
	additive.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    Draw_Quads(v,1,additive,NULL);
    if(hit) {
        // A surface dot as well as the beam; offset toward the eye to avoid z fighting.
        Vector3 eye_right=eye_camera->Get_Transform().Get_X_Vector(),eye_up=eye_camera->Get_Transform().Get_Y_Vector();
        Vector3 toward=eye_camera->Get_Transform().Get_Translation()-end;toward.Normalize();
        float size=WWMath::Clamp((end-start).Length()*0.0006f,0.003f,0.018f);
        OverlayVertex dot[32];Vector3 center=end+toward*0.003f;
        for(int i=0;i<8;++i) {
            float a=i*0.785398163f,b=(i+1)*0.785398163f;
            dot[i*4].Position=center;
            dot[i*4+1].Position=center+(eye_right*cosf(a)+eye_up*sinf(a))*size;
            dot[i*4+2].Position=center+(eye_right*cosf(b)+eye_up*sinf(b))*size;
            dot[i*4+3].Position=center;
            for(int j=0;j<4;++j) {dot[i*4+j].UV.Set(0,0);dot[i*4+j].Color=0xFFFF1515;}
        }
        Draw_Quads(dot,8,additive,NULL);
    }
}

static void Render_Scope(CameraClass *eye)
{
    if(!_ScopeValid || !_ScopeTexture)return;
    Vector3 center=_ScopePose.Get_Translation(),right=-_ScopePose.Get_Y_Vector(),up=_ScopePose.Get_Z_Vector();
    Vector3 view=eye->Get_Transform().Get_Translation()-center;
    if(Vector3::Dot_Product(view,_ScopePose.Get_X_Vector())>=0)return;
    const int sides=48;OverlayVertex lens[sides*4],ring[sides*4];
    for(int i=0;i<sides;++i){
        float a=i*6.283185307f/sides,b=(i+1)*6.283185307f/sides;
        Vector3 da=right*cosf(a)+up*sinf(a),db=right*cosf(b)+up*sinf(b);
        OverlayVertex *v=lens+i*4;v[0].Position=center;v[0].UV.Set(.5f,.5f);
        v[1].Position=center+da*_ScopeRadius;v[1].UV.Set(.5f+.5f*cosf(a),.5f-.5f*sinf(a));
        v[2].Position=center+db*_ScopeRadius;v[2].UV.Set(.5f+.5f*cosf(b),.5f-.5f*sinf(b));v[3]=v[2];
        for(int j=0;j<4;++j)v[j].Color=0xffffffff;
        v=ring+i*4;v[0].Position=center+da*_ScopeRadius;v[1].Position=center+da*_ScopeRadius*1.1f;
        v[2].Position=center+db*_ScopeRadius*1.1f;v[3].Position=center+db*_ScopeRadius;
        for(int j=0;j<4;++j){v[j].Color=0xff141414;v[j].UV.Set(0,0);}
    }
    ShaderClass texture=ShaderClass::_PresetOpaqueShader;texture.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    Draw_Quads(lens,sides,texture,_ScopeTexture);
    ShaderClass solid=texture;solid.Set_Texturing(ShaderClass::TEXTURING_DISABLE);Draw_Quads(ring,sides,solid,NULL);
    // Fine physical reticle lies in the lens plane, offset a fraction toward the eye.
    center-=_ScopePose.Get_X_Vector()*.0005f;OverlayVertex cross[8];
    Set_Quad(cross,center,right,up,_ScopeRadius*.9f,.00018f,RectClass(0,0,1,1),0xff111111);
    Set_Quad(cross+4,center,right,up,.00018f,_ScopeRadius*.9f,RectClass(0,0,1,1),0xff111111);
    Draw_Quads(cross,2,solid,NULL);
}

static void Render_Vehicle_Sight(CameraClass *eye)
{
    SoldierGameObj *star=COMBAT_STAR;VehicleGameObj *vehicle=star?star->Get_Vehicle():NULL;
    WeaponClass *weapon=vehicle?vehicle->Get_Weapon():NULL;if(!weapon || !COMBAT_SCENE)return;
    Matrix3D muzzle=vehicle->Get_Muzzle();Vector3 start=muzzle.Get_Translation(),direction=muzzle.Get_X_Vector();
    if(direction.Length2()<.001f)return;direction.Normalize();float range=WWMath::Clamp(weapon->Get_Range(),5.0f,400.0f);
    LineSegClass ray(start,start+direction*range);CastResultStruct result;
    PhysRayCollisionTestClass test(ray,&result,BULLET_COLLISION_GROUP,COLLISION_TYPE_PROJECTILE);
    star->Peek_Physical_Object()->Inc_Ignore_Counter();vehicle->Peek_Physical_Object()->Inc_Ignore_Counter();
    COMBAT_SCENE->Cast_Ray(test);vehicle->Peek_Physical_Object()->Dec_Ignore_Counter();star->Peek_Physical_Object()->Dec_Ignore_Counter();
    Vector3 target=start+direction*range*result.Fraction;
    Vector3 right=eye->Get_Transform().Get_X_Vector(),up=eye->Get_Transform().Get_Y_Vector();
    float size=WWMath::Clamp((target-eye->Get_Transform().Get_Translation()).Length()*.004f,.025f,1.4f);
    Vector3 toward=eye->Get_Transform().Get_Translation()-target;if(toward.Length2()>.001f){toward.Normalize();target+=toward*.02f;}
    OverlayVertex marks[8];unsigned color=weapon->Get_Clip_Rounds()!=0?0xff80ff40:0xffff6040;
    Set_Quad(marks,target,right,up,size,size*.12f,RectClass(0,0,1,1),color);
    Set_Quad(marks+4,target,right,up,size*.12f,size,RectClass(0,0,1,1),color);
    ShaderClass shader=ShaderClass::_PresetOpaqueShader;shader.Set_Texturing(ShaderClass::TEXTURING_DISABLE);shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);Draw_Quads(marks,2,shader,NULL);
}

void VRPlayerClass::Render_Overlays(CameraClass * eye_camera)
{
	if (!_HaveFrame || !_InGame || COMBAT_STAR == NULL) {
		return;
	}
	eye_camera->Apply();		// view and projection for this eye

	if(VRManagerClass::Is_Simulated() && strstr(::GetCommandLineA(),"EVAVIEW=1") && _WristValid) {
        Matrix3D eye=eye_camera->Get_Transform();OverlayVertex v[4];
        Set_Quad(v,eye*Vector3(0,0,-.12f),eye.Get_X_Vector(),eye.Get_Y_Vector(),.105f,.0525f,RectClass(0,0,1,1),0xFFFFFFFF);
        ShaderClass shader=ShaderClass::_PresetOpaqueShader;shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);Draw_Quads(v,1,shader,_WristTexture);
    } else {Render_Wrist();Render_Laser(eye_camera);Render_Vehicle_Sight(eye_camera);Render_Scope(eye_camera);Render_Weapon_Wheel(eye_camera);}
	DX8Wrapper::Set_Texture(0, NULL);
	DX8Wrapper::Set_World_Identity();
}
