// Included once by voxel_skin.cpp, after the private draw helpers.
namespace {
gore::V3 physicsPoint(const Vector3 &p){return {p.X,p.Z,p.Y};}
Vector3 enginePoint(gore::V3 p){return Vector3(p.x,p.z,p.y);}
gore::V3 rotatePiece(const gore::RigidPiece &p,gore::V3 v){gore::V3 u{p.q[1],p.q[2],p.q[3]};return v+gore::cross(u,gore::cross(u,v)+v*p.q[0])*2;}
class VoxelPiece : public RenderObjClass {
public:
    MeshClass *skin=NULL;std::shared_ptr<Template> source;BRVG_Body body=0;
    std::vector<float> pose;std::vector<Vector3> frozen,frozenNormals,posed,posedNormals;
    std::vector<BloodSoak> soaks;
    std::vector<HeatPatch> burns;
    std::vector<float> fleshPositions,fleshNormals;Vector3 origin;gore::RigidPiece rigid;
    float age=0,awakeAge=0,radius=1.2f;uint64_t revision=1,drawRevision=0;const BRVG_Snapshot *lastSnapshot=NULL;bool failed=false;
    VoxelPiece(Instance &owner,const BRVG_Piece &part,const std::vector<float> &palette,const Vector3 &direction):source(owner.source),body(part.body),pose(palette),soaks(owner.soaks),burns(owner.burns) {
        float center[3];need(BRVG_PosePoint(body,pose.data(),source->bones,part.bone,part.rest_center,center)!=0,"piece origin transform");origin=Vector3(center[0],center[1],center[2]);
        skin=static_cast<MeshClass*>(owner.mesh->Clone());int count=owner.mesh->Peek_Model()->Get_Vertex_Count();frozen.resize(count);frozenNormals.resize(count);owner.mesh->Get_Deformed_Vertices(frozen.data(),frozenNormals.data());
        rigid.pos=physicsPoint(origin);rigid.half={.10f,.12f,.10f};rigid.mass=.8f;Vector3 impulse=direction;if(impulse.Length2()>1e-10f)impulse.Normalize();rigid.vel=physicsPoint(impulse*1.4f+Vector3(0,0,1.5f));rigid.angVel={1.7f,.8f,-1.3f};Set_Position(origin);Set_Collision_Type(0);
    }
    ~VoxelPiece(){if(lastSnapshot)BRVG_ReleaseDraw(lastSnapshot);if(body)BRVG_FreeBody(body);if(skin)skin->Release_Ref();}
    RenderObjClass *Clone() const override{return NULL;}
    const char *Get_Name() const override{return "VOXEL_COSMETIC_PIECE";}
    void Get_Obj_Space_Bounding_Sphere(SphereClass &s) const override{s=SphereClass(Vector3(0,0,0),radius);}
    void Get_Obj_Space_Bounding_Box(AABoxClass &b) const override{b.Center=Vector3(0,0,0);b.Extent=Vector3(radius,radius,radius);}
    Vector3 position(const Vector3 &p) const{return enginePoint(rotatePiece(rigid,physicsPoint(p-origin))+rigid.pos);}
    Vector3 normal(const Vector3 &n) const{return enginePoint(rotatePiece(rigid,physicsPoint(n)));}
    void step(float dt){age+=dt;if(rigid.asleep)return;float left=std::min(.1f,dt);
        while(left>0){float sub=std::min(left,1.f/60);left-=sub;Vector3 old=enginePoint(rigid.pos),hit,n;float floor=-100000;
            if(terrainRay && terrainRay(old+Vector3(0,0,.15f),old-Vector3(0,0,4),&hit,&n) && n.Z>.25f)floor=hit.Z;
            rigid.step(sub,floor);Vector3 next=enginePoint(rigid.pos);
            if(terrainRay && (next-old).Length2()>1e-8f && terrainRay(old,next,&hit,&n)){rigid.pos=physicsPoint(hit+n*.04f);Vector3 v=enginePoint(rigid.vel);float into=Vector3::Dot_Product(v,n);if(into<0)v-=n*(into*1.25f);rigid.vel=physicsPoint(v);}
        }
        awakeAge+=dt;if(awakeAge>=10){rigid.asleep=true;rigid.vel={0,0,0};rigid.angVel={0,0,0};}++revision;
        Matrix3D tm(true);tm.Set_Translation(enginePoint(rigid.pos));if(movePiece)movePiece(this,tm);else Set_Transform(tm);
    }
    void Render(RenderInfoClass &info) override {
        try{BRVG_BeginFrame(logicFrame);float light[]={255,255,255,0,0,0,0,0,1};SnapshotOwner snapshot(BRVG_AcquireDraw(body,pose.data(),source->bones,light,1));if(!snapshot.p)return;const BRVG_DrawView *draw=BRVG_GetDraw(snapshot.p);need(draw&&draw->surface_count==1,"piece draw data");
            if(drawRevision!=revision || lastSnapshot!=snapshot.p){posed.resize(frozen.size());posedNormals.resize(frozenNormals.size());for(size_t i=0;i<frozen.size();++i){posed[i]=position(frozen[i]);posedNormals[i]=normal(frozenNormals[i]);}
                fleshPositions.resize(draw->flesh_vertex_count*3);fleshNormals.resize(draw->flesh_vertex_count*3);for(unsigned i=0;i<draw->flesh_vertex_count;++i){Vector3 p=position(Vector3(draw->flesh_xyz[i*3],draw->flesh_xyz[i*3+1],draw->flesh_xyz[i*3+2]));Vector3 n=normal(Vector3(draw->flesh_normals[i*3],draw->flesh_normals[i*3+1],draw->flesh_normals[i*3+2]));for(int k=0;k<3;++k){fleshPositions[i*3+k]=p[k];fleshNormals[i*3+k]=n[k];}}
                if(lastSnapshot)BRVG_ReleaseDraw(lastSnapshot);lastSnapshot=BRVG_AcquireDraw(body,pose.data(),source->bones,light,1);drawRevision=revision;
            }
            std::vector<std::unique_ptr<Pass>> passes;for(int p=0;p<skin->Peek_Model()->Get_Pass_Count();++p)passes.push_back(preparePass(skin,draw->surfaces[0],posed,posedNormals,p,source->identity));
            SavedState saved;DX8Wrapper::Set_World_Identity();bool base=(info.Current_Override_Flags()&RenderInfoClass::RINFO_OVERRIDE_ADDITIONAL_PASSES_ONLY)==0;if(base)for(auto &p:passes)drawPacket(*p);
            if(!passes.empty()){const BRVG_PacketView *geometry=BRVG_GetPacket(passes[0]->packet);for(int i=0;i<info.Additional_Pass_Count();++i){MaterialPassClass *pass=info.Peek_Additional_Pass(i);pass->Install_Materials();ShaderClass shader=pass->Peek_Shader();shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);DX8Wrapper::Set_Shader(shader);uploadAndDraw(geometry->vertices,geometry->vertex_count,geometry->indices,geometry->index_count);}}
            BRVG_DrawView transformed=*draw;transformed.flesh_xyz=fleshPositions.data();transformed.flesh_normals=fleshNormals.data();flesh(transformed,info,base);
            if (base && !passes.empty() && !(info.Current_Override_Flags() & RenderInfoClass::RINFO_OVERRIDE_SHADOW_RENDERING)) {
                std::array<SoakBatch, 2> batches;
                buildSoaks(*passes[0], draw->surfaces[0], *skin->Peek_Model(), source->soakSurface, soaks, batches);
                drawSoaks(batches);
                for (int stage=1;stage<=3 && !burns.empty();++stage) {
                    batches = {};
                    buildSoaks(*passes[0],draw->surfaces[0],*skin->Peek_Model(),source->soakSurface,soaks,batches,&burns,stage);
                    drawSoaks(batches,stage);
                }
            }
        }catch(const std::exception &e){if(!failed){log("piece fallback: %s",e.what());failed=true;}}
    }
};
std::vector<VoxelPiece*> pieces;
void discardPiece(size_t i){VoxelPiece *piece=pieces[i];if(piece->Peek_Scene())piece->Remove();piece->Release_Ref();pieces.erase(pieces.begin()+i);}
void clearPieces(){while(!pieces.empty())discardPiece(pieces.size()-1);}
void tickPieces(float dt){for(size_t i=0;i<pieces.size();){pieces[i]->step(dt);if(pieces[i]->age>30)discardPiece(i);else ++i;}}
void spawnPieces(Instance &state,const std::vector<float> &pose,const Vector3 &direction){
    if(!addPiece)return;
    BRVG_Piece separated[2]{};unsigned n=BRVG_Separate(state.body,separated,2);
    for(unsigned i=0;i<n;++i){try{VoxelPiece *piece=new VoxelPiece(state,separated[i],pose,direction);if(pieces.size()>=12)discardPiece(0);addPiece(piece);pieces.push_back(piece);log("severed original-skin piece: bone %d; non-colliding, expires in 30s",separated[i].bone);}catch(const std::exception &e){BRVG_FreeBody(separated[i].body);log("piece creation failed: %s",e.what());}}
}
}
