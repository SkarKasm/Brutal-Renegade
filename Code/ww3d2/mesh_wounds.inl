// Runtime wound geometry. Included by mesh.cpp; no asset-manager models are mutated.
#include <vector>
#include <algorithm>
#include "surfaceclass.h"

namespace {
// Squared distance to a triangle (Ericson's Voronoi-region test).
float Wound_Triangle_Distance(const Vector3 &p, const Vector3 &a, const Vector3 &b, const Vector3 &c)
{
    Vector3 ab=b-a, ac=c-a, ap=p-a;
    float d1=Vector3::Dot_Product(ab,ap), d2=Vector3::Dot_Product(ac,ap);
    if(d1<=0 && d2<=0) return ap.Length2();
    Vector3 bp=p-b;
    float d3=Vector3::Dot_Product(ab,bp), d4=Vector3::Dot_Product(ac,bp);
    if(d3>=0 && d4<=d3) return bp.Length2();
    float vc=d1*d4-d3*d2;
    if(vc<=0 && d1>=0 && d3<=0) return (p-(a+ab*(d1/(d1-d3)))).Length2();
    Vector3 cp=p-c;
    float d5=Vector3::Dot_Product(ab,cp), d6=Vector3::Dot_Product(ac,cp);
    if(d6>=0 && d5<=d6) return cp.Length2();
    float vb=d5*d2-d1*d6;
    if(vb<=0 && d2>=0 && d6<=0) return (p-(a+ac*(d2/(d2-d6)))).Length2();
    float va=d3*d6-d5*d4;
    if(va<=0 && d4-d3>=0 && d5-d6>=0) return (p-(b+(c-b)*((d4-d3)/((d4-d3)+(d5-d6))))).Length2();
    float sum=va+vb+vc;
    if(fabs(sum)<1e-12f) return (p-a).Length2();
    return (p-(a+ab*(vb/sum)+ac*(vc/sum))).Length2();
}
// Shared irregular silhouette for the cutout and its internal lining. Mixed
// frequencies, asymmetry and a wound-specific phase avoid a regular gear/star.
float Wound_Rim_Scale(float angle,const Vector3 &point)
{
    float phase=point.X*13.71f+point.Y*9.31f+point.Z*17.17f;
    return WWMath::Clamp(1.0f+.15f*sinf(angle*3.0f+phase)+
        .11f*sinf(angle*7.0f-phase*.73f)+.065f*sinf(angle*11.0f+phase*.41f)+
        .10f*cosf(angle*2.0f+phase*.31f),.65f,1.35f);
}
struct WoundTriangle { int source; Vector3 bary[3]; bool tissue; };
}

bool MeshClass::Find_Wound_Surface(const Vector3 &start, const Vector3 &direction,
    float &distance, Vector3 &point, Vector3 &normal, bool intact_bones_only)
{
    if(!Model || Model->Get_Vertex_Count()<=0) return false;
    bool skin=Model->Get_Flag(MeshModelClass::SKIN)!=0;
    if(skin && (!Container || !Container->Get_HTree())) return false;
    int nv=Model->Get_Vertex_Count();
    std::vector<Vector3> world(nv);
    if(skin) Get_Deformed_Vertices(&world[0]);
    else for(int i=0;i<nv;++i) Matrix3D::Transform_Vector(Get_Transform(),Model->Get_Vertex_Array()[i],&world[i]);
    const TriIndex *polys=Model->Get_Polygon_Array();
    const HTreeClass *tree=skin ? Container->Get_HTree() : NULL;
    const uint16 *links=skin ? Model->Get_Vertex_Bone_Links() : NULL;
    bool found=false;
    for(int p=0;p<Model->Get_Polygon_Count();++p) {
        const TriIndex &tri=polys[p];
        TextureClass *surface=Model->Peek_Texture(p,0,0);
        if(surface && !stricmp(surface->Get_Texture_Name(),"g_core_cutout"))continue;
        if(intact_bones_only && skin && (tree->Is_Skin_Hidden(links[tri.I]) ||
            tree->Is_Skin_Hidden(links[tri.J]) || tree->Is_Skin_Hidden(links[tri.K]))) continue;
        Vector3 a=world[tri.I], ab=world[tri.J]-a, ac=world[tri.K]-a, h;
        Vector3::Cross_Product(direction,ac,&h);
        float det=Vector3::Dot_Product(ab,h);
        if(det<=0.0000001f) continue; // nearest outward-facing visible surface only
        Vector3 delta=start-a;
        float u=Vector3::Dot_Product(delta,h)/det;
        if(u<0 || u>1) continue;
        Vector3 q; Vector3::Cross_Product(delta,ab,&q);
        float v=Vector3::Dot_Product(direction,q)/det;
        if(v<0 || u+v>1) continue;
        float t=Vector3::Dot_Product(ac,q)/det;
        if(t<0 || t>=distance) continue;
        distance=t;point=start+direction*t;
        Vector3::Cross_Product(ab,ac,&normal);normal.Normalize();found=true;
    }
    return found;
}

