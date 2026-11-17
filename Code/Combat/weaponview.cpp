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
 ***                            Confidential - Westwood Studios                              *** 
 *********************************************************************************************** 
 *                                                                                             * 
 *                 Project Name : Commando                                                     * 
 *                                                                                             * 
 *                     $Archive:: /Commando/Code/Combat/weaponview.cpp                        $* 
 *                                                                                             * 
 *                      $Author:: Byon_g                                                      $* 
 *                                                                                             * 
 *                     $Modtime:: 1/16/02 11:46a                                              $* 
 *                                                                                             * 
 *                    $Revision:: 56                                                          $* 
 *                                                                                             * 
 *---------------------------------------------------------------------------------------------* 
 * Functions:                                                                                  * 
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "weaponview.h"
#include "vr_forearm.h"
#include "assets.h"
#include "hanim.h"
#include "combat.h"
#include "pscene.h"
#include "ccamera.h"
#include "texture.h"
#include "physicalgameobj.h"
#include "debug.h"
#include "animobj.h"
#include "soldier.h"
#include "wwprofile.h"
#include "weapons.h"
#include "decophys.h"
#include "stealtheffect.h"
#include "animcontrol.h"
#include "mesh.h"
#include "meshmdl.h"
#include "htree.h"
#include "aabox.h"
#include <windows.h>
#include <stdio.h>
#include <vector>


/*
**
*/
enum {	
	WEAPON_STATE_IDLE,
	WEAPON_STATE_FIRE,
	WEAPON_STATE_RELOAD,
	WEAPON_STATE_ENTER,
	WEAPON_STATE_EXIT,
//	WEAPON_STATE_SPIN_DOWN,
	NUM_WEAPON_STATES,
};

int					WeaponViewEnabled;
int					WeaponState;
bool				LastMuzzleFlash;

DecorationPhysClass*HandsPhysObj;
RenderObjClass*		WeaponModel;
RenderObjClass*		ClipModel;

Vector3				HandsOffset;

HAnimClass	*		HandsAnims[ NUM_WEAPON_STATES ];
HAnimClass	*		WeaponAnims[ NUM_WEAPON_STATES ];

// Anim controls that provide blending
SimpleAnimControlClass	HandAnimControl;
SimpleAnimControlClass	WeaponAnimControl;

// Hands Bob
enum {
	BOB_NONE,
	BOB_IDLE,
	BOB_WALK,
	BOB_RUN,
};
int					BobState;
HTreeClass		*	BobHTree;
HAnimClass		*	BobHAnim;
float					BobFrame;
float					BobRecoil;


static	void		Set_Bob( int bob_state );
static	void		Set_Bob_Recoil( float amount );

static	void		Aquire_Hands_Assets( void );
static	void		Release_Hands_Assets( void );
static	void		Aquire_Weapon_Assets( const WeaponClass * weapon );
static	void		Release_Weapon_Assets( void );

/*
**
*/
WeaponViewClass::HandTransformFunc WeaponViewClass::VRHandTransform = NULL;

static int _VRManualStage=0;
static float _VRActionProgress=0;
static bool _VRMagazineRemoved=false;
static std::vector<VREvaVertex> VREvaMain, VREvaCompass;
static int VREvaBone=0;
static int VRRemoteBone=0, VRRemoteHand=0, VRRemoteFinger=0, VRRemoteKnuckle=0;
static Matrix3D VRRemoteGrip;
static int Magazine_Bone(){
    if(!WeaponModel)return 0;
    for(int b=1;b<WeaponModel->Get_Num_Bones();++b){const char *n=WeaponModel->Get_Bone_Name(b);
        if(n && !strnicmp(n,"F_CM_",5))return b;}
    return 0;
}
static bool Magazine_Part(RenderObjClass *gun,RenderObjClass *part,int root){
    if(!gun || !part || root<=0 || !gun->Get_HTree())return false;
    int bone=gun->Get_Sub_Object_Bone_Index(part);
    for(int b=bone;b>0;b=gun->Get_HTree()->Get_Parent_Index(b))if(b==root)return true;
    return false;
}
static void Update_VR_Magazine(){
    if(!WeaponViewClass::VRHandTransform || !WeaponModel)return;
    WeaponClass *active=COMBAT_STAR?COMBAT_STAR->Get_Weapon():NULL;
    if(active && (active->Get_Style()==WEAPON_HOLD_STYLE_C4 || active->Get_Style()==WEAPON_HOLD_STYLE_BEACON))return;
    int root=Magazine_Bone();
    if(root){
        if(ClipModel)ClipModel->Set_Hidden(true); // native reload prop must not duplicate the seated part
        for(int i=0;i<WeaponModel->Get_Num_Sub_Objects();++i){RenderObjClass *part=WeaponModel->Get_Sub_Object(i);
            if(Magazine_Part(WeaponModel,part,root)){part->Set_Hidden(_VRMagazineRemoved);part->Set_Animation_Hidden(false);}
            if(part)part->Release_Ref();}
    }else if(ClipModel){ClipModel->Set_Hidden(_VRMagazineRemoved);ClipModel->Set_Animation_Hidden(false);}
}
static bool Is_MK23()
{return false;} // Custom pistol retired; native pistol uses native animations.
static void Apply_MK23_Action()
{
    if(!Is_MK23())return;
    int slide=WeaponModel->Get_Bone_Index("SLIDE");if(slide<=0)return;
    bool empty=COMBAT_STAR && COMBAT_STAR->Get_Weapon() && COMBAT_STAR->Get_Weapon()->Get_Clip_Rounds()==0;
    if(WeaponClass::ManualReloadForPlayer && (_VRManualStage==3 || empty)){
        HTreeClass sample(*WeaponModel->Get_HTree());for(int i=0;i<sample.Num_Pivots();++i)sample.Release_Bone(i);
        Matrix3D identity(true);sample.Base_Update(identity);Matrix3D pose=sample.Get_Transform(slide);
        pose.Set_Translation(pose.Get_Translation()+Vector3(0,.038f,0));
        WeaponModel->Capture_Bone(slide);WeaponModel->Control_Bone_Root_Relative(slide,pose);
    }else WeaponModel->Release_Bone(slide);
}
// Supplied source arms and weapon parts share one authored timeline.
static bool Is_Source_Rifle(){return false;} // Native rifle and hand animations.
static void Select_Faction_Reload(WeaponClass *weapon){
    if(!weapon || !weapon->Is_Faction_Rifle())return;
    const bool gdi=weapon->Is_GDI_Rifle(),empty=weapon->Get_Clip_Rounds()==0;
    StringClass name;name.Format("BR_%s.BR_%s_W%s",gdi?"M4":"AK",gdi?"M4":"AK",empty?"EMPTY":"TAC");
    HAnimClass *g=WW3DAssetManager::Get_Instance()->Get_HAnim(name);
    if(g){REF_PTR_RELEASE(WeaponAnims[WEAPON_STATE_RELOAD]);WeaponAnims[WEAPON_STATE_RELOAD]=g;}
}
static void Update_Source_Rifle(){
    if(!HandsPhysObj || !HandsPhysObj->Peek_Model())return;
    RenderObjClass *hands=HandsPhysObj->Peek_Model();bool source=Is_Source_Rifle();
    if(!WeaponViewClass::VRHandTransform){
        for(int i=0;i<hands->Get_Num_Sub_Objects();++i){RenderObjClass *p=hands->Get_Sub_Object(i);
            if(p){if(strstr(p->Get_Name(),"F_HM_"))p->Set_Hidden(source);p->Release_Ref();}}
    }
    if(!source)return;
    for(int i=0;i<WeaponModel->Get_Num_Sub_Objects();++i){RenderObjClass *p=WeaponModel->Get_Sub_Object(i);
        if(p){if(strstr(p->Get_Name(),"DARM_") || strstr(p->Get_Name(),"DSPARE_"))p->Set_Hidden(WeaponViewClass::VRHandTransform!=NULL);p->Release_Ref();}}
    // Manual action opens only real moving mechanism parts. The donor's whole
    // reload does not drag a tracked gun or animate a second pair of arms.
    if(WeaponViewClass::VRHandTransform){
        const char *tree=WeaponModel->Get_HTree()->Get_Name();StringClass name;name.Format("%s.%s_VRBACK",tree,tree);
        HAnimClass *back=WW3DAssetManager::Get_Instance()->Get_HAnim(name);
        if(back){HTreeClass sample(*WeaponModel->Get_HTree());for(int i=0;i<sample.Num_Pivots();++i)sample.Release_Bone(i);
            Matrix3D id(true);sample.Anim_Update(id,back,0);
            const char *parts[]={"BOLT","CHARGE"};
            for(int i=0;i<2;++i){int bone=WeaponModel->Get_Bone_Index(parts[i]);if(bone<=0)continue;
                if(_VRManualStage==3){
                    HTreeClass rest(*WeaponModel->Get_HTree());for(int b=0;b<rest.Num_Pivots();++b)rest.Release_Bone(b);rest.Base_Update(id);
                    Matrix3D pose=sample.Get_Transform(bone);Vector3 origin=rest.Get_Transform(bone).Get_Translation();
                    float stroke=(!stricmp(tree,"BR_M4") && i==0)?1.0f:_VRActionProgress;
                    pose.Set_Translation(origin+(pose.Get_Translation()-origin)*stroke);
                    WeaponModel->Capture_Bone(bone);WeaponModel->Control_Bone_Root_Relative(bone,pose);
                }
                else WeaponModel->Release_Bone(bone);
            }back->Release_Ref();}
    }
}

