// Cosmetic W3D bridge. All coordinates passed to Hit are world-space metres.
#ifndef BR_VOXEL_SKIN_H
#define BR_VOXEL_SKIN_H
class RenderObjClass;
class MeshClass;
class RenderInfoClass;
class Vector3;
class Matrix3D;
struct BRVG_RenderVertex;
namespace VoxelSkin {
typedef void (*AddPiece)(RenderObjClass *);
typedef void (*MovePiece)(RenderObjClass *,const Matrix3D &);
typedef bool (*TerrainRay)(const Vector3 &,const Vector3 &,Vector3 *,Vector3 *);
void Set_Piece_Callbacks(AddPiece add,MovePiece move,TerrainRay ray);
// Offline integration harness only. NULL is the normal real-DX8 backend.
typedef bool (*OfflineDrawSink)(const BRVG_RenderVertex *,unsigned,const unsigned short *,unsigned);
void Set_Offline_Draw_Sink(OfflineDrawSink sink);
void Configure(bool enabled,const char *ini_path);
void Shutdown();
void Clear_Level();
void Tick(float dt);
bool Prewarm(RenderObjClass *actor);
bool Hit(RenderObjClass *actor,const Vector3 &point,const Vector3 &direction,float damage,int ammo_id,
         bool head_hit=false,const Vector3 *contact_normal=nullptr,const char *leg_bone_hint=nullptr,
         bool corpse_hit=false);
bool Blast(RenderObjClass *actor,const Vector3 &point,const Vector3 &direction,
           const char *bone,float radius,float depth,bool sever,bool corpse);
bool Burn(RenderObjClass *actor,const Vector3 &point,const Vector3 &normal,const char *bone,
          unsigned region,float radius,int stage,bool wholeRegion);
void Clear_Burn(RenderObjClass *actor);
bool Manages(const RenderObjClass *actor);
bool Is_Enabled();
bool Render(MeshClass *mesh,RenderInfoClass &info);
}
#endif
