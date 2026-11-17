// Anatomically neutral thermal surfaces. Included by mesh_wounds.inl.
// The original character remains the healthy layer. Two bounded skinned passes
// add matte carbon and glossy damaged dermis, never organ/rib atlas fragments.
namespace {
TextureClass *ThermalTextures[3][3][2]={0};
TextureClass *ThermalMasks[3][3]={0};
float Thermal_Hash(int x,int y) {
    unsigned h=(unsigned)x*0x8da6b343u^(unsigned)y*0xd8163841u;
    h^=h>>13;h*=0x85ebca6bu;h^=h>>16;return (h&65535)/65535.0f;
}
float Thermal_Value_Noise(float x,float y) {
    int ix=(int)floorf(x),iy=(int)floorf(y);float a=x-ix,b=y-iy;
    a=a*a*(3-2*a);b=b*b*(3-2*b);
    float top=Thermal_Hash(ix,iy)*(1-a)+Thermal_Hash(ix+1,iy)*a;
    float bottom=Thermal_Hash(ix,iy+1)*(1-a)+Thermal_Hash(ix+1,iy+1)*a;
    return top*(1-b)+bottom*b;
}
float Thermal_Noise(float x,float y) {
    return .57f*Thermal_Value_Noise(x,y)+.28f*Thermal_Value_Noise(x*2.17f+13,y*2.17f-7)+
        .15f*Thermal_Value_Noise(x*4.41f-3,y*4.41f+11);
}
TextureClass *Thermal_Texture(int stage,int profile,int layer) {
    TextureClass *&cached=ThermalTextures[stage-1][profile][layer];
    if(cached)return cached;
    cached=NEW_REF(TextureClass,(128,128,WW3D_FORMAT_A8R8G8B8,TextureClass::MIP_LEVELS_1));
    SurfaceClass *surface=cached->Get_Surface_Level();int pitch=0;
    unsigned char *pixels=surface?(unsigned char*)surface->Lock(&pitch):NULL;
    if(!pixels){if(surface)surface->Release_Ref();cached->Release_Ref();cached=NULL;return NULL;}
    // Optional authored albedo. Only the scalar heat/erosion mask is
    // procedural; neither texture contains anatomy or changes shared assets.
    SurfaceClass *albedo=NULL;unsigned char *art=NULL;int art_pitch=0;
    const char *asset=layer?"br_burn_dermis.tga":"br_burn_carbon.tga";
    char asset_path[MAX_PATH];GetModuleFileNameA(NULL,asset_path,sizeof(asset_path));asset_path[sizeof(asset_path)-1]=0;
    char *slash=strrchr(asset_path,'\\');if(slash)slash[1]=0;else asset_path[0]=0;
    if(strlen(asset_path)+strlen(asset)+6<sizeof(asset_path)){
        strcat(asset_path,"Data\\");strcat(asset_path,asset);FILE *exists=fopen(asset_path,"rb");
        if(exists){fclose(exists);TextureClass *source=WW3DAssetManager::Get_Instance()->Get_Texture(asset,TextureClass::MIP_LEVELS_1);
            if(source){source->Init();SurfaceClass *src=source->Get_Surface_Level();
                if(src){SurfaceClass::SurfaceDescription sd;src->Get_Description(sd);
                    if(sd.Width>0 && sd.Height>0){albedo=NEW_REF(SurfaceClass,(128,128,WW3D_FORMAT_A8R8G8B8));
                        albedo->Stretch_Copy(0,0,128,128,0,0,sd.Width,sd.Height,src);art=(unsigned char*)albedo->Lock(&art_pitch);}
                    src->Release_Ref();}source->Release_Ref();}
        }
    }
    if(strstr(GetCommandLineA(),"THERMALTEST=1")){
        FILE *f=fopen("thermal_assets.log","a");if(f){fprintf(f,"asset %s path %s loaded %d pitch %d\n",asset,asset_path,art!=NULL,art_pitch);fclose(f);}
    }
    for(int y=0;y<128;++y)for(int x=0;x<128;++x) {
        float u=(x-63.5f)/63.5f,v=(y-63.5f)/63.5f;
        float noise=WWMath::Clamp(Thermal_Noise(u*3,v*3),0.0f,1.0f);
        float grain=WWMath::Clamp(Thermal_Noise(u*17+7,v*17-3),0.0f,1.0f);
        float radial=sqrtf(u*u+v*v);
        float edge=1.0f-radial+(noise-.5f)*.22f;
        float coverage=WWMath::Clamp(edge*9,0.0f,1.0f);
        float core=WWMath::Clamp((.62f-radial+(noise-.5f)*.32f)*10,0.0f,1.0f);
        float erosion=stage==3?WWMath::Clamp((noise-.50f)*12,0.0f,1.0f):0;
        // Armor only discolors. Flame does not reveal anatomy through a plate.
        float exposed=profile==2?0:core*erosion;
        float alpha=layer?coverage*exposed:coverage*(1.0f-exposed*.8f);
        float red,green,blue;
        if(layer) {
            // Continuous mottled dermis and pale blister islands. No drawn
            // bones, intestines, complete organs, or repeating anatomy motifs.
            float blister=WWMath::Clamp((grain-.70f)*7,0.0f,1.0f);
            red=83+noise*50+blister*36;green=28+noise*19+blister*29;blue=24+noise*13+blister*18;
            alpha*=.94f;
        } else {
            float cracks=WWMath::Clamp((grain-.64f)*7,0.0f,1.0f);
            float fiber=profile==0?.5f+.5f*sinf(x*2.9f)*sinf(y*3.1f):.5f;
            if(stage==1) {red=104+noise*26;green=60+noise*20;blue=38+noise*16;alpha*=.48f;}
            else {red=48+noise*38+cracks*26;green=30+noise*21+cracks*12;blue=23+noise*16;}
            red+=fiber*7;green+=fiber*5;blue+=fiber*3;
            if(profile==1 && stage==1){red=150+noise*25;green=55+noise*16;blue=43+noise*12;}
            if(profile==1 && stage==2){red=126+noise*35;green=52+noise*24;blue=35+noise*13;alpha*=.8f;}
        }
        if(layer && art){const unsigned char *sample=art+y*art_pitch+x*4;
            red=sample[2];green=sample[1];blue=sample[0];}
        if(!layer){
            // Multiplicative carbon preserves the original outfit's folds,
            // stitching and colors. White outside the mask is an identity.
            float amount=WWMath::Clamp(alpha,0.0f,1.0f);
            float carbon=.38f+noise*.23f;
            if(art){const unsigned char *sample=art+y*art_pitch+x*4;
                float luminance=(sample[2]*.3f+sample[1]*.59f+sample[0]*.11f)/255.0f;
                carbon=.28f+luminance*.52f;}
            red=255*carbon;green=255*carbon*.86f;blue=255*carbon*.75f;
        }
        unsigned a=(unsigned)(WWMath::Clamp(alpha,0.0f,1.0f)*255);
        if(strstr(GetCommandLineA(),"THERMALZERO=1"))a=0;
        ((unsigned*)(pixels+y*pitch))[x]=(a<<24)|((unsigned)red<<16)|((unsigned)green<<8)|(unsigned)blue;
    }
    if(art)albedo->Unlock();if(albedo)albedo->Release_Ref();
    if(strstr(GetCommandLineA(),"THERMALTEST=1")) {
        char filename[80];sprintf(filename,"thermal_texture_%d_%d_%d.tga",stage,profile,layer);
        FILE *f=fopen(filename,"wb");if(f){unsigned char header[18]={0};header[2]=2;header[12]=128;header[14]=128;header[16]=32;header[17]=0x28;
            fwrite(header,1,18,f);for(int y=0;y<128;++y)fwrite(pixels+y*pitch,1,128*4,f);fclose(f);}
    }
    surface->Unlock();surface->Release_Ref();
    cached->Set_Texture_Name(layer?"g_thermal_dermis":"g_thermal_carbon");
    cached->Set_U_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);
    cached->Set_V_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);
    return cached;
}
TextureClass *Thermal_Mask(int stage,int profile,TextureClass *carbon)
{
    TextureClass *&cached=ThermalMasks[stage-1][profile];if(cached)return cached;
    SurfaceClass *src=carbon->Get_Surface_Level();int src_pitch=0;
    unsigned char *input=src?(unsigned char*)src->Lock(&src_pitch):NULL;
    if(!input){if(src)src->Release_Ref();return NULL;}
    cached=NEW_REF(TextureClass,(128,128,WW3D_FORMAT_A8R8G8B8,TextureClass::MIP_LEVELS_1));
    SurfaceClass *dst=cached->Get_Surface_Level();int dst_pitch=0;
    unsigned char *output=dst?(unsigned char*)dst->Lock(&dst_pitch):NULL;
    if(!output){if(dst)dst->Release_Ref();src->Unlock();src->Release_Ref();cached->Release_Ref();cached=NULL;return NULL;}
    for(int y=0;y<128;++y)for(int x=0;x<128;++x)
        ((unsigned*)(output+y*dst_pitch))[x]=(((unsigned*)(input+y*src_pitch))[x]&0xff000000u)|0x00ffffffu;
    dst->Unlock();dst->Release_Ref();src->Unlock();src->Release_Ref();cached->Set_Texture_Name("g_thermal_heat_mask");
    cached->Set_U_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);cached->Set_V_Addr_Mode(TextureClass::TEXTURE_ADDRESS_CLAMP);return cached;
}
struct ThermalTriangle {int Source;Vector3 Bary[3];};
}
void MeshClass::Release_Thermal_Textures() {
    for(int s=0;s<3;++s)for(int p=0;p<3;++p){
        for(int l=0;l<2;++l)REF_PTR_RELEASE(ThermalTextures[s][p][l]);REF_PTR_RELEASE(ThermalMasks[s][p]);}
}
MeshClass *MeshClass::Create_Thermal_Surface(const Vector3 &point,const Vector3 &normal,
    float radius,int severity,int profile,int region_bone,const char *name,bool whole_region)
{
    if(!Model || !Container || !Container->Get_HTree() || !Model->Get_Flag(MeshModelClass::SKIN) ||
        radius<=0 || severity<1 || severity>3 || profile<0 || profile>2)return NULL;
    // HLOD attachment/removal rebuilds bounds in the bind pose. Force its
    // animated world pose before sampling or inverse-binding new vertices.
    Container->Get_Bone_Transform(0);
    const HTreeClass *tree=Container->Get_HTree();int nv=Model->Get_Vertex_Count();
    if(nv<=0 || nv>65535)return NULL;
    std::vector<Vector3> world(nv);Get_Deformed_Vertices(&world[0]);
    const TriIndex *polys=Model->Get_Polygon_Array();const uint16 *links=Model->Get_Vertex_Bone_Links();
    std::vector<Vector3> source_normals(nv);Get_Deformed_Vertices(&world[0],&source_normals[0]);
    Vector3 n=normal;if(n.Length2()<.0001f)return NULL;n.Normalize();
    Vector3 axis=fabsf(n.Z)<.9f?Vector3(0,0,1):Vector3(0,1,0),u,v;
    Vector3::Cross_Product(axis,n,&u);u.Normalize();Vector3::Cross_Product(n,u,&v);
    std::vector<ThermalTriangle> triangles;
    for(int p=0;p<Model->Get_Polygon_Count();++p) {
        const TriIndex &t=polys[p];int ids[3]={t.I,t.J,t.K};bool hidden=false;
        for(int k=0;k<3;++k)if(links[ids[k]]>=tree->Num_Pivots() || tree->Is_Skin_Hidden(links[ids[k]]))hidden=true;
        if(hidden)continue;
        if(region_bone>0 && links[t.I]!=region_bone && links[t.J]!=region_bone && links[t.K]!=region_bone)continue;
        TextureClass *texture=Model->Peek_Texture(p,0,0);
        const char *texname=texture?texture->Get_Texture_Name():"";
        if(!stricmp(texname,"g_core_cutout") || !stricmp(texname,"g_tissue.tga"))continue;
        const Vector3 &a=world[t.I],&b=world[t.J],&c=world[t.K];Vector3 fn;
        Vector3::Cross_Product(b-a,c-a,&fn);if(fn.Length2()<1e-10f)continue;fn.Normalize();
        if(Vector3::Dot_Product(fn,n)<-.08f || (!whole_region && Wound_Triangle_Distance(point,a,b,c)>radius*radius))continue;
        // A shallow slab cannot paint the far side of a thin limb or torso.
        if(!whole_region && fabsf(Vector3::Dot_Product((a+b+c)/3-point,n))>radius*.65f)continue;
        float edge=std::max((a-b).Length(),std::max((a-c).Length(),(b-c).Length()));
        int d=std::min(6,std::max(1,(int)ceilf(edge/(whole_region?.07f:.035f))));
        if(triangles.size()+d*d>2048)continue;
        for(int i=0;i<d;++i)for(int j=0;j<d-i;++j) {
            ThermalTriangle q;q.Source=p;
            q.Bary[0]=Vector3(1.0f-(float)(i+j)/d,(float)i/d,(float)j/d);
            q.Bary[1]=q.Bary[0]+Vector3(-1.0f/d,1.0f/d,0);
            q.Bary[2]=q.Bary[0]+Vector3(-1.0f/d,0,1.0f/d);triangles.push_back(q);
            if(j<d-i-1){q.Bary[0]=q.Bary[1];q.Bary[1]+=Vector3(-1.0f/d,0,1.0f/d);triangles.push_back(q);}
        }
    }
    // Clip the generated shell itself to the impact footprint. A source
    // triangle can cross the footprint by a long way on a legacy low LOD.
    // Texture transparency alone must not allow those faces into the shell.
    std::vector<ThermalTriangle> clipped;
    for(unsigned i=0;i<triangles.size();++i){
        const ThermalTriangle &q=triangles[i];const TriIndex &t=polys[q.Source];
        Vector3 bary=(q.Bary[0]+q.Bary[1]+q.Bary[2])/3.0f;
        Vector3 delta=world[t.I]*bary.X+world[t.J]*bary.Y+world[t.K]*bary.Z-point;
        float x=Vector3::Dot_Product(delta,u)/radius,y=Vector3::Dot_Product(delta,v)/radius;
        float radial=sqrtf(x*x+y*y),edge=1.08f+(Thermal_Noise(x*3,y*3)-.5f)*.12f;
        if(whole_region || (radial<edge && fabsf(Vector3::Dot_Product(delta,n))<radius*.95f))clipped.push_back(q);
    }
    triangles.swap(clipped);
    if(triangles.empty())return NULL;
    TextureClass *carbon=Thermal_Texture(severity,profile,0),*dermis=Thermal_Texture(severity,profile,1);
    if(!carbon || !dermis)return NULL;TextureClass *mask=Thermal_Mask(severity,profile,carbon);if(!mask)return NULL;
    MeshClass *patch=NEW_REF(MeshClass,());patch->Model=NEW_REF(MeshModelClass,());MeshModelClass *m=patch->Model;
    int np=(int)triangles.size(),count=np*3;std::vector<Vector3> expected(count);m->Reset(np,count,2);m->Set_Name(name);m->HasBeenInUse=false;
    m->Set_Flag(MeshModelClass::SKIN,true);m->Set_Flag(MeshModelClass::TWO_SIDED,false);patch->Set_Collision_Type(0);
    m->CurMatDesc->Set_UV_Source(0,0,0);m->CurMatDesc->Set_UV_Source(0,1,0);m->CurMatDesc->Set_UV_Source(1,0,0);
    m->CurMatDesc->Set_DCG_Source(0,VertexMaterialClass::COLOR1);m->CurMatDesc->Set_DCG_Source(1,VertexMaterialClass::COLOR1);
    unsigned *colors=m->CurMatDesc->Get_Color_Array(0);
    Vector2 *uv=m->CurMatDesc->Get_UV_Array_By_Index(0);Vector3 *positions=m->Get_Vertex_Array(),*normals=m->get_vert_normals();
    uint16 *bindings=m->get_bone_links();TriIndex *out=m->get_polys();
    VertexMaterialClass *materials[2];
    for(int layer=0;layer<2;++layer){
        materials[layer]=NEW_REF(VertexMaterialClass,());VertexMaterialClass *mat=materials[layer];
        mat->Set_UV_Source(0,0);mat->Set_UV_Source(1,0);
        mat->Set_Lighting(layer!=0);mat->Set_Diffuse_Color_Source(VertexMaterialClass::COLOR1);mat->Set_Ambient(.8f,.8f,.8f);mat->Set_Diffuse(.85f,.85f,.85f);
        mat->Set_Emissive(.12f,.09f,.08f);mat->Set_Opacity(1);
        mat->Set_Specular(layer?.35f:0,layer?.22f:0,layer?.17f:0);mat->Set_Shininess(layer?23.0f:0);
    }
    for(int p=0;p<np;++p) {
        ThermalTriangle &q=triangles[p];const TriIndex &t=polys[q.Source];int ids[3]={t.I,t.J,t.K};
        out[p].I=p*3;out[p].J=p*3+1;out[p].K=p*3+2;
        Vector3 face;Vector3::Cross_Product(world[t.J]-world[t.I],world[t.K]-world[t.I],&face);face.Normalize();
        for(int k=0;k<3;++k) {
            int index=p*3+k;Vector3 bary=q.Bary[k],wp=world[t.I]*bary.X+world[t.J]*bary.Y+world[t.K]*bary.Z;
            Vector3 delta=wp-point;float x=Vector3::Dot_Product(delta,u)/radius,y=Vector3::Dot_Product(delta,v)/radius;
            float local=WWMath::Clamp(1-sqrtf(x*x+y*y),0.0f,1.0f);
            // Millimetre-scale raised blister/fabric curl; anatomy remains intact.
            Vector3 smooth=source_normals[t.I]*bary.X+source_normals[t.J]*bary.Y+source_normals[t.K]*bary.Z;
            if(smooth.Length2()>.0001f)smooth.Normalize();else smooth=face;
            float facing=WWMath::Clamp((Vector3::Dot_Product(smooth,n)-.12f)/.50f,0.0f,1.0f);
            float depth=WWMath::Clamp((radius*.90f-fabsf(Vector3::Dot_Product(delta,n)))/(radius*.55f),0.0f,1.0f);
            if(whole_region)depth=1.0f;
            colors[index]=((unsigned)(255*facing*depth)<<24)|0x00ffffffu;
            wp+=smooth*(.006f+local*(severity>=2?.0035f:0)*WWMath::Clamp(Thermal_Noise(x*8,y*8),0.0f,1.0f));
            expected[index]=wp;
            int bones[3]={links[ids[0]],links[ids[1]],links[ids[2]]};float weights[3]={bary.X,bary.Y,bary.Z};
            for(int a=0;a<3;++a)for(int b=a+1;b<3;++b)if(bones[a]==bones[b]){weights[a]+=weights[b];weights[b]=0;}
            int first=0;for(int a=1;a<3;++a)if(weights[a]>weights[first])first=a;
            int second=first==0?1:0;for(int a=0;a<3;++a)if(a!=first && weights[a]>weights[second])second=a;
            const Matrix3D &tm=tree->Get_Skin_Transform(bones[first]);
            Matrix3D::Inverse_Transform_Vector(tm,wp,&positions[index]);normals[index]=tm.Inverse_Rotate_Vector(smooth);bindings[index]=(uint16)bones[first];
            if(weights[second]>.001f){Vector3 secondary;const Matrix3D &other=tree->Get_Skin_Transform(bones[second]);Matrix3D::Inverse_Transform_Vector(other,wp,&secondary);
                m->Set_Wound_Skin_Influence(index,(uint16)bones[second],weights[second]/(weights[first]+weights[second]),secondary,other.Inverse_Rotate_Vector(smooth));}
            uv[index]=Vector2(.5f+x*.5f,.5f+y*.5f);
            for(int layer=0;layer<2;++layer)m->Set_Material(index,materials[layer],layer);
        }
        for(int layer=0;layer<2;++layer){ShaderClass shader=layer?ShaderClass::_PresetAlphaShader:ShaderClass::_PresetMultiplicativeShader;
            if(!layer){
                // Stage 0 produces white with heat*exposure alpha. Stage 1
                // interpolates carbon with white using that current alpha.
                // Multiplicative blending then preserves original outfit detail.
                shader.Set_Primary_Gradient(ShaderClass::GRADIENT_MODULATE);
                shader.Set_Post_Detail_Color_Func(ShaderClass::DETAILCOLOR_DETAILBLEND);
            }
            shader.Set_Alpha_Test(layer?ShaderClass::ALPHATEST_ENABLE:ShaderClass::ALPHATEST_DISABLE);
            shader.Set_Secondary_Gradient(layer?ShaderClass::SECONDARY_GRADIENT_ENABLE:ShaderClass::SECONDARY_GRADIENT_DISABLE);
            m->Set_Texture(p,layer?dermis:mask,layer,0);
            m->Set_Texture(p,layer?NULL:carbon,layer,1);m->Set_Shader(p,shader,layer);}
    }
    if(strstr(GetCommandLineA(),"THERMALTEST=1")) {
        FILE *f=fopen("thermal_mesh.log","a");if(f){float lo=1e9f,hi=-1e9f;for(int i=0;i<count;++i){lo=std::min(lo,std::min(uv[i].X,uv[i].Y));hi=std::max(hi,std::max(uv[i].X,uv[i].Y));}
        fprintf(f,"stage %d profile %d triangles %d uv %.3f %.3f radius %.3f\n",severity,profile,np,lo,hi,radius);fclose(f);}
        char filename[80];sprintf(filename,"thermal_vertices_%d.csv",severity);f=fopen(filename,"w");
        if(f){fprintf(f,"x,y,z,u,v,bone\n");for(int i=0;i<count;++i)fprintf(f,"%f,%f,%f,%f,%f,%d\n",positions[i].X,positions[i].Y,positions[i].Z,uv[i].X,uv[i].Y,bindings[i]);fclose(f);}
    }
    for(int layer=0;layer<2;++layer)materials[layer]->Release_Ref();
    RenderObjClass *parent=Container;parent->Add_Sub_Object_To_Bone(patch,0);
    if(patch->Get_Container()!=parent){patch->Release_Ref();return NULL;}
    parent->Get_Bone_Transform(0);
    if(strstr(GetCommandLineA(),"THERMALTEST=1")){
        std::vector<Vector3> actual(count);patch->Get_Deformed_Vertices(&actual[0]);float error=0;
        for(int i=0;i<count;++i)error=std::max(error,(actual[i]-expected[i]).Length());
        FILE *f=fopen("thermal_mesh.log","a");if(f){fprintf(f,"attachment max skin error %.6f\n",error);fclose(f);}
    }
    patch->Invalidate_Cached_Bounding_Volumes();return patch;
}