// Retarget the native hand-held clip trajectory to the replacement magazine.
// This path is flat-only: tracked VR retains its own independent magazine state.
static void Apply_Flatscreen_Reload()
{
    if(WeaponViewClass::VRHandTransform || !WeaponModel || !HandsPhysObj)return;
    const bool pistol=Is_MK23();
    if(!pistol)return;
    const char *tree=WeaponModel->Get_HTree()?WeaponModel->Get_HTree()->Get_Name():"";
    if(!pistol && stricmp(tree,"BR_M4") && stricmp(tree,"BR_AK"))return;
    int mag=pistol?WeaponModel->Get_Bone_Index("MAG"):Magazine_Bone();if(mag<=0)return;
    HTreeClass bind(*WeaponModel->Get_HTree());
    for(int b=0;b<bind.Num_Pivots();++b)bind.Release_Bone(b);
    Matrix3D id(true);bind.Base_Update(id);Matrix3D pose=bind.Get_Transform(mag);
    bool visible=true;
    if(WeaponState==WEAPON_STATE_RELOAD && HandsAnims[WEAPON_STATE_RELOAD]){
        HAnimClass *anim=HandsAnims[WEAPON_STATE_RELOAD];
        RenderObjClass *hands=HandsPhysObj->Peek_Model();
        if(hands && hands->Get_HTree()){
            HTreeClass sample(*hands->Get_HTree());
            for(int b=0;b<sample.Num_Pivots();++b)sample.Release_Bone(b);
            int gun=sample.Get_Bone_Index("GUNBONE"),clip=sample.Get_Bone_Index("CLIPBONE");
            if(gun>0 && clip>0){
                // The last visible clip frame is where the native hand seats it.
                // Calibrate there so the replacement returns exactly to its own magwell.
                int seat=-1;
                for(int f=anim->Get_Num_Frames()-1;f>=0;--f){
                    sample.Anim_Update(id,anim,(float)f);
                    if(sample.Get_Visibility(clip)){seat=f;break;}
                }
                if(seat>=0){
                    Matrix3D inv,reference,current,calibration;
                    sample.Anim_Update(id,anim,(float)seat);
                    sample.Get_Transform(gun).Get_Orthogonal_Inverse(inv);
                    Matrix3D::Multiply(inv,sample.Get_Transform(clip),&reference);
                    reference.Get_Orthogonal_Inverse(inv);Matrix3D::Multiply(inv,pose,&calibration);
                    float frame=HandAnimControl.Get_Current_Frame();sample.Anim_Update(id,anim,frame);
                    if(frame<seat){
                        visible=sample.Get_Visibility(clip);
                        sample.Get_Transform(gun).Get_Orthogonal_Inverse(inv);
                        Matrix3D::Multiply(inv,sample.Get_Transform(clip),&current);
                        Matrix3D::Multiply(current,calibration,&pose);
                    }
                }
            }
        }
        // The donor pistol has 113 frames, the native hands have 29: sample both
        // at the same normalized time instead of letting the gun continue alone.
        HAnimClass *gunanim=WeaponAnims[WEAPON_STATE_RELOAD];
        if(pistol && gunanim && anim->Get_Num_Frames()>1){
            float f=HandAnimControl.Get_Current_Frame()*(gunanim->Get_Num_Frames()-1)/(anim->Get_Num_Frames()-1);
            WeaponModel->Set_Animation(gunanim,f,RenderObjClass::ANIM_MODE_MANUAL);
        }
    }
    // Capture even while idle, preventing the synthetic/donor magazine channel
    // from competing with the hand clip or the seated bind position.
    WeaponModel->Capture_Bone(mag);WeaponModel->Control_Bone_Root_Relative(mag,pose);
    if(pistol && ClipModel){ClipModel->Set_Hidden(!visible);ClipModel->Set_Animation_Hidden(false);}
    else for(int i=0;i<WeaponModel->Get_Num_Sub_Objects();++i){
        RenderObjClass *part=WeaponModel->Get_Sub_Object(i);
        if(part && Magazine_Part(WeaponModel,part,mag)){part->Set_Hidden(!visible);part->Set_Animation_Hidden(false);}
        if(part)part->Release_Ref();
    }
}

static void Pose_VR_Remote()
{
    if (!VRRemoteBone) return;
    RenderObjClass *hands=HandsPhysObj->Peek_Model();
    const Matrix3D &wrist=hands->Get_Bone_Transform(VRRemoteHand);
    Matrix3D target, local, world_to_gun;
    Matrix3D::Multiply(wrist,VRRemoteGrip,&target);
    if (VRRemoteFinger && VRRemoteKnuckle) {
        const Vector3 finger=hands->Get_Bone_Transform(VRRemoteFinger).Get_Translation();
        const Vector3 knuckle=hands->Get_Bone_Transform(VRRemoteKnuckle).Get_Translation();
        const Vector3 palm=finger+(knuckle-finger)*.40f;
        // Centre of the stock transmitter's ribbed grip in F_GM_REMOTE space.
        const Vector3 grip(-.0097f,.0502f,-.0303f);
        target.Set_Translation(palm-target.Rotate_Vector(grip));
    }
    WeaponModel->Get_Transform().Get_Orthogonal_Inverse(world_to_gun);
    Matrix3D::Multiply(world_to_gun,target,&local);
    WeaponModel->Capture_Bone(VRRemoteBone);
    WeaponModel->Control_Bone_Root_Relative(VRRemoteBone,local);
}

static bool Apply_VR_Weapon_Pose()
{
    if(!WeaponViewClass::VRHandTransform || !HandsPhysObj || !HandsPhysObj->Peek_Model() || !WeaponModel) return false;
    Matrix3D tm;
    if(!WeaponViewClass::VRHandTransform(HandsPhysObj->Peek_Model(),WeaponModel,&tm)) return false;
    HandsPhysObj->Set_Transform(tm);
    Pose_VR_Remote();
    Update_Source_Rifle();
    Apply_Flatscreen_Reload();
    Apply_MK23_Action();
    return true;
}

// Instance-local materials: never change shared third-person/world gun shaders.
static void VR_Two_Sided(RenderObjClass *obj)
{
    if(!obj) return;
    if(obj->Class_ID()==RenderObjClass::CLASSID_MESH) {
        MeshClass *mesh=(MeshClass *)obj;mesh->Make_Unique();
        MeshModelClass *model=mesh->Peek_Model();
        model->Set_Flag(MeshModelClass::TWO_SIDED,true);
        for(int pass=0;pass<model->Get_Pass_Count();++pass) {
            if(model->Has_Shader_Array(pass)) {
                for(int i=0;i<model->Get_Polygon_Count();++i) {
                    ShaderClass sh=model->Get_Shader(i,pass);sh.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);model->Set_Shader(i,sh,pass);
                }
            } else {
                ShaderClass sh=model->Get_Single_Shader(pass);sh.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);model->Set_Single_Shader(sh,pass);
            }
        }
    }
    for(int i=0;i<obj->Get_Num_Sub_Objects();++i) {
        RenderObjClass *sub=obj->Get_Sub_Object(i);VR_Two_Sided(sub);if(sub) sub->Release_Ref();
    }
}

// These two legacy heavy models omit the grip behind the authored right hand.
// The replacement third-person assets have the same omission. Complete that
// local surface around the idle palm/index joints, without moving either hand.
class VRGripModelClass : public MeshModelClass
{
public:
    void Color(bool shell) {
        CurMatDesc->Set_DCG_Source(0,VertexMaterialClass::COLOR1);
        unsigned *colors=Get_Color_Array(0);const Vector3 *normals=Get_Vertex_Normal_Array();
        for(int i=0;i<Get_Vertex_Count();++i) {
            int shade=(int)(25+15*fabsf(normals[i].X)+8*fabsf(normals[i].Z));
            colors[i]=shell ? 0xff90652a : 0xff000000 | ((shade+2)<<16) | ((shade+5)<<8) | shade;
        }
    }
};

class VRGripMeshClass : public MeshClass
{
public:
    VRGripMeshClass(bool heavy, bool shell=false) {
        std::vector<Vector3> vertices;std::vector<TriIndex> tris;
        if(heavy) {
            Add_Box(vertices,tris,Vector3(-0.027f,-0.030f,0.008f),Vector3(0.055f,0.018f,0.020f));
            Add_Box(vertices,tris,Vector3(-0.095f,-0.015f,0.008f),Vector3(0.035f,0.035f,0.028f));
            Add_Box(vertices,tris,Vector3(0.050f,-0.026f,0.008f),Vector3(0.023f,0.035f,0.023f));
            Add_Box(vertices,tris,Vector3(0.035f,-0.082f,0.008f),Vector3(0.030f,0.004f,0.020f));
            Add_Box(vertices,tris,Vector3(0.006f,-0.060f,0.008f),Vector3(0.004f,0.026f,0.020f));
            Add_Box(vertices,tris,Vector3(0.034f,-0.057f,-0.006f),Vector3(0.012f,0.003f,0.010f));
        } else Add_Box(vertices,tris,Vector3(0,0,0),shell ? Vector3(0.012f,0.012f,0.032f) : Vector3(0.018f,0.030f,0.055f));
        Model=NEW_REF(VRGripModelClass,());Model->Reset((int)tris.size(),(int)vertices.size(),1);
        Model->Set_Name(heavy ? "VR_COMPLETE_GRIP" : "VR_RELOAD_SUPPLY");
        memcpy(Model->Get_Vertex_Array(),&vertices[0],vertices.size()*sizeof(Vector3));
        memcpy(const_cast<TriIndex *>(Model->Get_Polygon_Array()),&tris[0],tris.size()*sizeof(TriIndex));
        Model->Set_Flag(MeshModelClass::DIRTY_VNORMALS,true);Model->Set_Flag(MeshModelClass::DIRTY_BOUNDS,true);Model->Set_Flag(MeshModelClass::DIRTY_PLANES,true);
        Model->Set_Flag(MeshModelClass::TWO_SIDED,true);
        ((VRGripModelClass *)Model)->Color(shell);
        VertexMaterialClass *mat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
        Model->Set_Single_Material(mat);mat->Release_Ref();
        ShaderClass shader=ShaderClass::_PresetOpaqueSolidShader;shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);Model->Set_Single_Shader(shader);
        Set_Collision_Type(0);
    }
