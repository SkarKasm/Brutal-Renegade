// Native thermal particles. Included by goremanager.cpp after surface helpers.
// No external smoke assets, blood-slider dependency, or shared skin mutations.
// HLOD replaces attached object transforms with the bone transform each frame.
// Carry the local surface offset in the source randomizer instead.
class ThermalSurfaceRandomizer : public Vector3Randomizer {
public:
    ThermalSurfaceRandomizer(const Vector3 &offset):Offset(offset),Radius(.025f) {}
    virtual unsigned int Class_ID(void) const {return CLASSID_UNKNOWN;}
    virtual void Get_Vector(Vector3 &v) {v=Offset+Random_Unit_Vector()*Radius;}
    virtual float Get_Maximum_Extent(void) {return Offset.Length()+Radius;}
    virtual void Scale(float scale) {Offset*=scale;Radius*=fabsf(scale);}
    virtual Vector3Randomizer *Clone(void) const {return new ThermalSurfaceRandomizer(*this);}
private:
    Vector3 Offset;float Radius;
};

static TextureClass *Create_Thermal_Sprite(bool ember)
{
    TextureClass *texture=NEW_REF(TextureClass,(32,32,WW3D_FORMAT_A8R8G8B8,TextureClass::MIP_LEVELS_1));
    SurfaceClass *surface=texture->Get_Surface_Level();
    if(!surface) {texture->Release_Ref();return NULL;}
    int pitch=0;unsigned char *pixels=(unsigned char *)surface->Lock(&pitch);
    if(!pixels) {surface->Release_Ref();texture->Release_Ref();return NULL;}
    for(int y=0;y<32;++y) {
        unsigned int *row=(unsigned int *)(pixels+y*pitch);
        for(int x=0;x<32;++x) {
            float dx=(x-15.5f)/15.5f,dy=(y-15.5f)/15.5f;
            float radial=WWMath::Clamp(1.0f-dx*dx-dy*dy,0.0f,1.0f);
            // Smooth feathered alpha; uneven wisps for smoke, compact hot embers.
            float grain=ember?1.0f:0.72f+0.28f*sinf(x*1.73f+y*.87f)*sinf(y*1.29f-x*.41f);
            unsigned int alpha=(unsigned int)(255.0f*radial*radial*grain);
            row[x]=(alpha<<24)|0x00ffffff;
        }
    }
    surface->Unlock();surface->Release_Ref();
    texture->Set_Texture_Name(ember?"g_runtime_ember":"g_runtime_smoke");
    return texture;
}

static ParticleEmitterClass *Create_Thermal_Emitter(bool ember)
{
    TextureClass *texture=Create_Thermal_Sprite(ember);
    if(!texture)return NULL;
    float life=ember?1.1f:2.6f;
    float end_time=life;
    Vector3 end_color=ember?Vector3(.18f,.025f,.002f):Vector3(.28f,.25f,.22f);
    ParticlePropertyStruct<Vector3> color;
    color.Start=ember?Vector3(1.0f,.38f,.055f):Vector3(.34f,.30f,.26f);
    color.Rand.Set(.06f,.025f,.015f);
    color.NumKeyFrames=1;color.KeyTimes=&end_time;color.Values=&end_color;
    float end_opacity=0.0f;
    ParticlePropertyStruct<float> opacity;
    opacity.Start=ember?.85f:.24f;opacity.Rand=.04f;
    opacity.NumKeyFrames=1;opacity.KeyTimes=&end_time;opacity.Values=&end_opacity;
    float end_size=ember?.004f:.23f;
    ParticlePropertyStruct<float> size;
    size.Start=ember?.009f:.055f;size.Rand=ember?.003f:.018f;
    size.NumKeyFrames=1;size.KeyTimes=&end_time;size.Values=&end_size;
    ParticlePropertyStruct<float> zero;
    zero.Start=zero.Rand=0;zero.NumKeyFrames=0;zero.KeyTimes=NULL;zero.Values=NULL;
    // World acceleration keeps smoke rising even when its parent bone lies flat.
    ParticleEmitterClass *emitter=new ParticleEmitterClass(ember?2.0f:5.0f,1,
        new Vector3SolidSphereRandomizer(.025f),Vector3(0,0,0),
        new Vector3SolidSphereRandomizer(ember?.18f:.07f),0,0,
        color,opacity,size,zero,3.14f,zero,zero,Vector3(0,0,ember?.3f:.18f),
        life,texture,ShaderClass::_PresetAlphaSpriteShader,0,ember?8:20);
    texture->Release_Ref();
    emitter->Set_Name(ember?"G_BURN_EMBERS":"G_BURN_SMOKE");
    return emitter;
}

