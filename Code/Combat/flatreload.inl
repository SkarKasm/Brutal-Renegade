// Authored model-space flat reloads. Native RIFL reload motion is never sampled.
// Both the magazine and the grasping hand are children of one magazine transform.
static Matrix3D Flat_Mul(const Matrix3D &a,const Matrix3D &b){Matrix3D c;Matrix3D::Multiply(a,b,&c);return c;}
static Matrix3D Flat_Inv(const Matrix3D &a){Matrix3D b;a.Get_Orthogonal_Inverse(b);return b;}
static float Flat_Ease(float x){x=WWMath::Clamp(x,0.0f,1.0f);return x*x*(3-2*x);}
struct FlatReloadKey {float t,down,back,rock;};
static const FlatReloadKey FlatM4Keys[]={
 {0,0,0,0},{.18f,0,0,0},{.30f,-.20f,.015f,0},{.40f,-.48f,.08f,0},
 {.48f,-.48f,.08f,0},{.62f,-.16f,0,0},{.76f,0,0,0},{1,0,0,0}};
static const FlatReloadKey FlatAKKeys[]={
 {0,0,0,0},{.18f,0,0,0},{.25f,-.025f,.025f,24},{.34f,-.20f,.08f,35},
 {.42f,-.48f,.12f,35},{.49f,-.48f,.08f,26},{.64f,-.09f,.025f,25},
 {.71f,-.025f,.015f,20},{.79f,0,0,0},{1,0,0,0}};
