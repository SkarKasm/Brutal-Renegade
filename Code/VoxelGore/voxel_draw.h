// GPL-3.0-or-later. Per-instance DX8-ready geometry, no graphics API dependency.
#ifndef BRVG_DRAW_H
#define BRVG_DRAW_H
#include "renegade_voxel.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct BRVG_RenderVertex {float xyz[3],normal[3];uint32_t diffuse;float uv0[2],uv1[2];} BRVG_RenderVertex;
typedef struct {uint32_t material_group,first_index,index_count;} BRVG_DrawRange;
typedef struct BRVG_RenderPacket BRVG_RenderPacket;
typedef struct {
    uint32_t vertex_count,index_count,range_count;
    const BRVG_RenderVertex *vertices;
    const uint16_t *indices;
    const BRVG_DrawRange *ranges;
    const uint32_t *source_triangles; // One original triangle ID per output index triple.
} BRVG_PacketView;
// One material pass at a time. triangle_groups preserves the engine's exact
// source triangle -> material/texture/shader bucket, not a guessed UV grouping.
// NULL on wrong LOD/identity or insufficient 16-bit headroom: draw intact.
BRVG_RenderPacket *BRVG_BuildPacket(const BRVG_SurfaceView *cut,uint64_t identity,
    uint32_t vertex_count,uint32_t triangle_count,const uint32_t *source_indices,
    const BRVG_RenderVertex *posed_vertices,const uint32_t *triangle_groups);
const BRVG_PacketView *BRVG_GetPacket(const BRVG_RenderPacket *packet);
void BRVG_FreePacket(BRVG_RenderPacket *packet);
#ifdef __cplusplus
}
#endif
#endif