static bool Attach_Thermal_Remains(RenderObjClass *model,const char *bone_name,
    const Vector3 &center,Vector3 incoming,bool embers)
{
    // Reserve 24 timed slots for existing blood fountains and ground pools.
    if(!model || _TimedCount+2>MAX_GORE_TIMED-24)return false;
    int bone=Gore_Bone_Index(model,bone_name);
    if(bone<=0 || model->Is_Bone_Skin_Hidden(bone))return false;
    if(incoming.Length2()<.0001f)incoming=-model->Get_Transform().Get_X_Vector();
    incoming.Normalize();
    Vector3 point=center,normal=-incoming;float distance=1.25f;
    if(!Find_Model_Wound_Surface(model,center-incoming*.6f,incoming,distance,point,normal,true))return false;
    Matrix3D inverse;model->Get_Bone_Transform(bone).Get_Orthogonal_Inverse(inverse);
    Vector3 local=inverse*(point+normal*.015f);
    bool attached=false;
    for(int i=0;i<(embers?2:1);++i) {
        ParticleEmitterClass *emitter=Create_Thermal_Emitter(i==1);
        if(!emitter)continue;
        emitter->Set_Position_Randomizer(new ThermalSurfaceRandomizer(local));
        model->Add_Sub_Object_To_Bone(emitter,bone);
        if(emitter->Get_Container()==model) {
            emitter->Start();
            // Stop emissions first, retaining particles through their fade-out.
            Add_Timed(emitter,i==1?3.5f:8.0f,i==1?4.8f:10.8f,point,0);
            attached=true;
            Gore_Test_Log("thermal attached: %s bone %s (%d) position %.3f %.3f %.3f\n",emitter->Get_Name(),bone_name,bone,point.X,point.Y,point.Z);
        }
        emitter->Release_Ref();
    }
    return attached;
}

static void Update_Thermal_Remains(void)
{
    if(!GoreManager::Is_Enabled() || !_Settings.BurnEffects)return;
    for(unsigned i=0;i<ARRAY_COUNT(_WoundBudgets);++i) {
        WoundBudget &budget=_WoundBudgets[i];
        if(budget.BurnStage<2 || budget.DeathBurnEmitters)continue;
        PhysicalGameObj *object=GameObjManager::Find_PhysicalGameObj(budget.Id);
        SoldierGameObj *soldier=object?object->As_SoldierGameObj():NULL;
        if(!soldier || soldier->Peek_Model()!=budget.Model)continue;
        if(soldier->Get_Defense_Object()->Get_Health()>0.0f)continue;
        budget.DeathBurnEmitters=true;
        Gore_Test_Log("thermal death refresh: actor %d burn stage %d\n",budget.Id,budget.BurnStage);
        int attached=0;
        // Strongest regions first, at most four pairs on a burned corpse.
        for(int stage=3;stage>=2 && attached<4;--stage) {
            for(int region=0;region<ARRAY_COUNT(budget.BurnRegionStage) && attached<4;++region) {
                if(budget.BurnRegionStage[region]!=stage)continue;
                int box=region/2;
                if(box>=ARRAY_COUNT(_HitBoxes))continue;
                const GoreHitBoxStruct &hit=_HitBoxes[box];
                RenderObjClass *model=soldier->Peek_Model();
                Vector3 center=Bone_Position(model,hit.Bone);
                Find_Model_Hit_Box_Center(model,hit.HitBox,center);
                Vector3 incoming=model->Get_Transform().Get_X_Vector()*(region%2?1.0f:-1.0f);
                if(Attach_Thermal_Remains(model,hit.Bone,center,incoming,stage==3))++attached;
            }
        }
    }
}

