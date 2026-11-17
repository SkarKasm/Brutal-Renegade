// GPL-3.0-or-later. CPU-only executable: actual HTree loader/base/capture and
// vector processor, read-only fixture inputs. No WW3D initialization or game.
#include "w3d_reader.h"
#include "always.h"
#include "ramfile.h"
#include "chunkio.h"
#include "htree.h"
#include "meshmdl.h"
#include "vp.h"
#include <iostream>
#include <iomanip>

static w3d::TM convert(const Matrix3D& matrix) {
    w3d::TM out;
    for(int r=0;r<3;++r) for(int c=0;c<4;++c) out.m[r][c]=matrix[r][c];
    return out;
}
static float distance(const Vector3& a,gore::V3 b) {
    return gore::len(gore::V3{a.X-b.x,a.Y-b.y,a.Z-b.z});
}
class ParitySkin : public MeshModelClass {
public:
    explicit ParitySkin(const w3d::Surface& surface) {
        Reset(int(surface.idx.size()/3),int(surface.local.size()),1);
        uint16* links=get_bone_links(true);
        for(size_t i=0;i<surface.local.size();++i){auto p=surface.local[i],n=surface.normal[i];Vertex->Get_Array()[i]=Vector3(p.x,p.y,p.z);VertexNorm->Get_Array()[i]=Vector3(n.x,n.y,n.z);links[i]=surface.bone[i];}
        // Supply normals directly, as loaded by MeshGeometryClass's W3D reader.
        Set_Flag(DIRTY_VNORMALS,false);
    }
    using MeshModelClass::get_deformed_vertices;
};
int main(int argc,char** argv) {
    try {
        w3d::require(argc==4,"usage: voxel_engine_parity body.w3d hierarchy.w3d output.txt");
        w3d::Bytes hb(argv[2]);auto sk=w3d::skeleton(hb);auto model=w3d::mesh(w3d::Bytes(argv[1]),sk);
        RAMFileClass file(hb.data.data(),int(hb.data.size()));w3d::require(file.Open(FileClass::READ)!=0,"RAM input open");
        ChunkLoadClass chunks(&file);w3d::require(chunks.Open_Chunk() && chunks.Cur_Chunk_ID()==0x100,"hierarchy top chunk");
        HTreeClass hierarchy;w3d::require(hierarchy.Load_W3D(chunks)==HTreeClass::OK,"engine HTree load");chunks.Close_Chunk();
        w3d::require(hierarchy.Num_Pivots()==sk.rest.size(),"engine bone count");
        std::ofstream log(argv[3]);w3d::require(bool(log),"output open");log<<std::setprecision(9);
        float worstMatrix=0,worstVertex=0,worstNormal=0;size_t count=0;
        for(int frame=0;frame<3;++frame) {
            Matrix3D root(true);
            if(frame) {
                root.Rotate_Z(.31f*frame);root.Set_Translation(Vector3(3.4f,-2.1f,.7f));
                int bone=hierarchy.Get_Bone_Index(frame==1?"C L FOREARM":"C SPINE1");
                w3d::require(bone>0,"capture fixture bone");hierarchy.Capture_Bone(bone);
                Matrix3D control(true);control.Rotate_X(.37f*frame);control.Rotate_Y(-.21f*frame);
                hierarchy.Control_Bone(bone,control);
            }
            hierarchy.Base_Update(root);
            log<<"pose "<<frame<<" bones "<<hierarchy.Num_Pivots()<<'\n';
            for(int b=0;b<hierarchy.Num_Pivots();++b) {
                const Matrix3D& matrix=hierarchy.Get_Transform(b);log<<"bone "<<b;
                for(int r=0;r<3;++r)for(int c=0;c<4;++c){log<<' '<<matrix[r][c];if(!frame)worstMatrix=(std::max)(worstMatrix,std::abs(matrix[r][c]-sk.rest[b].m[r][c]));}
                log<<'\n';
            }
            for(const auto& surf:model.surfaces) {
                std::vector<Vector3> local(surf.local.size()),normal(surf.normal.size()),actual(local.size()),actualNormal(local.size());
                for(size_t i=0;i<local.size();++i){auto p=surf.local[i],n=surf.normal[i];local[i]=Vector3(p.x,p.y,p.z);normal[i]=Vector3(n.x,n.y,n.z);}
                ParitySkin skin(surf);
                skin.get_deformed_vertices(actual.data(),actualNormal.data(),&hierarchy);
                for(size_t i=0;i<local.size();++i) {
                    auto matrix=convert(hierarchy.Get_Transform(surf.bone[i]));
                    auto p=frame?matrix.point(surf.local[i]):surf.rest[i];auto n=frame?matrix.dir(surf.normal[i]):surf.restNormal[i];
                    worstVertex=(std::max)(worstVertex,distance(actual[i],p));worstNormal=(std::max)(worstNormal,distance(actualNormal[i],n));++count;
                    log<<"v "<<surf.name<<' '<<i<<' '<<actual[i].X<<' '<<actual[i].Y<<' '<<actual[i].Z<<' '<<actualNormal[i].X<<' '<<actualNormal[i].Y<<' '<<actualNormal[i].Z<<'\n';
                }
            }
        }
        log<<"max_matrix_error "<<worstMatrix<<" max_position_error_m "<<worstVertex<<" max_normal_error "<<worstNormal<<" vertices_checked "<<count<<'\n';
        w3d::require(worstMatrix<1e-5f && worstVertex<1e-5f && worstNormal<1e-5f,"engine HTree/VP parity tolerance exceeded");
        std::cout<<"ENGINE HTREE/VP PARITY PASSED: "<<count<<" vertices, matrix "<<worstMatrix<<", position "<<worstVertex<<", normal "<<worstNormal<<"\n";
        std::cout<<"Scope: actual engine loader/base/captured poses and CPU vector processor. Not a live renderer, animation clip, material or DX8 validation.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}
}