int MeshClass::Carve_Wound(const Vector3 &point, const Vector3 &normal, float radius, float depth, TextureClass *tissue, bool intact_bones_only, bool open_core)
{
    if(!Model || !tissue || radius<=0 || depth<=0) return 0;
    MeshModelClass *old=Model;
    const int nv=old->Get_Vertex_Count(), np=old->Get_Polygon_Count();
    const int passes=old->Get_Pass_Count();
    // Legacy index-buffer counts are 16 bit as well as the index values.
    // Every material pass reserves its own complete triangle-index range.
    // In particular the mutant skins have two passes: a per-mesh polygon
    // budget alone otherwise permits the allocation count to wrap at 65536.
    if(passes<=0 || passes>MeshMatDescClass::MAX_PASSES) return 0;
    const int max_wound_polygons=std::min(18000,65520/(3*passes));
    // DX8 uses 16 bit vertex indices. A bounded mesh also bounds repeated corpse fire.
    if(nv<=0 || nv>26000 || np>16000 || np>max_wound_polygons) return 0;
    bool skin=old->Get_Flag(MeshModelClass::SKIN)!=0;
    const HTreeClass *tree=skin && Container ? Container->Get_HTree() : NULL;
    if(skin && !tree) return 0;
    const uint16 *links=skin ? old->Get_Vertex_Bone_Links() : NULL;
    std::vector<Vector3> world(nv);
    if(skin) Get_Deformed_Vertices(&world[0]);
    else for(int v=0;v<nv;++v) Matrix3D::Transform_Vector(Get_Transform(),old->Get_Vertex_Array()[v],&world[v]);
    const TriIndex *polys=old->Get_Polygon_Array();
    Vector3 n=normal; if(n.Length2()<0.0001f) return 0; n.Normalize();
    Vector3 axis= fabs(n.Z)<0.9f ? Vector3(0,0,1) : Vector3(0,1,0), u,vaxis;
    Vector3::Cross_Product(axis,n,&u); u.Normalize(); Vector3::Cross_Product(n,u,&vaxis);
    std::vector<int> divisions(np,0);
    int affected=0;
    for(int p=0;p<np;++p) {
        const TriIndex &t=polys[p];
        // Severed-bone skin transforms are singular collapse matrices. Blast
        // wounds must never invert them or rebind their boundary triangles.
        // Ordinary projectile wounds retain the existing default path.
        if(intact_bones_only && skin && (tree->Is_Skin_Hidden(links[t.I]) ||
            tree->Is_Skin_Hidden(links[t.J]) || tree->Is_Skin_Hidden(links[t.K]))) continue;
        // Special-weapon craters leave prior exposed tissue surfaces in place.
        // Ballistic craters still use the original accumulation behavior.
        TextureClass *existing_tissue=intact_bones_only ? old->Peek_Texture(p,0,0) : NULL;
        if(existing_tissue && (!stricmp(existing_tissue->Get_Texture_Name(),"g_tissue.tga") ||
            !stricmp(existing_tissue->Get_Texture_Name(),"g_core_cutout"))) continue;
        const Vector3 &a=world[t.I], &b=world[t.J], &c=world[t.K];
        Vector3 fn; Vector3::Cross_Product(b-a,c-a,&fn);
        if(fn.Length2()<1e-12f) continue;
        fn.Normalize();
        if(Vector3::Dot_Product(fn,n)<0.08f) continue;
        if(Wound_Triangle_Distance(point,a,b,c)>radius*radius) continue;
        float edge=std::max((a-b).Length(),std::max((a-c).Length(),(b-c).Length()));
        divisions[p]=std::min(open_core?(radius>=.04f?6:16):12,std::max(1,(int)ceil(edge/(radius*0.23f))));
        ++affected;
    }
    if(!affected) return 0;
    std::vector<WoundTriangle> extra;
    for(int p=0;p<np;++p) if(divisions[p]>0) {
        int d=divisions[p];
        for(int i=0;i<d;++i) for(int j=0;j<d-i;++j) {
            Vector3 b0(1.0f-(float)(i+j)/d,(float)i/d,(float)j/d);
            Vector3 b1=b0+Vector3(-1.0f/d,1.0f/d,0), b2=b0+Vector3(-1.0f/d,0,1.0f/d);
            WoundTriangle q; q.source=p; q.bary[0]=b0;q.bary[1]=b1;q.bary[2]=b2;extra.push_back(q);
            if(j<d-i-1) { q.bary[0]=b1;q.bary[1]=b1+Vector3(-1.0f/d,0,1.0f/d);q.bary[2]=b2;extra.push_back(q); }
        }
    }
    // Keep original vertices and indices intact for existing skinned decals.
    int newnv=nv+(int)extra.size()*3, newnp=np+(int)extra.size();
    // Keep a complete deformed mesh comfortably within the legacy renderer's
    // batching limits. Reaching the budget preserves the existing wound surface.
    if(newnv>30000 || newnp>max_wound_polygons) return 0;
    MeshModelClass *next=NEW_REF(MeshModelClass,());
    next->Reset(newnp,newnv,passes);
    next->WoundRestVertices=NEW_REF(ShareBufferClass<Vector3>,(newnv));
    Vector3 *rest=next->WoundRestVertices->Get_Array();
    const Vector3 *oldrest=old->WoundRestVertices ? old->WoundRestVertices->Get_Array() : old->Get_Vertex_Array();
    memcpy(rest,oldrest,nv*sizeof(Vector3));
    next->Set_Name(old->Get_Name());
    next->HasBeenInUse=false;
    next->Flags=(old->Flags & ~(MeshModelClass::DIRTY_VNORMALS | MeshModelClass::ALLOW_NPATCHES))
        | MeshModelClass::DIRTY_BOUNDS | MeshModelClass::DIRTY_PLANES;
    next->W3dAttributes=old->W3dAttributes;
    REF_PTR_SET(next->MatInfo,old->MatInfo);
    next->Set_Flag(MeshModelClass::SKIN,skin);
    next->Set_Flag(MeshModelClass::TWO_SIDED,old->Get_Flag(MeshModelClass::TWO_SIDED)!=0);
    next->Set_Flag(MeshModelClass::CAST_SHADOW,old->Get_Flag(MeshModelClass::CAST_SHADOW)!=0);
    next->Set_Flag(MeshModelClass::DIRTY_BOUNDS,true);
    next->Set_Flag(MeshModelClass::DIRTY_PLANES,true);
    Vector3 *verts=next->Get_Vertex_Array();
    Vector3 *norms=next->get_vert_normals();
    uint16 *newlinks=skin ? next->get_bone_links() : NULL;
    TriIndex *out=next->get_polys();
    memcpy(verts,old->Get_Vertex_Array(),nv*sizeof(Vector3));
    memcpy(norms,old->Get_Vertex_Normal_Array(),nv*sizeof(Vector3));
    if(skin) memcpy(newlinks,links,nv*sizeof(uint16));
    memcpy(out,polys,np*sizeof(TriIndex));
    for(int p=0;p<np;++p) if(divisions[p]>0) out[p].J=out[p].K=out[p].I;
    // Preserve every original material pass and UV stage, including character skins.
    int nextuv=0;
    for(int pass=0;pass<passes;++pass) {
        next->CurMatDesc->Set_DCG_Source(pass,old->CurMatDesc->Get_DCG_Source(pass));
        next->CurMatDesc->Set_DIG_Source(pass,old->CurMatDesc->Get_DIG_Source(pass));
        for(int stage=0;stage<2;++stage) {
            const Vector2 *src=old->Get_UV_Array(pass,stage);
            if(src) {
                int index=nextuv++;
                next->CurMatDesc->Set_UV_Source(pass,stage,index);
                Vector2 *dst=next->CurMatDesc->Get_UV_Array_By_Index(index);
                memcpy(dst,src,nv*sizeof(Vector2));
            }
        }
        for(int p=0;p<np;++p) {
            next->Set_Shader(p,old->Get_Shader(p,pass),pass);
            for(int stage=0;stage<2;++stage) next->Set_Texture(p,old->Peek_Texture(p,pass,stage),pass,stage);
        }
        for(int vi=0;vi<nv;++vi) next->Set_Material(vi,old->Peek_Material(vi,pass),pass);
    }
    for(int array=0;array<MeshMatDescClass::MAX_COLOR_ARRAYS;++array) {
        unsigned *src=old->CurMatDesc->Get_Color_Array(array,false);
        if(src) memcpy(next->CurMatDesc->Get_Color_Array(array),src,nv*sizeof(unsigned));
    }
    VertexMaterialClass *flesh=NEW_REF(VertexMaterialClass,());
    flesh->Set_Lighting(true);
    flesh->Set_Diffuse(Vector3(1.0f,1.0f,1.0f));
    flesh->Set_Emissive(Vector3(0.08f,0.04f,0.04f));
    flesh->Set_Ambient(Vector3(0.8f,0.8f,0.8f));
    TextureClass *cutout=NULL;
    if(open_core) {
        cutout=NEW_REF(TextureClass,(4,4,WW3D_FORMAT_A8R8G8B8,TextureClass::MIP_LEVELS_1));
        SurfaceClass *surface=cutout->Get_Surface_Level();surface->Clear();surface->Release_Ref();
        cutout->Set_Texture_Name("g_core_cutout");
    }
    int open_triangles=0;
    for(unsigned e=0;e<extra.size();++e) {
        WoundTriangle &q=extra[e];
        const TriIndex &t=polys[q.source]; int ids[3]={t.I,t.J,t.K};
        Vector3 center(0,0,0);
        for(int k=0;k<3;++k) center+=world[ids[k]]*(q.bary[0][k]+q.bary[1][k]+q.bary[2][k])/3.0f;
        Vector3 delta=center-point;
        float x=Vector3::Dot_Product(delta,u), y=Vector3::Dot_Product(delta,vaxis);
        float radial=sqrt(x*x+y*y)/radius;
        float ragged=Wound_Rim_Scale(atan2f(y,x),point);
        q.tissue=radial<0.78f*ragged;
        bool hole=open_core && radial<.44f*ragged;
        // The fitted collar overlaps the ragged boundary. Requiring every
        // corner to be inside left crossing triangles as opaque tissue plugs
        // over small openings. Classify the locally subdivided core instead.
        if(hole)++open_triangles;
        int dstpoly=np+e, first=nv+e*3;
        out[dstpoly].I=first;out[dstpoly].J=first+1;out[dstpoly].K=first+2;
        for(int k=0;k<3;++k) {
            Vector3 bary=q.bary[k], wp=world[t.I]*bary.X+world[t.J]*bary.Y+world[t.K]*bary.Z;
            Vector3 diff=wp-point;
            float px=Vector3::Dot_Product(diff,u), py=Vector3::Dot_Product(diff,vaxis);
            float r=sqrt(px*px+py*py)/radius;
            float rag=Wound_Rim_Scale(atan2f(py,px),point);
            float f=std::max(0.0f,1.0f-r/rag);
            // Steep wound walls and a shallow, irregular tissue floor.
            Vector3 baseworld(0,0,0);
            for(int j=0;j<3;++j) {
                Vector3 original;
                const Matrix3D &base_tm=skin ? tree->Get_Skin_Transform(links[ids[j]]) : Get_Transform();
                Matrix3D::Transform_Vector(base_tm,oldrest[ids[j]],&original);
                baseworld+=original*bary[j];
            }
            float existing=Vector3::Dot_Product(baseworld-wp,n);
            float wanted=depth*std::min(1.0f,f*3.5f)*(0.93f+0.07f*sinf(px*153+py*127));
            float prior_distance=(baseworld-wp).Length();
            wp-=n*std::max(0.0f,wanted-existing);
            Vector3 displacement=wp-baseworld;
            float displacement_length=displacement.Length();
            float max_depth=std::max(depth,prior_distance);
            if(displacement_length>max_depth && displacement_length>0.00001f)
                wp=baseworld+displacement*(max_depth/displacement_length);
            int nearest=bary.X>=bary.Y && bary.X>=bary.Z ? 0 : bary.Y>=bary.Z ? 1 : 2;
            int vi=first+k;
            Matrix3D tm=skin ? tree->Get_Skin_Transform(links[ids[nearest]]) : Get_Transform();
            Matrix3D::Inverse_Transform_Vector(tm,wp,&verts[vi]);
            Matrix3D::Inverse_Transform_Vector(tm,baseworld,&rest[vi]);
            if(skin) newlinks[vi]=links[ids[nearest]];
            for(int array=0;array<MeshMatDescClass::MAX_COLOR_ARRAYS;++array) {
                unsigned *src=old->CurMatDesc->Get_Color_Array(array,false);
                if(!src) continue;
                unsigned color=0;
                for(int shift=0;shift<32;shift+=8) {
                    unsigned channel=(unsigned)(((src[t.I]>>shift)&255)*bary.X+((src[t.J]>>shift)&255)*bary.Y+((src[t.K]>>shift)&255)*bary.Z);
                    color|=std::min(255u,channel)<<shift;
                }
                next->CurMatDesc->Get_Color_Array(array)[vi]=q.tissue ? 0xffffffffu : color;
            }
            for(int pass=0;pass<passes;++pass) {
                next->Set_Material(vi,q.tissue ? flesh : old->Peek_Material(ids[nearest],pass),pass);
                for(int stage=0;stage<2;++stage) {
                    const Vector2 *src=old->Get_UV_Array(pass,stage);
                    if(!src) continue;
                    Vector2 *dst=next->CurMatDesc->Get_UV_Array(pass,stage);
                    dst[vi]=q.tissue ? Vector2(0.73f+px/(radius*5.2f),0.75f+py/(radius*5.2f)) : src[t.I]*bary.X+src[t.J]*bary.Y+src[t.K]*bary.Z;
                }
            }
        }
        Vector3 fn; Vector3::Cross_Product(verts[first+1]-verts[first],verts[first+2]-verts[first],&fn);
        if(fn.Length2()>1e-12f) fn.Normalize(); else fn=Vector3(0,0,1);
        for(int k=0;k<3;++k) norms[first+k]=fn;
        for(int pass=0;pass<passes;++pass) {
            ShaderClass shader=q.tissue?ShaderClass::_PresetOpaqueShader:old->Get_Shader(q.source,pass);
            if(hole)shader.Set_Alpha_Test(ShaderClass::ALPHATEST_ENABLE);
            next->Set_Shader(dstpoly,shader,pass);
            for(int stage=0;stage<2;++stage) next->Set_Texture(dstpoly,hole ? (stage==0?cutout:NULL) : q.tissue ? (stage==0 ? tissue : NULL) : old->Peek_Texture(q.source,pass,stage),pass,stage);
        }
    }
    if(cutout)cutout->Release_Ref();
    flesh->Release_Ref();
    // Tiny holes on a coarse LOD may have no triangle center in the core.
    // Preserve the original mesh so the caller can use a closed crater.
    if(open_core && !open_triangles){next->Release_Ref();return 0;}
    TheDX8MeshRenderer.Unregister_Mesh_Type(this);
    if(UserLighting) { delete[] UserLighting; UserLighting=NULL; }
    REF_PTR_SET(Model,next); next->Release_Ref();
    Invalidate_Cached_Bounding_Volumes();
    // Original vertices are retained and all new vertices move inward, so the
    // parent bounds remain conservative. Rebuilding HLOD bounds here would put
    // its shared skeleton into the bind pose and break the next LOD's carving.
    return affected;
}