// Thermal geometry is owned by the actor's HLOD, not a global raw-pointer list.
// A severity update replaces that region. Destruction/unload releases its meshes.
static void Remove_Thermal_Surface(RenderObjClass *model,const char *name)
{
    if(!model)return;
    for(int i=model->Get_Num_Sub_Objects()-1;i>=0;--i){
        RenderObjClass *child=model->Get_Sub_Object(i);if(!child)continue;
        const char *n=child->Get_Name();
        if(n && !strnicmp(n,"G_THERMAL_",10) && (!name || !stricmp(n,name)))model->Remove_Sub_Object(child);
        else Remove_Thermal_Surface(child,name);
        child->Release_Ref();
    }
    if(model->Get_HTree())model->Get_Bone_Transform(0);
}
static int Add_Thermal_Surface(RenderObjClass *model,const Vector3 &point,const Vector3 &normal,
    float radius,int stage,int profile,int region_bone,const char *name,int remaining,bool whole_region=false)
{
    if(!model || remaining<=0 || model->Is_Hidden())return 0;
    const char *n=model->Get_Name();
    if(n && (!strnicmp(n,"G_",2) || !strnicmp(n,"W_",2) || !strnicmp(n,"F_",2)))return 0;
    if(model->Class_ID()==RenderObjClass::CLASSID_MESH){
        MeshClass *patch=((MeshClass*)model)->Create_Thermal_Surface(point,normal,radius,stage,profile,region_bone,name,whole_region);
        if(patch){Gore_Test_Log("thermal surface %s: %d triangles / 2 passes profile %d stage %d\n",name,patch->Peek_Model()->Get_Polygon_Count(),profile,stage);patch->Release_Ref();return 1;}
        return 0;
    }
    int added=0;
    if(model->Class_ID()==RenderObjClass::CLASSID_HLOD){
        HLodClass *lod=(HLodClass*)model;int level=lod->Get_Lod_Count()-1;if(level<0)return 0;
        for(int i=0;i<lod->Get_Lod_Model_Count(level) && added<remaining;++i)
            added+=Add_Thermal_Surface(lod->Peek_Lod_Model(level,i),point,normal,radius,stage,profile,region_bone,name,remaining-added,whole_region);
        // Authored skinned accessories may live outside the ordinary LOD list.
        int count=lod->Get_Additional_Model_Count();
        for(int i=0;i<count && added<remaining;++i)
            added+=Add_Thermal_Surface(lod->Peek_Additional_Model(i),point,normal,radius,stage,profile,region_bone,name,remaining-added,whole_region);
        return added;
    }
    int count=model->Get_Num_Sub_Objects();
    for(int i=0;i<count && added<remaining;++i){RenderObjClass *child=model->Get_Sub_Object(i);
        added+=Add_Thermal_Surface(child,point,normal,radius,stage,profile,region_bone,name,remaining-added,whole_region);if(child)child->Release_Ref();}
    return added;
}
static int Thermal_Surface_Profile(SoldierGameObj *soldier,const char *box)
{
    char path[MAX_PATH];Get_Exe_Relative_Path("burnprofiles.ini",path,sizeof(path));
    const char *preset=soldier->Get_Definition().Get_Name();
    int fallback=(int)GetPrivateProfileIntA("Default",box?box:"K_CHEST",0,path);
    const char *model=soldier->Peek_Model()?soldier->Peek_Model()->Get_Name():"Default";
    fallback=(int)GetPrivateProfileIntA(model,box?box:"K_CHEST",fallback,path);
    int profile=(int)GetPrivateProfileIntA(preset,box?box:"K_CHEST",fallback,path);
    return profile<0?0:profile>2?2:profile;
}

static unsigned Thermal_Surface_Vertex_Count(RenderObjClass *model)
{
    if(!model)return 0;
    const char *name=model->Get_Name();
    if(model->Class_ID()==RenderObjClass::CLASSID_MESH && name && !strnicmp(name,"G_THERMAL_",10))
        return ((MeshClass*)model)->Peek_Model()->Get_Vertex_Count();
    unsigned count=0;
    for(int i=0;i<model->Get_Num_Sub_Objects();++i){RenderObjClass *child=model->Get_Sub_Object(i);
        count+=Thermal_Surface_Vertex_Count(child);if(child)child->Release_Ref();}
    return count;
}
static bool Thermal_Geometry_Budget_Available()
{
    unsigned count=0;
    for(unsigned i=0;i<ARRAY_COUNT(_WoundBudgets);++i){
        const WoundBudget &budget=_WoundBudgets[i];if(!budget.BurnStage)continue;
        PhysicalGameObj *object=GameObjManager::Find_PhysicalGameObj(budget.Id);
        if(object && object->Peek_Model()==budget.Model)count+=Thermal_Surface_Vertex_Count(object->Peek_Model());
        // Reserve the worst-case two-mesh update before touching existing marks.
        if(count>150000-12288)return false;
    }
    return true;
}

static void Rename_Thermal_Surface(RenderObjClass *model,const char *old_name,const char *new_name)
{
    if(!model)return;
    if(model->Get_Name() && !stricmp(model->Get_Name(),old_name)){model->Set_Name(new_name);return;}
    for(int i=0;i<model->Get_Num_Sub_Objects();++i){RenderObjClass *child=model->Get_Sub_Object(i);
        Rename_Thermal_Surface(child,old_name,new_name);if(child)child->Release_Ref();}
}
static void Extinguish_Thermal_Emitters(RenderObjClass *model)
{
    if(!model)return;const char *name=model->Get_Name();
    if(model->Class_ID()==RenderObjClass::CLASSID_PARTICLEEMITTER && name &&
        (!stricmp(name,"G_BURN_SMOKE") || !stricmp(name,"G_BURN_EMBERS")))((ParticleEmitterClass*)model)->Stop();
    for(int i=0;i<model->Get_Num_Sub_Objects();++i){RenderObjClass *child=model->Get_Sub_Object(i);
        Extinguish_Thermal_Emitters(child);if(child)child->Release_Ref();}
}