private:
    static void Add_Box(std::vector<Vector3> &v,std::vector<TriIndex> &t,const Vector3 &center,const Vector3 &half) {
        unsigned base=(unsigned)v.size();
        for(int n=0;n<8;++n) v.push_back(center+Vector3((n&1)?half.X:-half.X,(n&2)?half.Y:-half.Y,(n&4)?half.Z:-half.Z));
        const int faces[6][4]={{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
        for(int f=0;f<6;++f) {TriIndex a,b;a.I=base+faces[f][0];a.J=base+faces[f][1];a.K=base+faces[f][2];b.I=a.I;b.J=a.K;b.K=base+faces[f][3];t.push_back(a);t.push_back(b);}
    }
};

void 	WeaponViewClass::Init()
{
	WeaponModel = NULL;
	HandsPhysObj = NULL;
	HandAnimControl.Set_Model( NULL );
	WeaponAnimControl.Set_Model( NULL );

	ClipModel = NULL;
	
	for ( int i = 0; i < NUM_WEAPON_STATES; i++ ) {
		WeaponAnims[i] = NULL;
		HandsAnims[i] = NULL;
	}

	WeaponState = -1;

	BobState = BOB_NONE;
	BobHTree = NULL;
	BobHAnim = NULL;
	BobFrame = 0;
	BobRecoil = 0;

	WeaponViewEnabled = false;
}

/*
**
*/
void 	WeaponViewClass::Shutdown()
{
	Set_Bob( BOB_NONE );

	Release_Weapon_Assets();
	Release_Hands_Assets();
}

/*
**
*/
void 	WeaponViewClass::Reset()
{
	Set_Bob( BOB_NONE );

	Release_Weapon_Assets();
	Release_Hands_Assets();
}


enum	{
	CHUNKID_VARIABLES						=	730011054,

	MICROCHUNKID_HANDS_PHYS_OBJ		=	1,
	MICROCHUNKID_ENABLED,
};

bool	WeaponViewClass::Save( ChunkSaveClass &csave )
{
	csave.Begin_Chunk( CHUNKID_VARIABLES );
		// If the scene has our hands, we must save and swizzle them
		if ( HandsPhysObj != NULL && COMBAT_SCENE->Contains( HandsPhysObj ) ) {
			WRITE_MICRO_CHUNK( csave, MICROCHUNKID_HANDS_PHYS_OBJ, HandsPhysObj );
		}
		WRITE_MICRO_CHUNK( csave, MICROCHUNKID_ENABLED, WeaponViewEnabled );
	csave.End_Chunk();
	return true;
}

bool	WeaponViewClass::Load( ChunkLoadClass &cload )
{
	Release_Weapon_Assets();
	Release_Hands_Assets();

	while (cload.Open_Chunk()) {
		switch(cload.Cur_Chunk_ID()) {

			case CHUNKID_VARIABLES:
			{
				while (cload.Open_Micro_Chunk()) {
					switch(cload.Cur_Micro_Chunk_ID()) {
						READ_MICRO_CHUNK( cload, MICROCHUNKID_HANDS_PHYS_OBJ, HandsPhysObj );
						READ_MICRO_CHUNK( cload, MICROCHUNKID_ENABLED, WeaponViewEnabled );

						default:
							Debug_Say(("Unhandled Micro Chunk:%d File:%s Line:%d\r\n",cload.Cur_Micro_Chunk_ID(),__FILE__,__LINE__));
							break;
					}
					cload.Close_Micro_Chunk();
				}

				break;
			}

			default:
				Debug_Say(("Unhandled Chunk:%d File:%s Line:%d\r\n",cload.Cur_Chunk_ID(),__FILE__,__LINE__));
				break;

		}
		cload.Close_Chunk();
	}

	if ( HandsPhysObj != NULL ) {
		REQUEST_REF_COUNTED_POINTER_REMAP ((RefCountClass **)&HandsPhysObj);
	}

	return true;
}

/*
**
*/
void 	WeaponViewClass::Enable( bool enable )
{
	WeaponViewEnabled = enable;
}

/*
**	called each time through the main loop
*/
void 	WeaponViewClass::Think()
{
	WWPROFILE( "WeaponView Think" );

	bool bail = false;

	if ( !VRHandTransform && COMBAT_CAMERA && COMBAT_CAMERA->Is_Star_Sniping() ) {
		bail = true;
	}

	if ( COMBAT_CAMERA && COMBAT_CAMERA->Is_Using_Host_Model() ) {
		bail = true;
	}

	if ( COMBAT_STAR == NULL ) {
		bail = true;
	}

	if ( !WeaponViewEnabled ) {
		bail = true;
	}

	// No first person weapon in vehicles
	if ( COMBAT_STAR && COMBAT_STAR->Get_Vehicle() != NULL ) {
		bail = true;
	}

	// not if in trans (camera lerping )
	if ( COMBAT_CAMERA && COMBAT_CAMERA->Is_Lerping() ) {
		bail = true;
	}


	WeaponClass * weapon = NULL;

	if ( COMBAT_STAR != NULL ) {
		weapon = COMBAT_STAR->Get_Weapon();
	}

	if ( weapon == NULL && WeaponModel == NULL ) {
		bail = true;
	}

	if ( weapon != NULL ) {
		if ( ( weapon->Get_Style() == WEAPON_HOLD_STYLE_C4 ) || 
			  ( weapon->Get_Style() == WEAPON_HOLD_STYLE_BEACON ) ) {
			if ( weapon->Get_Total_Rounds() == 0 ) {
				if ( WeaponModel == NULL || WeaponState == WEAPON_STATE_IDLE ) {
					bail = true;
				}
			}
		}
	}

	// If not in first person mode, hide the model and bail
	if ( bail ) {
		if ( HandsPhysObj != NULL && COMBAT_SCENE->Contains( HandsPhysObj ) ) {
			COMBAT_SCENE->Remove_Object( HandsPhysObj );
		}

		// To force an enter when we come back
		WeaponState = WEAPON_STATE_EXIT;

		Release_Weapon_Assets();
		return;
	}

	// Setup camera bob
	if ( COMBAT_STAR ) {
		Vector3 vel;
		COMBAT_STAR->Get_Velocity( vel );
		if ( vel.Length() > WWMATH_EPSILON ) {
			if ( COMBAT_STAR->Is_Slow() ) {
				Set_Bob( BOB_WALK );
			} else {
				Set_Bob( BOB_RUN );
			}
		} else {
			Set_Bob( BOB_IDLE );
		}

		// Setup weapon State
		if ( weapon != NULL && weapon->Is_Firing() ) {
			Set_Bob_Recoil( 0.15f );
		}
	}

	// is the current (non-looping) animation completed
	bool	is_current_complete = true;
#if 0
	if ( HandsPhysObj != NULL && (WeaponState != WEAPON_STATE_IDLE) ) {
		RenderObjClass * hands_model = HandsPhysObj->Peek_Model();
		if ( hands_model != NULL && ( hands_model->Peek_Animation() != NULL ) &&
			  ((Animatable3DObjClass*)hands_model)->Is_Animation_Complete() == false ) {
			is_current_complete = false;
		}
	}
#else
	if ( HandsPhysObj != NULL && (WeaponState != WEAPON_STATE_IDLE) ) {
		is_current_complete = Is_Source_Rifle() ? WeaponAnimControl.Is_Complete() : HandAnimControl.Is_Complete();
	}
#endif

	bool	muzzle_flash_on = false;

	static bool	ForceFireLoop = false;

	// What state are we going to?
	int new_weapon_state = WEAPON_STATE_IDLE;
	if ( COMBAT_STAR ) {
		WeaponClass	* star_weapon = COMBAT_STAR->Get_Weapon();
		if ( star_weapon != NULL ) {
			if ( star_weapon->Is_Reloading() ) {
                // Physical VR gestures have already performed the reload. Keep
                // the inserted magazine steady instead of replaying the flat
                // hand/clip removal animation; weapon timing/ammo stay authoritative.
                new_weapon_state = (VRHandTransform && WeaponClass::ManualReloadForPlayer) ? WEAPON_STATE_IDLE : WEAPON_STATE_RELOAD;
			} else if ( star_weapon->Is_Firing() ) {
				if ( WeaponState == WEAPON_STATE_FIRE ) {
					ForceFireLoop = true;
				}
				new_weapon_state = WEAPON_STATE_FIRE;

				if ( WeaponState == WEAPON_STATE_RELOAD ) {
					is_current_complete = true;	// Force a fire during reload
				}

				muzzle_flash_on = !star_weapon->Was_M203_Fired();
                if(star_weapon->Was_M203_Fired())new_weapon_state=WEAPON_STATE_IDLE;

				if ( WeaponModel != NULL ) {
					// if this gun has an eject bone, eject a shell.
					int eject_index = WeaponModel->Get_Bone_Index( "eject" );
					if ( eject_index > 0 ) {
						star_weapon->Make_Shell_Eject( WeaponModel->Get_Bone_Transform( eject_index ) );
					}
				}
			}

		}
	}

	const char* cur_weapon_model_name="";

	if ( WeaponModel ) {
		cur_weapon_model_name = WeaponModel->Get_Name();
	}

	StringClass new_weapon_model_name(true);

	if ( weapon ) {
		Get_Render_Obj_Name_From_Filename( new_weapon_model_name, weapon->Get_First_Person_Model_Name() );
	}

	// If we need to change the assets,
	if ( stricmp( cur_weapon_model_name,new_weapon_model_name)!=0) {
//	if ( cur_weapon_model_name.Compare_No_Case( new_weapon_model_name ) != 0 ) {

		// If we don't have a weapon, switch!
		if ( WeaponModel == NULL ) {
			WeaponState = WEAPON_STATE_EXIT;
			is_current_complete = true;
		} 
			
		if ( WeaponState != WEAPON_STATE_EXIT ) {	// If we have a current weapon, exit it
			new_weapon_state = WEAPON_STATE_EXIT;
		} else {
			if ( is_current_complete ) {
				// If we are done exiting, switch and enter
				Release_Weapon_Assets();
				Aquire_Weapon_Assets( COMBAT_STAR->Get_Weapon() );
				new_weapon_state = WEAPON_STATE_ENTER;
			}
		}
	}

	if ( WeaponModel ) {
		// Update the muzzle flash
		RenderObjClass * model = WeaponModel;
        if ( model ) {
			for (int i=0; i<model->Get_Num_Sub_Objects(); i++) {
				RenderObjClass * robj = model->Get_Sub_Object(i);
				if (strstr(robj->Get_Name(),"MUZZLEFLASH") || strstr(robj->Get_Name(),"MZ")) {
                    bool flash=muzzle_flash_on;
                    if(strstr(robj->Get_Name(),"MZB"))flash=weapon && weapon->Is_Firing() && weapon->Was_M203_Fired();
                    robj->Set_Hidden(!flash);
				}
				robj->Release_Ref();
			}
			LastMuzzleFlash = muzzle_flash_on;
		}
	}


	if ( HandsPhysObj != NULL && WeaponModel != NULL ) {

		// make sure the "hands" object has the stealth effect
		if (COMBAT_STAR->Peek_Stealth_Effect() != NULL) {
			HandsPhysObj->Add_Effect_To_Me(COMBAT_STAR->Peek_Stealth_Effect());
		}

		// Add all rendobjs from the scene
		if ( !COMBAT_SCENE->Contains( HandsPhysObj ) ) {
			COMBAT_SCENE->Add_Dynamic_Object( HandsPhysObj );

			if ( new_weapon_state == WEAPON_STATE_ENTER ) {
				new_weapon_state = WEAPON_STATE_IDLE;
				is_current_complete = true;
			}
		}

		// When stopping, go to spin down
		if ( is_current_complete && 
				WeaponState == WEAPON_STATE_FIRE && 
				new_weapon_state == WEAPON_STATE_IDLE ) {
			// if the anims exist...
//			new_weapon_state = WEAPON_STATE_SPIN_DOWN;
		}

		if ( WeaponState != WEAPON_STATE_IDLE ) {
			if ( is_current_complete ) {
				// If done playing the current non-looping anim, go to next state

				if ( WeaponState == WEAPON_STATE_FIRE && ForceFireLoop ) {
					ForceFireLoop = false;	// Clear any force fire
					// Keep it by restarting the fire anim

					RenderObjClass * hands_model = HandsPhysObj->Peek_Model();
					if ( hands_model != NULL ) {										
//						hands_model->Set_Animation( HandsAnims[ WeaponState ], 0, RenderObjClass::ANIM_MODE_ONCE );
						HandAnimControl.Set_Animation( HandsAnims[ WeaponState ] );
						HandAnimControl.Set_Mode( ANIM_MODE_ONCE, 0 );
						HandAnimControl.Update(0);
					}
//					WeaponModel->Set_Animation( WeaponAnims[ WeaponState ], 0, RenderObjClass::ANIM_MODE_ONCE );
					WeaponAnimControl.Set_Animation( WeaponAnims[ WeaponState ] );
					WeaponAnimControl.Set_Mode( ANIM_MODE_ONCE, 0 );
					WeaponAnimControl.Update(0);
				} else {
					WeaponState = -1;
				}
			}
		}

		if ( new_weapon_state == WEAPON_STATE_EXIT ) {
			WeaponState = -1; // force a change
		}

		// If playing a looping anim, switch if needed
		if ( (WeaponState == -1) || ( WeaponState == WEAPON_STATE_IDLE ) ) {
			if ( new_weapon_state != WeaponState ) {
				WeaponState = new_weapon_state;
                if(WeaponState==WEAPON_STATE_RELOAD && !VRHandTransform)Select_Faction_Reload(weapon);


				float anim_blend_time = 0.15f;
				if ( new_weapon_state == WEAPON_STATE_FIRE ) {
					anim_blend_time = 0;	// No blend when firing
				}

				int mode = ( WeaponState == WEAPON_STATE_IDLE ) ? RenderObjClass::ANIM_MODE_LOOP : RenderObjClass::ANIM_MODE_ONCE;
				RenderObjClass * hands_model = HandsPhysObj->Peek_Model();
				if ( hands_model != NULL ) {
					mode = ( WeaponState == WEAPON_STATE_IDLE ) ? ANIM_MODE_LOOP : ANIM_MODE_ONCE;
//					hands_model->Set_Animation( HandsAnims[ WeaponState ], 0, mode );
					HandAnimControl.Set_Animation( HandsAnims[ WeaponState ], anim_blend_time );
					HandAnimControl.Set_Mode( (AnimMode)mode );
				}
//				WeaponModel->Set_Animation( WeaponAnims[ WeaponState ], 0, mode );
  				WeaponAnimControl.Set_Animation( WeaponAnims[ WeaponState ], anim_blend_time );
				mode = ( WeaponState == WEAPON_STATE_IDLE ) ? ANIM_MODE_LOOP : ANIM_MODE_ONCE;
  				WeaponAnimControl.Set_Mode( (AnimMode)mode );

//				if ( HandsAnims[ WeaponState ] != NULL ) {
//					Debug_Say(( "Playing %s\n", HandsAnims[ WeaponState ]->Get_Name() ));
//				}
			}
		}


		// Lets put the gun relative to the camera
		Matrix3D tm = COMBAT_CAMERA->Get_Transform();

		// Add bobing position
		if ( BobHTree != NULL && BobHAnim != NULL ) {
			BobFrame += TimeManager::Get_Frame_Seconds() * BobHAnim->Get_Frame_Rate();
			if ( BobFrame > BobHAnim->Get_Num_Frames()-1 ) {
				BobFrame -= BobHAnim->Get_Num_Frames()-1;
			}
#if 0 // no more programatic recoil
			tm.Translate( 0, 0, BobRecoil );

			if ( BobRecoil > 0 ) {
				BobRecoil -= TimeManager::Get_Frame_Real_Seconds() * 2;
				if ( BobRecoil < 0 ) {
					BobRecoil = 0;
				}
			}
#endif

			BobHTree->Anim_Update( tm, BobHAnim, BobFrame );

			// Now get the camera bone
//			Debug_Say(( "%f %f %f\n", tm.Get_Translation().X, tm.Get_Translation().Y, tm.Get_Translation().Z ));
			tm = BobHTree->Get_Transform( BobHTree->Get_Bone_Index( "CAMERA" ) );
//			Debug_Say(( "%f %f %f\n", tm.Get_Translation().X, tm.Get_Translation().Y, tm.Get_Translation().Z ));
		}

		// Convert from camera to world convention
		tm.Rotate_Z( DEG_TO_RADF(90.0) );
		tm.Rotate_Y( DEG_TO_RADF(90.0) );

		Vector3	fp_offset = HandsOffset + COMBAT_CAMERA->Get_First_Person_Offset_Tweak();
		tm.Translate( fp_offset );

		//WWASSERT(COMBAT_STAR != NULL);//TSS
		if (COMBAT_STAR != NULL) {
			//
			// COMBAT_STAR may be NULL during MP game intermission
			//
			Matrix3D obj_convention_camera = COMBAT_CAMERA->Get_Transform();
			obj_convention_camera.Rotate_Z( DEG_TO_RADF(90.0) );
			obj_convention_camera.Rotate_Y( DEG_TO_RADF(90.0) );

			Vector3	camera_space_target;
			Vector3	camera_space_source;
			Vector3	target_pos = COMBAT_STAR->Get_Targeting_Pos();
			
			// Always look at a point a constant distance ahead, to avoid the gun poping around
			target_pos = COMBAT_CAMERA->Get_Transform() * Vector3( 0,0,-100 );
			Matrix3D::Inverse_Transform_Vector( obj_convention_camera, target_pos, &camera_space_target );
			Matrix3D::Inverse_Transform_Vector( obj_convention_camera, tm.Get_Translation(), &camera_space_source );
			Matrix3D camera_space_aim;
			camera_space_aim.Obj_Look_At( camera_space_source, camera_space_target, 0 );
			Matrix3D::Multiply( obj_convention_camera, camera_space_aim, &tm );

		}

		HandsPhysObj->Set_Transform( tm );

		// Now, pull the hands backwards enough to clear any walls
		if ( WeaponModel && COMBAT_CAMERA) {

			const Matrix3D & muzzle_tm = WeaponModel->Get_Bone_Transform( "muzzlea0" );
			Vector3 ray_end = muzzle_tm * Vector3( 0.1f, 0, 0 );

			Vector3 ray_start = COMBAT_CAMERA->Get_Transform().Get_Translation();
			ray_start.Z = ray_end.Z;

			LineSegClass ray;
			ray.Set( ray_start, ray_end );

			CastResultStruct result;
			PhysRayCollisionTestClass raytest(ray, &result, BULLET_COLLISION_GROUP, COLLISION_TYPE_PROJECTILE);

			{ 
				if (COMBAT_STAR != NULL && COMBAT_STAR->Peek_Physical_Object()!=NULL) {
					COMBAT_STAR->Peek_Physical_Object()->Inc_Ignore_Counter();
				}
				WWPROFILE( "Cast Ray" );
				WWASSERT(COMBAT_SCENE != NULL);
				COMBAT_SCENE->Cast_Ray( raytest );
				if (COMBAT_STAR != NULL && COMBAT_STAR->Peek_Physical_Object()!=NULL) {
					COMBAT_STAR->Peek_Physical_Object()->Dec_Ignore_Counter();
				}
			}

/*			if ( raytest.CollidedPhysObj != NULL && raytest.CollidedPhysObj->Get_Observer() != NULL ) {
				DamageableGameObj * obj = ((CombatPhysObserverClass *)raytest.CollidedPhysObj->Get_Observer())->As_DamageableGameObj();
				if ( obj ) {
					Debug_Say(( "Hit %s\n", obj->Get_Definition().Get_Name() ));
				}
			}*/

			// Determine the Camera Target Point and object
			Vector3	collision_pt;
			ray.Compute_Point( raytest.Result->Fraction, &collision_pt );
			collision_pt -= ray_end;
			float pullback = collision_pt.Length();
//			Debug_Say(( "Pullback = %f\n", pullback ));
			tm.Translate( Vector3( -pullback, 0, 0 ) );

			HandsPhysObj->Set_Transform( tm );
		}

	}

    // Evaluate animation before sampling grip/muzzle bones. Querying the firing
    // muzzle also reapplies this pose using the latest tracking frame.
    if(VRHandTransform && WeaponState==WEAPON_STATE_IDLE){
        HandAnimControl.Set_Mode(ANIM_MODE_STOP,0);WeaponAnimControl.Set_Mode(ANIM_MODE_STOP,0);
    }
    if(Is_Source_Rifle())HandAnimControl.Set_Mode(ANIM_MODE_STOP,0);
    if(HandsPhysObj && HandsPhysObj->Peek_Model()) HandAnimControl.Update(TimeManager::Get_Frame_Seconds());
    if(WeaponModel) WeaponAnimControl.Update(TimeManager::Get_Frame_Seconds());
    Update_Source_Rifle();
    Apply_Flatscreen_Reload();
    Apply_MK23_Action();
    Apply_VR_Weapon_Pose();Update_VR_Magazine();
}

bool WeaponViewClass::Get_Rendered_Muzzle(Matrix3D *tm,int index){
    if(!WeaponModel || !tm)return false;const char *names[]={"muzzlea0","muzzlea1","muzzleb0","muzzleb1"};
    int bone=WeaponModel->Get_Bone_Index(names[index&3]);
    if(bone<=0)bone=WeaponModel->Get_Bone_Index(index>=2?"muzzleb0":"muzzlea0");
    if(bone<=0)return false;*tm=WeaponModel->Get_Bone_Transform(bone);return true;
}
bool WeaponViewClass::Get_VR_Muzzle(Matrix3D *tm,int index)
{
    if(!COMBAT_STAR || COMBAT_STAR->Get_Vehicle() || COMBAT_STAR->Get_Defense_Object()->Get_Health()<=0 || !Apply_VR_Weapon_Pose()) return false;
    const char *names[]={"muzzlea0","muzzlea1","muzzleb0","muzzleb1"};
    int bone=WeaponModel->Get_Bone_Index(names[index&3]);
    if(bone<=0) bone=WeaponModel->Get_Bone_Index("muzzlea0");
    if(bone<=0) return false;
    *tm=WeaponModel->Get_Bone_Transform(bone);
    return tm->Get_X_Vector().Length2()>0.5f;
}

bool WeaponViewClass::Has_VR_Manual_Action(){return Is_MK23() || Is_Source_Rifle();}
void WeaponViewClass::Hide_VR_For_Scope(bool hidden){
    static bool was_hidden=false;if(!HandsPhysObj || !HandsPhysObj->Peek_Model())return;
    if(hidden){was_hidden=HandsPhysObj->Peek_Model()->Is_Hidden()!=0;HandsPhysObj->Peek_Model()->Set_Hidden(true);}
    else HandsPhysObj->Peek_Model()->Set_Hidden(was_hidden);
}
bool WeaponViewClass::Get_VR_Scope(Matrix3D *pose,float *radius){
    if(!pose || !radius || !WeaponModel || !COMBAT_STAR || !COMBAT_STAR->Get_Weapon() || !COMBAT_STAR->Get_Weapon()->Get_Can_Snipe())return false;
    if(!Apply_VR_Weapon_Pose())return false;Matrix3D muzzle;if(!Get_VR_Muzzle(&muzzle))return false;
    Vector3 local;const char *name=WeaponModel->Get_Name();int bone=0;
    // Measured rear eyepiece rings in the installed stock meshes, in their bone frames.
    if(strstr(name,"RAMJ")){local.Set(.1816f,.1355f,.0025f);*radius=.025f;bone=WeaponModel->Get_Bone_Index("F_GM_SCOPE");}
    else if(strstr(name,"SNIP")){local.Set(.2222f,-.0445f,.0046f);*radius=.014f;bone=WeaponModel->Get_Bone_Index("F_GM_SNIP");}
    else return false;
    if(bone<=0)return false;Matrix3D frame=WeaponModel->Get_Bone_Transform(bone);
    Vector3 forward=-frame.Get_Y_Vector(),left=-frame.Get_Z_Vector(),up=frame.Get_X_Vector();
    *pose=Matrix3D(true);for(int r=0;r<3;++r){(*pose)[r][0]=forward[r];(*pose)[r][1]=left[r];(*pose)[r][2]=up[r];}
    pose->Set_Translation(frame*local);return true;
}
void WeaponViewClass::Set_VR_Action_State(int stage){_VRManualStage=stage;if(stage!=3)_VRActionProgress=0;}
void WeaponViewClass::Set_VR_Action_Progress(float progress){_VRActionProgress=WWMath::Clamp(progress,0.0f,1.0f);}
void WeaponViewClass::Set_VR_Magazine_Removed(bool removed)
{
    _VRMagazineRemoved=removed;
    if(ClipModel) ClipModel->Set_Hidden(removed);Update_VR_Magazine();
}

bool WeaponViewClass::Is_VR_Magazine_Removed()
{
    return _VRMagazineRemoved;
}

// Sample uncaptured animation bones: IK controls must not feed back into the weapon anchor.
bool WeaponViewClass::Get_VR_Hand_Pose(int bone, Matrix3D *pose)
{
    if(!HandsPhysObj || !HandsPhysObj->Peek_Model() || !pose) return false;
    const HTreeClass *tree=HandsPhysObj->Peek_Model()->Get_HTree();
    if(!tree || bone<0 || bone>=tree->Num_Pivots()) return false;
    HTreeClass sample(*tree);
    for(int b=0;b<sample.Num_Pivots();++b) sample.Release_Bone(b);
    Matrix3D identity(true);
    if(WeaponState>=0 && WeaponState<NUM_WEAPON_STATES && HandsAnims[WeaponState]) sample.Anim_Update(identity,HandsAnims[WeaponState],VRHandTransform && WeaponState==WEAPON_STATE_IDLE ? 0 : HandAnimControl.Get_Current_Frame());
    else sample.Base_Update(identity);
    *pose=sample.Get_Transform(bone);
    if(Is_Source_Rifle() && strstr(tree->Get_Bone_Name(bone),"L HAND")){
        int grip=WeaponModel->Get_Bone_Index("GRIP_L"),gun=sample.Get_Bone_Index("GUNBONE");
        if(grip>0 && gun>0){HTreeClass source(*WeaponModel->Get_HTree());for(int i=0;i<source.Num_Pivots();++i)source.Release_Bone(i);source.Base_Update(identity);
            pose->Set_Translation(sample.Get_Transform(gun)*source.Get_Transform(grip).Get_Translation());}
    }
    return true;
}

bool WeaponViewClass::Get_VR_Gun_Frame(Matrix3D *pose)
{
    if(!WeaponModel || !pose)return false;
    *pose=WeaponModel->Get_Transform();return true;
}

bool WeaponViewClass::Get_VR_Magazine_Geometry(Matrix3D *relative,Vector3 *center)
{
    if(!WeaponModel || !relative || !center)return false;
    int mag=Magazine_Bone();
    if(mag){
        // The extracted group retains gun-local coordinates. Its visible centre is the insertion target.
        *relative=Matrix3D(true);Vector3 lo(1e9f,1e9f,1e9f),hi(-1e9f,-1e9f,-1e9f);bool found=false;
        Matrix3D inv;WeaponModel->Get_Transform().Get_Orthogonal_Inverse(inv);
        for(int i=0;i<WeaponModel->Get_Num_Sub_Objects();++i){RenderObjClass *part=WeaponModel->Get_Sub_Object(i);
            if(Magazine_Part(WeaponModel,part,mag)){
                AABoxClass box;part->Get_Obj_Space_Bounding_Box(box);Matrix3D local;Matrix3D::Multiply(inv,part->Get_Transform(),&local);
                for(int c=0;c<8;++c){Vector3 p=local*(box.Center+Vector3((c&1)?box.Extent.X:-box.Extent.X,(c&2)?box.Extent.Y:-box.Extent.Y,(c&4)?box.Extent.Z:-box.Extent.Z));
                    for(int a=0;a<3;++a){lo[a]=WWMath::Min(lo[a],p[a]);hi[a]=WWMath::Max(hi[a],p[a]);}}found=true;
            }if(part)part->Release_Ref();}
        if(found){*center=(lo+hi)*.5f;return true;}return false;
    }
    if(!ClipModel)return false;
    Matrix3D inverse;WeaponModel->Get_Transform().Get_Orthogonal_Inverse(inverse);
    Matrix3D::Multiply(inverse,ClipModel->Get_Transform(),relative);
    // Rotation only: part scale must not be applied a second time by its holder.
    Vector3 x=relative->Get_X_Vector(),y=relative->Get_Y_Vector();
    if(x.Length2()<.0001f || y.Length2()<.0001f)return false;
    x.Normalize();y-=x*Vector3::Dot_Product(x,y);if(y.Length2()<.0001f)return false;y.Normalize();
    Vector3 z;Vector3::Cross_Product(x,y,&z);
    for(int r=0;r<3;++r){(*relative)[r][0]=x[r];(*relative)[r][1]=y[r];(*relative)[r][2]=z[r];}
    relative->Set_Translation(Vector3(0,0,0));
    AABoxClass box;ClipModel->Get_Obj_Space_Bounding_Box(box);*center=box.Center;
    return true;
}

bool WeaponViewClass::Get_VR_Reload_Points(Vector3 *feed,Vector3 *action)
{
    if(!WeaponModel || !feed || !action)return false;
    if(Is_Source_Rifle()){
        Apply_VR_Weapon_Pose();int f=WeaponModel->Get_Bone_Index("FEED"),a=WeaponModel->Get_Bone_Index("ACTION");
        if(f>0 && a>0){*feed=WeaponModel->Get_Bone_Transform(f).Get_Translation();*action=WeaponModel->Get_Bone_Transform(a).Get_Translation();return true;}
    }
    if(Magazine_Bone()){
        Apply_VR_Weapon_Pose();Matrix3D relative;Vector3 center;
        if(Get_VR_Magazine_Geometry(&relative,&center)){
            *feed=WeaponModel->Get_Transform()*center;*action=*feed;return true;
        }
    }
    if(Is_MK23()){
        Apply_VR_Weapon_Pose();int mag=WeaponModel->Get_Bone_Index("MAG"),slide=WeaponModel->Get_Bone_Index("SLIDE");
        if(mag>0 && slide>0){*feed=WeaponModel->Get_Bone_Transform(mag).Get_Translation();
            *action=WeaponModel->Get_Bone_Transform(slide).Get_Translation();return true;}
    }
    if(!HandsPhysObj || !HandsPhysObj->Peek_Model() || !HandsAnims[WEAPON_STATE_RELOAD] ||
        !strstr(HandsAnims[WEAPON_STATE_RELOAD]->Get_Name(),"RELOD")){
        Matrix3D gun=WeaponModel->Get_Transform();
        *feed=ClipModel?ClipModel->Get_Transform().Get_Translation():gun*Vector3(.06f,0,-.04f);
        *action=gun*Vector3(.03f,0,.08f);return true;
    }
    RenderObjClass *hands=HandsPhysObj->Peek_Model();
    const HTreeClass *tree=hands->Get_HTree();if(!tree) return false;
    static RenderObjClass *cached=NULL;static Vector3 local_feed,local_action;static char cached_name[128]="";
    if(cached!=WeaponModel || stricmp(cached_name,WeaponModel->Get_Name())) {
        strncpy(cached_name,WeaponModel->Get_Name(),sizeof(cached_name)-1);
        cached=WeaponModel;HTreeClass sample(*tree);Matrix3D identity(true);
        for(int i=0;i<sample.Num_Pivots();++i) if(sample.Is_Bone_Captured(i)) sample.Release_Bone(i);
        int gun=sample.Get_Bone_Index("GUNBONE"),clip=sample.Get_Bone_Index("CLIPBONE"),left=sample.Get_Bone_Index("F_ L HAND");
        if(gun<=0 || clip<=0 || left<=0) {
            cached=NULL;Matrix3D frame=WeaponModel->Get_Transform();
            *feed=ClipModel?ClipModel->Get_Transform().Get_Translation():frame*Vector3(.06f,0,-.04f);
            *action=frame*Vector3(.03f,0,.08f);return true;
        }
        HAnimClass *anim=HandsAnims[WEAPON_STATE_RELOAD];
        float best=1e9f;local_action.Set(0.03f,0,0.08f);
        bool was_visible=false,inserted=false;int insert_step=8;
        // The magazine disappears into the gun at the authored visibility
        // transition. Use closest approach only when no such track exists.
        for(int step=0;step<=20;++step) {
            sample.Anim_Update(identity,anim,(anim->Get_Num_Frames()-1)*step/20.0f);
            Matrix3D gun_tm=sample.Get_Transform(gun);Vector3 candidate;
            Matrix3D::Inverse_Transform_Vector(gun_tm,sample.Get_Transform(clip).Get_Translation(),&candidate);
            bool visible=sample.Get_Visibility(clip);
            if(step>=5 && step<=18 && !inserted && candidate.Length2()<best) {best=candidate.Length2();local_feed=candidate;}
            if(step>=5 && was_visible && !visible && !inserted) {local_feed=candidate;best=candidate.Length2();insert_step=step;inserted=true;}
            was_visible=visible;
        }
        sample.Anim_Update(identity,anim,(anim->Get_Num_Frames()-1)*WWMath::Clamp((insert_step+1)/20.0f,0.3f,0.8f));
        Matrix3D::Inverse_Transform_Vector(sample.Get_Transform(gun),sample.Get_Transform(left).Get_Translation(),&local_action);
        if(best>0.64f){
            cached=NULL;Matrix3D frame=WeaponModel->Get_Transform();
            *feed=ClipModel?ClipModel->Get_Transform().Get_Translation():frame*Vector3(.06f,0,-.04f);
            *action=frame*Vector3(.03f,0,.08f);return true;
        }
    }
    Apply_VR_Weapon_Pose();
    *feed=WeaponModel->Get_Transform()*local_feed;
    *action=WeaponModel->Get_Transform()*local_action;
    return true;
}

RenderObjClass *WeaponViewClass::Clone_VR_Magazine()
{
    int mag=Magazine_Bone();
    if(mag && WeaponModel){
        RenderObjClass *copy=WeaponModel->Clone();copy->Set_Animation();Matrix3D inverse;WeaponModel->Get_Transform().Get_Orthogonal_Inverse(inverse);
        // Freeze the seated pose and keep only the magazine subtree (including fluid/glass parts).
        for(int b=1;b<copy->Get_Num_Bones();++b){Matrix3D pose;Matrix3D::Multiply(inverse,WeaponModel->Get_Bone_Transform(b),&pose);
            copy->Capture_Bone(b);copy->Control_Bone_Root_Relative(b,pose);}
        for(int i=0;i<copy->Get_Num_Sub_Objects();++i){RenderObjClass *part=copy->Get_Sub_Object(i);
            bool keep=Magazine_Part(copy,part,mag);if(part){part->Set_Hidden(!keep);part->Set_Animation_Hidden(false);part->Release_Ref();}}
        copy->Set_Hidden(false);copy->Set_Animation_Hidden(false);VR_Two_Sided(copy);return copy;
    }
    if(!ClipModel) return new VRGripMeshClass(false,Get_VR_Reload_Kind()==2);
    RenderObjClass *copy=ClipModel->Clone();copy->Set_Hidden(false);copy->Set_Animation_Hidden(false);VR_Two_Sided(copy);return copy;
}

int WeaponViewClass::Get_VR_Reload_Kind()
{
    if(!WeaponModel)return 0;
    if(COMBAT_STAR && COMBAT_STAR->Get_Weapon() &&
       (COMBAT_STAR->Get_Weapon()->Get_Style()==WEAPON_HOLD_STYLE_C4 || COMBAT_STAR->Get_Weapon()->Get_Style()==WEAPON_HOLD_STYLE_BEACON))return 0;
    char name[128];lstrcpynA(name,WeaponModel->Get_Name(),sizeof(name));CharUpperBuffA(name,(DWORD)strlen(name));
    if(strstr(name,"SHOT"))return 2;
    if(Magazine_Bone() || ClipModel)return 1;
    return HandsAnims[WEAPON_STATE_RELOAD] && strstr(HandsAnims[WEAPON_STATE_RELOAD]->Get_Name(),"RELOD") ? 3 : 0;
}

void WeaponViewClass::Log_VR_Weapon_Audit()
{
    if(!WeaponModel || !HandsPhysObj || !HandsPhysObj->Peek_Model()) return;
    char path[MAX_PATH];GetModuleFileNameA(NULL,path,sizeof(path));char *sep=strrchr(path,'\\');if(!sep) return;strcpy(sep+1,"vr_weapon_audit.log");
    FILE *log=fopen(path,"a");if(!log) return;
    fprintf(log,"weapon %s clip %s reload %s frames %d\n",WeaponModel->Get_Name(),ClipModel?ClipModel->Get_Name():"none",HandsAnims[WEAPON_STATE_RELOAD]?HandsAnims[WEAPON_STATE_RELOAD]->Get_Name():"none",HandsAnims[WEAPON_STATE_RELOAD]?HandsAnims[WEAPON_STATE_RELOAD]->Get_Num_Frames():0);
    RenderObjClass *hands=HandsPhysObj->Peek_Model();const HTreeClass *tree=hands->Get_HTree();
    for(int i=0;i<hands->Get_Num_Sub_Objects();++i) {
        RenderObjClass *sub=hands->Get_Sub_Object(i);
        if(sub && sub->Class_ID()==RenderObjClass::CLASSID_MESH && strstr(sub->Get_Name(),"F_HM_HAVOC.")) {
            MeshModelClass *mesh=((MeshClass*)sub)->Peek_Model();TextureClass *tex=mesh->Peek_Texture(0);
            fprintf(log,"  loaded_hand %s vertices %d triangles %d texture %s size %d %d\n",sub->Get_Name(),mesh->Get_Vertex_Count(),mesh->Get_Polygon_Count(),tex?(const char*)tex->Get_Texture_Name():"none",tex?tex->Get_Width():0,tex?tex->Get_Height():0);
        }
        if(sub) sub->Release_Ref();
    }
    if(tree && HandsAnims[WEAPON_STATE_RELOAD]) {
        HTreeClass sample(*tree);Matrix3D id(true);
        for(int i=0;i<sample.Num_Pivots();++i) if(sample.Is_Bone_Captured(i)) sample.Release_Bone(i);
        int gun=sample.Get_Bone_Index("GUNBONE"),clip=sample.Get_Bone_Index("CLIPBONE"),hand=sample.Get_Bone_Index("F_ L HAND");
        if(gun>0 && clip>0 && hand>0) for(int n=0;n<=20;++n) {
            sample.Anim_Update(id,HandsAnims[WEAPON_STATE_RELOAD],(HandsAnims[WEAPON_STATE_RELOAD]->Get_Num_Frames()-1)*n/20.0f);
            Vector3 a,b;Matrix3D::Inverse_Transform_Vector(sample.Get_Transform(gun),sample.Get_Transform(clip).Get_Translation(),&a);Matrix3D::Inverse_Transform_Vector(sample.Get_Transform(gun),sample.Get_Transform(hand).Get_Translation(),&b);
            fprintf(log,"  %.2f clip %.4f %.4f %.4f left %.4f %.4f %.4f visible %d\n",n/20.0f,a.X,a.Y,a.Z,b.X,b.Y,b.Z,sample.Get_Visibility(clip));
        }
        if(gun>0 && HandsAnims[WEAPON_STATE_IDLE]) {
            sample.Anim_Update(id,HandsAnims[WEAPON_STATE_IDLE],0);
            const char *bones[]={"F_ R HAND","F_ R FINGER1","F_ R FINGER11","F_ R FINGER2","F_ R FINGER21","F_ R FINGER3","F_ R FINGER31"};
            for(int i=0;i<7;++i) {int bone=sample.Get_Bone_Index(bones[i]);if(bone>0) {Vector3 v;Matrix3D::Inverse_Transform_Vector(sample.Get_Transform(gun),sample.Get_Transform(bone).Get_Translation(),&v);fprintf(log,"  idle %s %.4f %.4f %.4f\n",bones[i],v.X,v.Y,v.Z);}}
        }
    }
    Matrix3D muzzle;Vector3 feed,action;
    fprintf(log,"  tracked muzzle %d reload points %d\n",Get_VR_Muzzle(&muzzle),Get_VR_Reload_Points(&feed,&action));
    fclose(log);
}

Vector3 WeaponViewClass::Get_Muzzle_Pos()
{
	if ( WeaponModel ) {
		return WeaponModel->Get_Bone_Transform( "muzzlea0" ).Get_Translation();
	} 

	return COMBAT_CAMERA->Get_Transform().Get_Translation();
}

const char * WeaponActionNames[ NUM_WEAPON_STATES ] = {
	"IDLE",		//WEAPON_STATE_IDLE,
	"FIRE",		//WEAPON_STATE_FIRE,
	"RELOD",		//WEAPON_STATE_RELOAD,
	"ENTER",		//WEAPON_STATE_ENTER,
	"EXIT",		//WEAPON_STATE_EXIT,
//	"EFIRE",		//WEAPON_STATE_SPIN_DOWN,
};

/*
** Get and release the Weapon specific assets
** Each weapon has a name ie: PIST
** The weapon model is in the form F_GM_<NAME> ie: F_GM_PIST
** The clip model is in the form F_CM_<NAME> ie: F_CM_PIST
** The weapon states are IDLE, FIRE, RELOAD, ENTER, EXIT
** For each weapon state, except ENTER and EXIT, we have a weapon anim tied to the weapon model (use IDLE anim for ENTER/EXIT )
** The weapon anims are in the form F_GA_<NAME>_<STATE> ie: F_GA_PIST_IDLE, F_GA_PIST_FIRE
** For each weapon state, we have a hand anim tied to the F_SKELETON
** The hand anims are in the form F_GA_<NAME>_<STATE> ie: F_GA_PIST_IDLE, F_GA_PIST_FIRE
** Weapon anims should be the same duration (number of frames) and the corresponding hand anim (except looping +IDLES)
*/
static void Calibrate_VR_Remote(RenderObjClass *hands)
{
    const int remote=WeaponModel->Get_Bone_Index("F_GM_REMOTE");
    const int left=hands->Get_Bone_Index("F_ L HAND");
    const int gun=hands->Get_Bone_Index("GUNBONE");
    if (remote<=0 || left<=0 || gun<=0 || !WeaponModel->Get_HTree() || !hands->Get_HTree()) return;

    const Matrix3D identity(true);
    HTreeClass hands_sample(*hands->Get_HTree());
    for (int bone=0;bone<hands_sample.Num_Pivots();++bone) hands_sample.Release_Bone(bone);
    HTreeClass weapon_sample(*WeaponModel->Get_HTree());
    for (int bone=0;bone<weapon_sample.Num_Pivots();++bone) weapon_sample.Release_Bone(bone);
    if (HandsAnims[WEAPON_STATE_IDLE]) hands_sample.Anim_Update(identity,HandsAnims[WEAPON_STATE_IDLE],0);
    else hands_sample.Base_Update(identity);
    if (WeaponAnims[WEAPON_STATE_IDLE]) weapon_sample.Anim_Update(identity,WeaponAnims[WEAPON_STATE_IDLE],0);
    else weapon_sample.Base_Update(identity);
    Matrix3D left_inverse, remote_idle;
    hands_sample.Get_Transform(left).Get_Orthogonal_Inverse(left_inverse);
    Matrix3D::Multiply(hands_sample.Get_Transform(gun),weapon_sample.Get_Transform(remote),&remote_idle);
    Matrix3D::Multiply(left_inverse,remote_idle,&VRRemoteGrip);
    VRRemoteBone=remote;
    VRRemoteHand=left;
    VRRemoteFinger=hands->Get_Bone_Index("F_ L FINGER2");
    VRRemoteKnuckle=hands->Get_Bone_Index("F_ L FINGER21");
}

static void	Aquire_Weapon_Assets( const WeaponClass * weapon )
{
	if ( HandsPhysObj == NULL ) {
		Aquire_Hands_Assets();
	} else {
		HandAnimControl.Set_Model( HandsPhysObj->Peek_Model() );
		HandAnimControl.Set_Animation( (HAnimClass*)NULL );
	}


	if ( HandsPhysObj == NULL || HandsPhysObj->Peek_Model() == NULL ) {
		return;
	}

	RenderObjClass * hands_model = HandsPhysObj->Peek_Model();

	StringClass weapon_name;

	if ( weapon ) {
//		Debug_Say(( "Loading Weapon from %s\n", weapon->Get_First_Person_Model_Name() ));
		WeaponModel = ::Create_Render_Obj_From_Filename( weapon->Get_First_Person_Model_Name() );
		LastMuzzleFlash = true;	// force it off
//		Debug_Say(( "Loaded %s\n", WeaponModel ? WeaponModel->Get_Name() : "NONE" ));

		// Use model name
		if ( WeaponModel != NULL ) {
			weapon_name = WeaponModel->Get_Name() + 5;
            if(weapon->Is_Faction_Rifle())weapon_name="RIFL";		// Get the weapon name from the model
			Debug_Say(( "weapon_name is %s\n", (const char *)weapon_name ));

			StringClass clip_name;
			clip_name = WeaponModel->Get_Name();
			if ( clip_name.Get_Length() > 4 ) {
				clip_name[2] = 'C';
				clip_name[3] = 'M';
			}
			// The remote C4 gun model already contains the tracked transmitter.
			// In flatscreen its separate clip prop participates in the stock animation.
			const bool tracked_remote = WeaponViewClass::VRHandTransform &&
				WeaponModel->Get_Bone_Index("F_GM_REMOTE") > 0;
			if (!tracked_remote) {
				ClipModel = WW3DAssetManager::Get_Instance()->Create_Render_Obj( clip_name );
			}
			if ( ClipModel != NULL ) {
				if(Is_MK23())WeaponModel->Add_Sub_Object_To_Bone(ClipModel,"MAG");
                else hands_model->Add_Sub_Object_To_Bone( ClipModel, "CLIPBONE" );
			}
		}
	}

	WeaponAnimControl.Set_Model( WeaponModel );
	WeaponAnimControl.Set_Animation( (HAnimClass*)NULL );

	if ( WeaponModel != NULL ) {

		// Add the model
		hands_model->Add_Sub_Object_To_Bone( WeaponModel, "GUNBONE" );

		HandsOffset = weapon->Get_First_Person_Model_Offset();

		StringClass weapon_htree_name;
		if ( WeaponModel->Get_HTree() != NULL ) {
			weapon_htree_name = WeaponModel->Get_HTree()->Get_Name();
		}

		SET_REF_OWNER( WeaponModel );

		for ( int i = 0; i < NUM_WEAPON_STATES; i++ ) {

			StringClass anim_name;

			// Get Weapon Anims, use the idle anim for enter and exit
			int state = ( i >= WEAPON_STATE_ENTER ) ? WEAPON_STATE_IDLE : i;
			anim_name.Format( "%s.F_GA_%s_%s", weapon_htree_name, weapon_name, WeaponActionNames[state] );
//			Debug_Say(( "Loading Weapon Anim %s\n", anim_name ));
			WeaponAnims[i] = WW3DAssetManager::Get_Instance()->Get_HAnim( anim_name );
			if ( WeaponAnims[i] == NULL ) {
				Debug_Say(( "Missing Weapon Anim %s\n", anim_name ));
			}

			// Get Hands Anims
			anim_name.Format( "F_SKELETON.F_HA_%s_%s", weapon_name, WeaponActionNames[i] );
//			Debug_Say(( "Loading Hands Anim %s\n", anim_name ));
			HandsAnims[i] = WW3DAssetManager::Get_Instance()->Get_HAnim( anim_name );
			if ( HandsAnims[i] == NULL ) {
				Debug_Say(( "Missing Hands Anim %s\n", anim_name ));
				HandsAnims[i] = WW3DAssetManager::Get_Instance()->Get_HAnim( "F_SKELETON.F_HA_PIST_IDLE" );
			}
		}
	}
    if(Is_Source_Rifle()){
        for(int i=0;i<NUM_WEAPON_STATES;++i){REF_PTR_RELEASE(HandsAnims[i]);
            HandsAnims[i]=WW3DAssetManager::Get_Instance()->Get_HAnim("F_SKELETON.F_HA_RIFL_IDLE");}
        if(WeaponViewClass::VRHandTransform){StringClass name;const char *tree=WeaponModel->Get_HTree()->Get_Name();name.Format("%s.%s_VRFIRE",tree,tree);
            HAnimClass *fire=WW3DAssetManager::Get_Instance()->Get_HAnim(name);if(fire){REF_PTR_RELEASE(WeaponAnims[WEAPON_STATE_FIRE]);WeaponAnims[WEAPON_STATE_FIRE]=fire;}}
    }

    if(WeaponViewClass::VRHandTransform) {
        if (WeaponModel) Calibrate_VR_Remote(hands_model);
        if(WeaponModel && (!stricmp(WeaponModel->Get_Name(),"F_GM_ROCK") || !stricmp(WeaponModel->Get_Name(),"F_GM_CHNG"))) {
            RenderObjClass *grip=new VRGripMeshClass(true);WeaponModel->Add_Sub_Object_To_Bone(grip,0);grip->Release_Ref();
        }
        VR_Two_Sided(hands_model);
    }
}

static void	Release_Weapon_Assets( void )
{
    VRRemoteBone=0;
    _VRMagazineRemoved=false;
	WeaponAnimControl.Set_Model( NULL );
	WeaponAnimControl.Set_Animation( (HAnimClass*)NULL );

	if ( WeaponModel != NULL ) {
		// Remove
		WWASSERT( HandsPhysObj != NULL );
		RenderObjClass * hands_model = HandsPhysObj->Peek_Model();
		WWASSERT( hands_model != NULL );
		hands_model->Remove_Sub_Objects_From_Bone( "GUNBONE" );

		WeaponModel->Release_Ref();
		WeaponModel = NULL;
	}

	if ( ClipModel != NULL ) {
		// Remove
		WWASSERT( HandsPhysObj != NULL );
		RenderObjClass * hands_model = HandsPhysObj->Peek_Model();
		WWASSERT( hands_model != NULL );
		hands_model->Remove_Sub_Objects_From_Bone( "CLIPBONE" );

		ClipModel->Release_Ref();
		ClipModel = NULL;
	}

	for ( int i = 0; i < NUM_WEAPON_STATES; i++ ) {
		if ( WeaponAnims[i] != NULL ) {
			WeaponAnims[i]->Release_Ref();
			WeaponAnims[i] = NULL;
		}

		if ( HandsAnims[i] != NULL ) {
			HandsAnims[i]->Release_Ref();
			HandsAnims[i] = NULL;
		}
	}
}

/*
** Get and release the Hand assets
** All hand models are built on the F_SKELETON HTree, which has a GUNBONE and CLIPBONE
** The hand models can be different for each player model, and are in the form F_HM_XXX ie: F_HM_HAVOC
** The hand anims are created from the particular weapon model loaded.
*/
int WeaponViewClass::Get_VR_EVA_Surface(bool compass, const VREvaVertex **vertices, Matrix3D *pose)
{
    const auto &surface = compass ? VREvaCompass : VREvaMain;
    if (!HandsPhysObj || !HandsPhysObj->Peek_Model() || !VREvaBone || surface.empty()) return 0;
    RenderObjClass *hands = HandsPhysObj->Peek_Model();
    if (!hands->Is_Not_Hidden_At_All() || !COMBAT_SCENE || !COMBAT_SCENE->Contains(HandsPhysObj)) return 0;
    *pose = hands->Get_Bone_Transform(VREvaBone);
    *vertices = surface.data();
    return static_cast<int>(surface.size());
}

static void VR_Clip_EVA_Surface(MeshModelClass &model, int bone, const RectClass &region,
    std::vector<VREvaVertex> &surface)
{
    const Vector2 *uv = model.Get_UV_Array();
    if (!uv) return;
    const Vector3 *vertices = model.Get_Vertex_Array();
    const uint16 *links = model.Get_Vertex_Bone_Links();
    const TriIndex *triangles = model.Get_Polygon_Array();
    for (int face=0;face<model.Get_Polygon_Count();++face) {
        VREvaVertex polygon[8];
        bool on_forearm=true;
        for (int i=0;i<3;++i) {
            const int v=triangles[face][i];
            on_forearm &= links[v]==bone;
            // Invert W3D's loaded V back to authored texture coordinates.
            polygon[i]={vertices[v], Vector2(uv[v].X,1.f-uv[v].Y)};
        }
        if (!on_forearm) continue;
        const Vector3 a=polygon[1].Position-polygon[0].Position;
        const Vector3 b=polygon[2].Position-polygon[0].Position;
        Vector3 normal=Vector3::Cross_Product(a,b);
        if (normal.Length2()<1e-12f) continue;
        normal.Normalize();

        // Clip to the screen footprint on the actual deformed-in-bind-pose skin.
        int count=3;
        const float edges[]={region.Left,region.Right,region.Top,region.Bottom};
        for (int edge=0;edge<4 && count>=3;++edge) {
            const int axis=edge/2;
            const float inward=(edge&1) ? -1.f : 1.f;
            VREvaVertex clipped[8];
            int size=0;
            for (int i=0;i<count;++i) {
                const VREvaVertex &start=polygon[i], &end=polygon[(i+1)%count];
                const float da=(start.UV[axis]-edges[edge])*inward;
                const float db=(end.UV[axis]-edges[edge])*inward;
                if (da>=0.f) clipped[size++]=start;
                if ((da>0.f && db<0.f) || (da<0.f && db>0.f)) {
                    const float t=da/(da-db);
                    clipped[size++]={start.Position+(end.Position-start.Position)*t,
                        start.UV+(end.UV-start.UV)*t};
                }
            }
            count=size;
            for (int i=0;i<count;++i) polygon[i]=clipped[i];
        }
        for (int i=0;i<count;++i) {
            polygon[i].Position+=normal*.00025f;
            polygon[i].UV.Set((polygon[i].UV.Y-region.Top)/region.Height(),
                (polygon[i].UV.X-region.Left)/region.Width());
        }
        for (int i=1;i+1<count;++i) {
            surface.push_back(polygon[0]);
            surface.push_back(polygon[i]);
            surface.push_back(polygon[i+1]);
        }
    }
}

static void VR_Fit_Forearms(RenderObjClass *object,int left,int right)
{
    if (object->Class_ID() == RenderObjClass::CLASSID_MESH) {
        MeshClass *mesh = static_cast<MeshClass*>(object);
        if (mesh->Peek_Model()->Get_Vertex_Bone_Links()) {
            mesh->Make_Unique();
            MeshModelClass *model = mesh->Peek_Model();
            model->Make_Geometry_Unique();
            const uint16 *links = model->Get_Vertex_Bone_Links();
            Vector3 *vertices = model->Get_Vertex_Array();
            Vector3 *normals = const_cast<Vector3*>(model->Get_Vertex_Normal_Array());
            const bool eva = strstr(mesh->Get_Name(),"LEFT_FOREARM01") != NULL;
            for (int i=0;i<model->Get_Vertex_Count();++i) {
                if (links[i] != left && links[i] != right) continue;
                Vector3 &v = vertices[i];
                // Compress the off-camera extension into a short overlap at the elbow.
                if (v.X < 0.f) {
                    v.X *= .075f;
                    v.Y *= .93f;
                    v.Z *= .93f;
                }
                if (eva && links[i] == left) {
                    const float angle = VR_EVA_Mount_Angle(v.X);
                    v = VR_Rotate_Forearm_X(v,angle);
                    normals[i] = VR_Rotate_Forearm_X(normals[i],angle);
                }
            }
            if (eva) {
                VREvaBone=left;
                VR_Clip_EVA_Surface(*model,left,RectClass(.680f,.640f,.835f,.930f),VREvaMain);
                VR_Clip_EVA_Surface(*model,left,RectClass(.681f,.376f,.840f,.548f),VREvaCompass);
            }
            model->Set_Flag(MeshModelClass::DIRTY_BOUNDS,true);
            model->Set_Flag(MeshModelClass::DIRTY_PLANES,true);
            mesh->Set_Transform(mesh->Get_Transform());
        }
    }
    for (int i=0;i<object->Get_Num_Sub_Objects();++i) {
        RenderObjClass *sub = object->Get_Sub_Object(i);
        if (sub) {
            VR_Fit_Forearms(sub,left,right);
            sub->Release_Ref();
        }
    }
}

static void	Aquire_Hands_Assets( void )
{
	StringClass name = COMBAT_STAR->Get_First_Person_Hands_Model_Name();
    if(WeaponViewClass::VRHandTransform && COMBAT_STAR->Peek_Model()) {
        StringClass body=COMBAT_STAR->Peek_Model()->Get_Name();_strupr(&body[0]);
        if(strstr(body,"HAVOC")) name="f_hm_havoc.w3d";
    }
//	Debug_Say(( "Loading Hands from %s\n", name ));

	HandsPhysObj = new DecorationPhysClass();
	if ( HandsPhysObj != NULL ) {
		HandsPhysObj->Set_Collision_Group( UNCOLLIDEABLE_GROUP );

		RenderObjClass * model = ::Create_Render_Obj_From_Filename( name );
		if ( model != NULL ) {
			if (WeaponViewClass::VRHandTransform && !stricmp(name,"f_hm_havoc.w3d")) {
                const int left = model->Get_Bone_Index("F_ L FOREARM");
                const int right = model->Get_Bone_Index("F_ R FOREARM");
                if (left > 0 && right > 0) VR_Fit_Forearms(model,left,right);
            }
            HandAnimControl.Set_Model( model );
			HandAnimControl.Set_Animation( (HAnimClass*)NULL );
			HandsPhysObj->Set_Model( model );
			model->Release_Ref();
		}
	}
}

static void	Release_Hands_Assets( void )
{
    VREvaMain.clear();
    VREvaCompass.clear();
    VREvaBone=0;
	HandAnimControl.Set_Model( NULL );
	HandAnimControl.Set_Animation( (HAnimClass*)NULL );

	if ( HandsPhysObj != NULL ) {
		if ( COMBAT_SCENE->Contains( HandsPhysObj ) ) {
			COMBAT_SCENE->Remove_Object( HandsPhysObj );
		}
		HandsPhysObj->Release_Ref();
		HandsPhysObj = NULL;
	}
}


/*
**
*/
static const char * BobNames[] = {
	"",				// BOB_NONE
	"F_CA_IDLE",	// BOB_IDLE
	"F_CA_WALK",	// BOB_WALK
	"F_CA_RUN",		// BOB_RUN
};

static void	Set_Bob( int bob_state )
{
	if ( BobState != bob_state ) {
		BobState = bob_state;

		// Release the old copys, if any
		BobHTree = NULL;
		REF_PTR_RELEASE( BobHAnim );

		StringClass name(BobNames[ BobState ],true);

//		Debug_Say(( "New Camera Bob State %d \"%s\"\n", BobState, name ));

		if ( !name.Is_Empty() ) {
			BobHTree = WW3DAssetManager::Get_Instance()->Get_HTree( name );
			StringClass anim(0,true);
			anim.Format( "%s.%s", name,name );
			BobHAnim = WW3DAssetManager::Get_Instance()->Get_HAnim( anim );
		}
	}
}


static void	Set_Bob_Recoil( float amount )		
{ 
	BobRecoil = amount; 
}