static bool _FlatRiflePose=false,_FlatRifleEmpty=false;
static Matrix3D Flat_Segment(const Vector3 &a,const Vector3 &b,const Vector3 &roll){
 Vector3 x=b-a;if(x.Length2()<.000001f)x.Set(1,0,0);x.Normalize();
 Vector3 y=roll-x*Vector3::Dot_Product(roll,x);
 if(y.Length2()<.000001f)y=Vector3(0,0,1)-x*x.Z;
 if(y.Length2()<.000001f)y=Vector3(0,1,0)-x*x.Y;
 y.Normalize();Vector3 z;Vector3::Cross_Product(x,y,&z);Matrix3D m(true);
 for(int i=0;i<3;++i){m[i][0]=x[i];m[i][1]=y[i];m[i][2]=z[i];}m.Set_Translation(a);return m;
}
static void Flat_Pose_Arm(RenderObjClass *hands,const HTreeClass &idle,bool left,const Matrix3D &wrist){
 int hand=hands->Get_Bone_Index(left?"F_ L HAND":"F_ R HAND");
 int fore=hands->Get_Bone_Index(left?"F_ L FOREARM":"F_ R FOREARM");if(hand<=0 || fore<=0)return;
 Matrix3D relative=Flat_Inv(idle.Get_Transform(hand));
 // Preserve the idle finger curl relative to the new wrist; never leave fingers
 // driven by the original reload animation after the palm has moved.
 for(int b=1;b<hands->Get_Num_Bones();++b){
  bool child=false;for(int a=b;a>0;a=hands->Get_HTree()->Get_Parent_Index(a))if(a==hand){child=true;break;}
  if(child){hands->Capture_Bone(b);hands->Control_Bone_Root_Relative(b,Flat_Mul(wrist,Flat_Mul(relative,idle.Get_Transform(b))));}
 }
 Vector3 elbow=idle.Get_Transform(fore).Get_Translation(),target=wrist.Get_Translation();
 float length=(idle.Get_Transform(hand).Get_Translation()-elbow).Length();
 Vector3 axis=target-elbow;if(axis.Length2()<.000001f)axis.Set(1,0,0);axis.Normalize();
 elbow=target-axis*length;
 Matrix3D arm=Flat_Segment(elbow,target,idle.Get_Transform(fore).Get_Y_Vector());
 hands->Capture_Bone(fore);hands->Control_Bone_Root_Relative(fore,arm);
}
static void Apply_Authored_Rifle_Reload(){
 if(WeaponViewClass::VRHandTransform || !HandsPhysObj || !HandsPhysObj->Peek_Model() || !WeaponModel)return;
 RenderObjClass *hands=HandsPhysObj->Peek_Model();
 const char *tree=WeaponModel->Get_HTree()?WeaponModel->Get_HTree()->Get_Name():"";
 bool m4=!stricmp(tree,"BR_M4"),ak=!stricmp(tree,"BR_AK");
 bool reload=(m4||ak) && WeaponState==WEAPON_STATE_RELOAD && HandsAnims[WEAPON_STATE_RELOAD];
 if(!reload){
  if(_FlatRiflePose){
   for(int b=1;b<hands->Get_Num_Bones();++b)hands->Release_Bone(b);
   int mag=Magazine_Bone(),bolt=WeaponModel->Get_Bone_Index("BOLT");
   if(mag>0)WeaponModel->Release_Bone(mag);if(bolt>0)WeaponModel->Release_Bone(bolt);
   _FlatRiflePose=false;
  }return;
 }
 if(!_FlatRiflePose){_FlatRifleEmpty=COMBAT_STAR && COMBAT_STAR->Get_Weapon() && COMBAT_STAR->Get_Weapon()->Get_Clip_Rounds()==0;}
 _FlatRiflePose=true;
 HTreeClass idle(*hands->Get_HTree());for(int b=0;b<idle.Num_Pivots();++b)idle.Release_Bone(b);
 Matrix3D id(true);idle.Anim_Update(id,HandsAnims[WEAPON_STATE_IDLE],0);
 int gun=hands->Get_Bone_Index("GUNBONE"),left=hands->Get_Bone_Index("F_ L HAND"),right=hands->Get_Bone_Index("F_ R HAND");
 int mag=Magazine_Bone();if(gun<=0 || left<=0 || right<=0 || mag<=0)return;
 float t=WWMath::Clamp(HandAnimControl.Get_Current_Frame()/WWMath::Max(1.0f,(float)HandsAnims[WEAPON_STATE_RELOAD]->Get_Num_Frames()-1),0.0f,1.0f);
 float tilt=Flat_Ease(t/.14f)*(1-Flat_Ease((t-.93f)/.07f));
 Matrix3D motion(true);motion.Rotate_Y(DEG_TO_RADF((m4?-24.0f:22.0f)*tilt));motion.Rotate_Z(DEG_TO_RADF(10*tilt));
 motion.Set_Translation(Vector3(.015f*tilt,.015f*tilt,0));
 Matrix3D gunpose=Flat_Mul(idle.Get_Transform(gun),motion);
 Matrix3D rootmotion=Flat_Mul(gunpose,Flat_Inv(idle.Get_Transform(gun)));
 hands->Capture_Bone(gun);hands->Control_Bone_Root_Relative(gun,gunpose);
 Flat_Pose_Arm(hands,idle,false,Flat_Mul(rootmotion,idle.Get_Transform(right)));
 HTreeClass bind(*WeaponModel->Get_HTree());for(int b=0;b<bind.Num_Pivots();++b)bind.Release_Bone(b);bind.Base_Update(id);
 const FlatReloadKey *keys=m4?FlatM4Keys:FlatAKKeys;int count=m4?sizeof(FlatM4Keys)/sizeof(*keys):sizeof(FlatAKKeys)/sizeof(*keys);
 int k=0;while(k<count-2 && t>keys[k+1].t)++k;
 float a=Flat_Ease((t-keys[k].t)/(keys[k+1].t-keys[k].t));
 float down=keys[k].down+(keys[k+1].down-keys[k].down)*a,back=keys[k].back+(keys[k+1].back-keys[k].back)*a;
 float rock=keys[k].rock+(keys[k+1].rock-keys[k].rock)*a;
 Matrix3D magpose(true);magpose.Rotate_Z(DEG_TO_RADF(rock));magpose.Set_Translation(bind.Get_Transform(mag).Get_Translation()+Vector3(down,back,0));
 WeaponModel->Capture_Bone(mag);WeaponModel->Control_Bone_Root_Relative(mag,magpose);
 for(int i=0;i<WeaponModel->Get_Num_Sub_Objects();++i){RenderObjClass *part=WeaponModel->Get_Sub_Object(i);
  if(part && Magazine_Part(WeaponModel,part,mag)){part->Set_Hidden(t>.405f && t<.475f);part->Set_Animation_Hidden(false);}if(part)part->Release_Ref();}
 if(ClipModel)ClipModel->Set_Hidden(true);
 Matrix3D support=Flat_Mul(Flat_Inv(idle.Get_Transform(gun)),idle.Get_Transform(left));
 Matrix3D grasp= support;Matrix3D turn(true);turn.Rotate_Z(DEG_TO_RADF(80));grasp=Flat_Mul(turn,grasp);
 grasp.Set_Translation(Vector3(-.025f,.065f,-.045f));
 if(_FlatRifleEmpty){
  float travel=m4?1-Flat_Ease((t-.88f)/.05f):Flat_Ease((t-.86f)/.025f)*(1-Flat_Ease((t-.91f)/.025f));
  int bolt=WeaponModel->Get_Bone_Index("BOLT");if(bolt>0){Matrix3D pose=bind.Get_Transform(bolt);pose.Set_Translation(pose.Get_Translation()+Vector3(0,.035f*travel,0));WeaponModel->Capture_Bone(bolt);WeaponModel->Control_Bone_Root_Relative(bolt,pose);}
 }
 Matrix3D held=Flat_Mul(magpose,grasp),wrist=held;
 if(t<.18f)wrist=Lerp(support,held,Flat_Ease(t/.18f));
 else if(t>.80f){
  if(_FlatRifleEmpty){
   Matrix3D action=grasp;action.Set_Translation(m4?Vector3(.055f,-.065f,-.045f):Vector3(.06f,-.10f,.055f));
   float pull=Flat_Ease((t-.86f)/.025f)*(1-Flat_Ease((t-.91f)/.025f));
   action.Set_Translation(action.Get_Translation()+(ak?Vector3(0,.035f*pull,0):Vector3(0,0,.010f*pull)));
   wrist=t<.86f?Lerp(held,action,Flat_Ease((t-.80f)/.06f)):Lerp(action,support,Flat_Ease((t-.94f)/.06f));
  }else wrist=Lerp(held,support,Flat_Ease((t-.80f)/.20f));
 }
 Flat_Pose_Arm(hands,idle,true,Flat_Mul(gunpose,wrist));
}