// Chemical softening is per instance, keeps topology/materials/bone links intact,
// and advances in small time steps instead of instantly scaling the render object.
int MeshClass::Melt_Skin(const Vector3 &origin, float amount, float dissolved)
{
    if(!Model || amount<=0 || (Model->W3dAttributes & 0x00000ff0)!=0) return 0;
    bool skin=Model->Get_Flag(MeshModelClass::SKIN)!=0;
    const HTreeClass *tree=skin && Container ? Container->Get_HTree() : NULL;
    if(skin && !tree) return 0;
    Make_Unique();
    if(dissolved>0.08f) {
        TextureClass *goo=WW3DAssetManager::Get_Instance()->Get_Texture("g_chem_melt.tga");
        if(goo) {
            for(int pass=0;pass<Model->Get_Pass_Count();++pass) {
                if(Model->Peek_Texture(0,pass,0)!=goo) {
                    Model->CurMatDesc->Make_Texture_Array_Unique(pass,0);
                    if(Model->Has_Texture_Array(pass,0)) for(int t=0;t<Model->Get_Polygon_Count();++t) Model->Set_Texture(t,goo,pass,0);
                    Model->Set_Single_Texture(goo,pass,0);
                }
            }
            goo->Release_Ref();
        }
    }
    int count=Model->Get_Vertex_Count();
    if(count<=0) return 0;
    Model->Get_Vertex_Normal_Array(); // Make the source normals available before detaching.
    ShareBufferClass<Vector3> *positions=NEW_REF(ShareBufferClass<Vector3>,(count));
    ShareBufferClass<Vector3> *normals=NEW_REF(ShareBufferClass<Vector3>,(count));
    memcpy(positions->Get_Array(),Model->Get_Vertex_Array(),count*sizeof(Vector3));
    memcpy(normals->Get_Array(),Model->Get_Vertex_Normal_Array(),count*sizeof(Vector3));
    REF_PTR_SET(Model->Vertex,positions);positions->Release_Ref();
    REF_PTR_SET(Model->VertexNorm,normals);normals->Release_Ref();
    if(Model->WoundRestVertices && Model->WoundRestVertices->Num_Refs()>1) {
        ShareBufferClass<Vector3> *rest=NEW_REF(ShareBufferClass<Vector3>,(count));
        memcpy(rest->Get_Array(),Model->WoundRestVertices->Get_Array(),count*sizeof(Vector3));
        REF_PTR_SET(Model->WoundRestVertices,rest);rest->Release_Ref();
    }
    Vector3 *verts=Model->Get_Vertex_Array(),*vn=Model->get_vert_normals();
    Vector3 *rest=Model->WoundRestVertices ? Model->WoundRestVertices->Get_Array() : NULL;
    const uint16 *links=skin ? Model->Get_Vertex_Bone_Links() : NULL;
    float vertical=expf(-2.3f*amount);
    for(int i=0;i<count;++i) {
        if(skin && tree->Is_Skin_Hidden(links[i])) continue;
        const Matrix3D &tm=skin ? tree->Get_Skin_Transform(links[i]) : Get_Transform();
        for(int stream=0;stream<(rest?2:1);++stream) {
            Vector3 *target=stream==0 ? &verts[i] : &rest[i],world;
            Matrix3D::Transform_Vector(tm,*target,&world);
            Vector3 delta=world-origin;
            float height=WWMath::Max(0.0f,delta.Z);
            float spread=1.0f+amount*0.60f*WWMath::Clamp(1.0f-height/1.6f,0.0f,1.0f);
            delta.X*=spread;delta.Y*=spread;
            float radial=sqrtf(delta.X*delta.X+delta.Y*delta.Y);
            if(radial>0.48f) {delta.X*=0.48f/radial;delta.Y*=0.48f/radial;}
            // Retain a low mound of material instead of flattening into a sheet.
            delta.Z=height*vertical+amount*0.30f*WWMath::Clamp(1.0f-radial/0.48f,0.0f,1.0f);
            // Cohesive, low-amplitude sag; matching seam positions get identical motion.
            delta.Z-=amount*0.025f*(0.5f+0.5f*sinf(world.X*13.0f+world.Y*17.0f+world.Z*11.0f));
            if(delta.Z<0.015f) delta.Z=0.015f;
            Matrix3D::Inverse_Transform_Vector(tm,origin+delta,target);
        }
        Vector3 normal=tm.Rotate_Vector(vn[i]);normal.Z/=vertical;
        vn[i]=tm.Inverse_Rotate_Vector(normal);if(vn[i].Length2()>0.0001f) vn[i].Normalize();
    }
    TheDX8MeshRenderer.Unregister_Mesh_Type(this);
    if(UserLighting) {delete[] UserLighting;UserLighting=NULL;}
    Model->Set_Flag(MeshModelClass::DIRTY_PLANES,true);
    if(!skin) Model->Set_Flag(MeshModelClass::DIRTY_BOUNDS,true);
    REF_PTR_RELEASE(Model->CullTree);
    Invalidate_Cached_Bounding_Volumes();
    return count;
}

#include "mesh_tunnel.inl"

#include "mesh_thermal.inl"
