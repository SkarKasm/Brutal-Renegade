// Paired ballistic openings. No source asset or shared skeleton is changed.
MeshClass *MeshClass::Carve_Through_Wound(const Vector3 &contact,const Vector3 &incoming,float radius,TextureClass *tissue,bool head_only)
{
    if(!Model || !tissue || !Container || !Container->Get_HTree() || !Model->Get_Flag(MeshModelClass::SKIN))return NULL;
    if(incoming.Length2()<.0001f || radius<=0)return NULL;
    // Adding a tunnel rebuilds HLOD bounds in bind pose. Resolve the live pose
    // before reading endpoints and restore it again after attachment.
    Container->Get_Bone_Transform(0);
    const HTreeClass *tree=Container->Get_HTree();Vector3 direction=incoming;direction.Normalize();
    if(head_only)radius=WWMath::Clamp(radius,.010f,.030f);
    Vector3 entry,exit,en,xn;float distance=.45f;
    if(!Find_Wound_Surface(contact-direction*.12f,direction,distance,entry,en,true))return NULL;
    distance=1.0f;
    if(!Find_Wound_Surface(entry+direction, -direction,distance,exit,xn,true))return NULL;
    float thickness=Vector3::Dot_Product(exit-entry,direction);
    if(thickness<(head_only?.025f:.05f) || thickness>(head_only?.32f:.85f) || Vector3::Dot_Product(en,direction)>-.1f || Vector3::Dot_Product(xn,direction)<.1f)return NULL;
    std::vector<Vector3> world(Model->Get_Vertex_Count());Get_Deformed_Vertices(&world[0]);
    const uint16 *links=Model->Get_Vertex_Bone_Links();int bones[2]={-1,-1};float nearest[2]={1.0e30f,1.0e30f};
    for(int v=0;v<Model->Get_Vertex_Count();++v) {
        if(links[v]>=tree->Num_Pivots() || tree->Is_Skin_Hidden(links[v]))continue;
        float d[2]={(world[v]-entry).Length2(),(world[v]-exit).Length2()};
        for(int k=0;k<2;++k)if(d[k]<nearest[k]){nearest[k]=d[k];bones[k]=links[v];}
    }
    if(bones[0]<0 || bones[1]<0)return NULL;
    if(head_only){
        int head=Container->Get_Bone_Index("C HEAD");
        if(head<=0 || bones[0]!=head || bones[1]!=head)return NULL;
    }
    MeshClass *tube=NEW_REF(MeshClass,());tube->Model=NEW_REF(MeshModelClass,());
    const int segments=32,rings=9,nv=segments*rings,np=segments*(rings-1)*2;
    MeshModelClass *model=tube->Model;model->Reset(np,nv,1);model->HasBeenInUse=false;model->Set_Name("G_WOUND_TUNNEL");
    model->Set_Flag(MeshModelClass::SKIN,true);model->Set_Flag(MeshModelClass::TWO_SIDED,true);
    tube->Set_Collision_Type(0);model->CurMatDesc->Set_UV_Source(0,0,0);
    Vector2 *uv=model->CurMatDesc->Get_UV_Array_By_Index(0);
    Vector3 *vertices=model->Get_Vertex_Array(),*normals=model->get_vert_normals();uint16 *bindings=model->get_bone_links();
    Vector3 axis=fabsf(direction.Z)<.9f?Vector3(0,0,1):Vector3(0,1,0),u,vaxis;
    Vector3::Cross_Product(axis,direction,&u);u.Normalize();Vector3::Cross_Product(direction,u,&vaxis);
    const Matrix3D &a=tree->Get_Skin_Transform(bones[0]),&b=tree->Get_Skin_Transform(bones[1]);
    VertexMaterialClass *material=NEW_REF(VertexMaterialClass,());material->Set_Lighting(true);
    material->Set_Diffuse(Vector3(.8f,.65f,.6f));material->Set_Ambient(Vector3(.8f,.8f,.8f));
    material->Set_Specular(Vector3(.12f,.07f,.07f));material->Set_Shininess(14);
    float exit_scale=1.25f;
    float rim_depth=std::min(radius*.35f,thickness*.12f);
    for(int row=0;row<rings;++row)for(int col=0;col<segments;++col) {
        int i=row*segments+col;float angle=6.2831853f*col/segments;
        Vector3 radial=u*cosf(angle)+vaxis*sinf(angle);
        const float along[9]={0,.015f,.04f,.25f,.5f,.75f,.96f,.985f,1};
        const float bore[9]={1.10f,.70f,.52f,.49f,.52f,.50f,.52f,.70f,1.10f};
        float weight=along[row],scale=1+(exit_scale-1)*weight;
        float ragged=Wound_Rim_Scale(angle,entry)*(1-weight)+Wound_Rim_Scale(angle,exit)*weight;
        if(row<=1 || row>=7){
            // Preserve the common angular order at both ends. Constructing
            // independent frames from opposite surface normals mirrored the
            // entry ring and folded connecting faces across the open bore.
            const Vector3 &outward=row<=1?en:xn;
            Vector3 surface_radial=radial-outward*Vector3::Dot_Product(radial,outward);
            if(surface_radial.Length2()<.0001f){tube->Release_Ref();material->Release_Ref();return NULL;}
            surface_radial.Normalize();
            float blend=(row==0 || row==8)?1.0f:.5f;
            radial=radial*(1-blend)+surface_radial*blend;
            if(radial.Length2()>.0001f)radial.Normalize();
        }
        Vector3 point=entry+(exit-entry)*weight+radial*(radius*bore[row]*scale*ragged);
        Vector3 normal=-radial,secondary;
        if(row==0 || row==8){
            // Overlapping tissue collar sits on the original curved surface,
            // bridging the skin/cutout edge into the cavity without a gap.
            const Vector3 &end=row==0?entry:exit;
            const Vector3 &outward=row==0?en:xn;
            float fit_distance=radius*4;Vector3 fit,fit_normal;
            if(!Find_Wound_Surface(point+outward*radius*2,-outward,fit_distance,fit,fit_normal,true) ||
                (fit-end).Length()>radius*2.5f){tube->Release_Ref();material->Release_Ref();return NULL;}
            point=fit+fit_normal*.0006f;normal=fit_normal;
        }else if(row<=2)point+=direction*rim_depth;
        else if(row>=6)point-=direction*rim_depth;
        // Inner rings drift slightly to break the optical straight-pipe look.
        if(row>=3 && row<=5)point+=u*(radius*.025f*sinf(weight*9))+vaxis*(radius*.025f*cosf(weight*11));
        Matrix3D::Inverse_Transform_Vector(a,point,&vertices[i]);normals[i]=a.Inverse_Rotate_Vector(normal);bindings[i]=bones[0];
        Matrix3D::Inverse_Transform_Vector(b,point,&secondary);
        model->Set_Wound_Skin_Influence(i,(uint16)bones[1],weight,secondary,b.Inverse_Rotate_Vector(normal));
        // Match the existing wound's tissue swatch; never wrap the entire
        // organ atlas repeatedly down the inside of a small bullet channel.
        uv[i]=Vector2(.73f+.055f*cosf(angle)+.014f*sinf(weight*7),.75f+.055f*sinf(angle)+.025f*weight);
        model->Set_Material(i,material,0);
    }
    TriIndex *polys=model->get_polys();int p=0;
    ShaderClass shader=ShaderClass::_PresetOpaqueShader;shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    for(int row=0;row<rings-1;++row)for(int col=0;col<segments;++col) {
        int i=row*segments+col,j=row*segments+(col+1)%segments;
        polys[p].I=i;polys[p].J=j;polys[p].K=i+segments;++p;
        polys[p].I=j;polys[p].J=j+segments;polys[p].K=i+segments;++p;
    }
    for(int i=0;i<np;++i){model->Set_Texture(i,tissue,0,0);model->Set_Shader(i,shader,0);}
    material->Release_Ref();
    // Both openings must succeed. A dense or thin mesh keeps its previous
    // topology instead of receiving one unsupported, hollow opening.
    MeshModelClass *original=Model;original->Add_Ref();float depth=std::min(radius*(head_only?.40f:.55f),thickness*.18f);
    if(!Carve_Wound(entry,en,radius,depth,tissue,true,true) || !Carve_Wound(exit,xn,radius*exit_scale,depth,tissue,true,true)) {
        TheDX8MeshRenderer.Unregister_Mesh_Type(this);
        if(UserLighting){delete[] UserLighting;UserLighting=NULL;}
        REF_PTR_SET(Model,original);original->Release_Ref();tube->Release_Ref();Invalidate_Cached_Bounding_Volumes();return NULL;
    }
    RenderObjClass *parent=Container;parent->Add_Sub_Object_To_Bone(tube,0);
    if(tube->Get_Container()!=parent) {
        TheDX8MeshRenderer.Unregister_Mesh_Type(this);REF_PTR_SET(Model,original);
        original->Release_Ref();tube->Release_Ref();Invalidate_Cached_Bounding_Volumes();return NULL;
    }
    parent->Get_Bone_Transform(0);
    original->Release_Ref();tube->Invalidate_Cached_Bounding_Volumes();return tube;
}
