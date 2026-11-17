// GPL-3.0-or-later. Format-neutral CPU adapter. Native pointers, fixed-width
// counts; all allocation and ownership remain inside this module. Single owner
// thread. Draw snapshots stay immutable until explicitly released.
#ifndef RENEGADE_VOXEL_H
#define RENEGADE_VOXEL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BRVG_ABI_VERSION 1u
#define BRVG_DEFAULT_BODY_CELL_M 0.01f
#define BRVG_DEFAULT_HEAD_CELL_M 0.0075f
typedef uint32_t BRVG_Model;
typedef uint32_t BRVG_Body;
typedef struct BRVG_Snapshot BRVG_Snapshot;
typedef struct {
    uint64_t identity; // exact mesh/revision/LOD identity, supplied by engine
    uint32_t vertex_count,triangle_count;
    const float *rest_xyz,*rest_normals; // xyz, model bind space, metres
    const uint16_t *bones;
    const uint32_t *indices;
} BRVG_SurfaceDesc;
typedef struct {
    uint32_t size,version,bone_count,surface_count;
    const float *rest_palette; // per bone: 9 row-major rotation + 3 translation
    const int32_t *parents;
    const char *bone_names; // bone_count records, each 32 bytes
    const BRVG_SurfaceDesc *surfaces;
    float cell_m; // zero = 1cm body / 7.5mm separately scoped rigid head
    uint32_t rigid_head;
} BRVG_ModelDesc;
typedef struct {
    uint64_t identity;
    uint32_t vertex_count,triangle_count;
    int32_t keep_count; // -1 means intact
    const int32_t *keep;
    uint32_t sub_count;
    const float *sub; // original triangle ID + 3x3 barycentric corners
} BRVG_SurfaceView;
typedef struct {
    uint32_t version,surface_count,two_sided;
    const BRVG_SurfaceView *surfaces;
    uint32_t flesh_vertex_count,flesh_index_count;
    const float *flesh_xyz,*flesh_normals;
    const uint8_t *flesh_rgba;
    const int32_t *flesh_indices;
} BRVG_DrawView;
typedef struct {
    int32_t cells,bone_cells,exited,bone;
    float rest_entry[3],rest_exit[3],rest_direction[3];
} BRVG_HitResult;
typedef struct { BRVG_Body body;int32_t bone;float rest_center[3]; } BRVG_Piece;
typedef struct { uint32_t model_count,body_count,snapshot_count;uint64_t owned_bytes,rebuild_count,pose_cache_hits; } BRVG_Stats;

BRVG_Model BRVG_CreateModel(const BRVG_ModelDesc *desc);
int BRVG_FreeModel(BRVG_Model model); // Releases an unused template. Fails while a body uses it.
BRVG_Body BRVG_CreateBody(BRVG_Model model);
int BRVG_CompactBody(BRVG_Body body); // Keep edits, release idle render data and dense mutable cells.
void BRVG_FreeBody(BRVG_Body body);
void BRVG_Shutdown(void); // outstanding draw snapshots remain releasable
void BRVG_BeginFrame(uint64_t frame); // same token for both VR eyes
int BRVG_Shot(BRVG_Body body,const float *pose,uint32_t bones,const float point[3],const float direction[3],float radius_in,float radius_out,float energy,BRVG_HitResult *result);
int BRVG_ShotOnBone(BRVG_Body body,const float *pose,uint32_t bones,int32_t hit_bone,const float point[3],const float direction[3],float radius_in,float radius_out,float energy,BRVG_HitResult *result);
// World-space ellipsoid aligned to a posed bone and blast/heat direction.
int BRVG_Erode(BRVG_Body body,const float *pose,uint32_t bones,int32_t bone,
              const float center[3],const float direction[3],float radius,float depth,int skin_only,int limb_cut);
// Call after each successful carve, on living as well as dead soldiers. This
// adapter emits cosmetic pieces only; SP reactions belong to Combat, not here.
uint32_t BRVG_Separate(BRVG_Body body,BRVG_Piece *pieces,uint32_t capacity);
int BRVG_PosePoint(BRVG_Body body,const float *pose,uint32_t bones,int32_t bone,const float rest[3],float posed[3]);
const BRVG_Snapshot *BRVG_AcquireDraw(BRVG_Body body,const float *pose,uint32_t bones,const float light[9],int visible);
const BRVG_DrawView *BRVG_GetDraw(const BRVG_Snapshot *snapshot);
void BRVG_ReleaseDraw(const BRVG_Snapshot *snapshot);
BRVG_Stats BRVG_GetStats(void);
const char *BRVG_LastError(void);
#ifdef __cplusplus
}
#endif
#endif
