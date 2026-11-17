// Offline map-completion tiles; original MIX geometry and navigation remain intact.
#include "staticphys.h"
#include "surfaceclass.h"
#include "physcoltest.h"
#include "lineseg.h"
static bool _TerrainCompletionLoaded=false;
static DWORD _TerrainAuditStart=0;
static int _TerrainAuditCapture=0;
static int _GoreAuditCapture=0;

static void Load_Terrain_Completion(const char *map_name)
{
    if(_TerrainCompletionLoaded || !COMBAT_SCENE || !map_name)return;
    const char *name=map_name;
    for(const char *p=map_name;*p;++p)if(*p=='/' || *p=='\\')name=p+1;
    if(strnicmp(name,"C&C_Under.",10))return;
    _TerrainCompletionLoaded=true;
    if(strstr(GetCommandLineA(),"TERRAINFILL=0"))return;
    FILE *manifest=fopen("Data\\br_under_terrain.txt","rt");
    if(!manifest)return;
    static StaticPhysDefClass definition;
    char line[80];int loaded=0,failed=0;
    while(fgets(line,sizeof(line),manifest) && loaded+failed<512) {
        line[strcspn(line,"\r\n")]=0;
        // Names are generated local identifiers, never paths from an arbitrary file.
        if(strncmp(line,"br_u_",5) || strlen(line)>24 || strspn(line,"abcdefghijklmnopqrstuvwxyz0123456789_")!=strlen(line))continue;
        char filename[48];sprintf(filename,"%s.w3d",line);
        WW3DAssetManager::Get_Instance()->Load_3D_Assets(filename);
        RenderObjClass *model=WW3DAssetManager::Get_Instance()->Create_Render_Obj(line);
        if(!model){++failed;continue;}
        StaticPhysClass *tile=NEW_REF(StaticPhysClass,());
        tile->Init(definition);tile->Set_Model(model);tile->Enable_Dont_Save(true);
        tile->Set_Transform(Matrix3D::Identity);
        COMBAT_SCENE->Add_Static_Object(tile);
        tile->Release_Ref();model->Release_Ref();++loaded;
    }
    fclose(manifest);
    int backdrops=0;
    if(loaded>0 && failed==0) {
        RefPhysListIterator it=COMBAT_SCENE->Get_Static_Object_Iterator();
        for(it.First();!it.Is_Done();it.Next()) {
            RenderObjClass *object=it.Peek_Obj()->Peek_Model();
            if(object && !stricmp(object->Get_Name(),"MP_UNDER.BACKDROP")) {
                object->Set_Hidden(true);++backdrops;
            }
        }
    }
    int collision_hits=0;
    if(strstr(GetCommandLineA(),"TERRAINAUDIT=1")) {
        const Vector3 points[]={Vector3(-300,0,0),Vector3(200,0,0),Vector3(-53,-240,0),Vector3(-53,240,0)};
        for(int i=0;i<4;++i) {
            LineSegClass ray(points[i]+Vector3(0,0,150),points[i]-Vector3(0,0,150));
            CastResultStruct result;
            PhysRayCollisionTestClass test(ray,&result,DEFAULT_COLLISION_GROUP,COLLISION_TYPE_PHYSICAL);
            COMBAT_SCENE->Cast_Ray(test);
            if(test.CollidedPhysObj && test.CollidedPhysObj->Peek_Model() &&
                !strnicmp(test.CollidedPhysObj->Peek_Model()->Get_Name(),"br_u_",5))++collision_hits;
        }
    }
    FILE *log=fopen("terrain_completion.log","at");
    if(log){fprintf(log,"Under exterior: loaded=%d failed=%d backdrops_hidden=%d audit_collision=%d/4; original world retained\n",loaded,failed,backdrops,collision_hits);fclose(log);}
}

static void Set_Terrain_Audit_Camera(void)
{
    if(!strstr(GetCommandLineA(),"TERRAINAUDIT=1") || !COMBAT_CAMERA)return;
    if(!_TerrainAuditStart) {
        _TerrainAuditStart=GetTickCount();
        FILE *log=fopen("terrain_capture.log","at");
        if(log){fprintf(log,"audit camera started: %s\n",CombatManager::Get_Last_LSD_Name());fclose(log);}
    }
    DWORD elapsed=GetTickCount()-_TerrainAuditStart;
    int stage=WWMath::Min(3,(int)(elapsed/5000));
    const Vector3 centers[]={Vector3(-53,-4,8),Vector3(-140,-25,10),Vector3(-25,80,12),Vector3(-53,-4,8)};
    const Vector3 offsets[]={Vector3(0,-330,330),Vector3(-100,-160,160),Vector3(130,100,160),Vector3(0,0,490)};
    Matrix3D tm(true);tm.Look_At(centers[stage]+offsets[stage],centers[stage],0);
    COMBAT_CAMERA->Set_Transform(tm);COMBAT_CAMERA->Set_Clip_Planes(.3f,3000);
    COMBAT_SCENE->Set_Fog_Enable(false);
    CombatManager::Set_Command_Mode(true);
}

// Called after End_Render, when the native framebuffer can be captured safely.
void BR_Capture_Terrain_Audit(void)
{
    bool gore=strstr(GetCommandLineA(),"GOREAUDIT=1") && strstr(GetCommandLineA(),"WOUNDTEST") && COMBAT_STAR;
    if(!gore && (!_TerrainAuditStart || !strstr(GetCommandLineA(),"TERRAINAUDIT=1")))return;
    int requested=0;
    if(gore) {
        char path[MAX_PATH];GetModuleFileNameA(NULL,path,sizeof(path));char *slash=strrchr(path,'\\');
        if(!slash)return;strcpy(slash+1,"gorecapture.ini");
        requested=GetPrivateProfileIntA("Capture","Request",0,path);
    }
    DWORD elapsed=GetTickCount()-_TerrainAuditStart;
    if(gore ? requested>_GoreAuditCapture : (_TerrainAuditCapture<4 && elapsed>=(DWORD)(4000+_TerrainAuditCapture*5000))) {
        char name[64];sprintf(name,gore?"gore_render_%03d.tga":"terrain_render_%03d.tga",gore?requested:_TerrainAuditCapture);
        // Capture the game's own backbuffer, never the desktop front buffer.
        SurfaceClass *surface=DX8Wrapper::_Get_DX8_Back_Buffer();
        if(surface) {
            D3DSURFACE_DESC desc;surface->Peek_D3D_Surface()->GetDesc(&desc);
            D3DLOCKED_RECT locked;
            IDirect3DSurface8 *readback=NULL;
            HRESULT result=DX8Wrapper::_Get_D3D_Device8()->CreateImageSurface(desc.Width,desc.Height,desc.Format,&readback);
            if(SUCCEEDED(result))result=DX8Wrapper::_Get_D3D_Device8()->CopyRects(surface->Peek_D3D_Surface(),NULL,0,readback,NULL);
            if(SUCCEEDED(result))result=readback->LockRect(&locked,NULL,D3DLOCK_READONLY);
            if(SUCCEEDED(result)) {
                if(desc.Format==D3DFMT_X8R8G8B8 || desc.Format==D3DFMT_A8R8G8B8) {
                    FILE *file=fopen(name,"wb");
                    if(file) {
                        unsigned char header[18]={0};header[2]=2;header[12]=(unsigned char)desc.Width;header[13]=(unsigned char)(desc.Width>>8);
                        header[14]=(unsigned char)desc.Height;header[15]=(unsigned char)(desc.Height>>8);header[16]=32;header[17]=32;
                        fwrite(header,1,18,file);
                        for(unsigned y=0;y<desc.Height;++y)fwrite((char *)locked.pBits+y*locked.Pitch,4,desc.Width,file);
                        fclose(file);
                    }
                }
                readback->UnlockRect();
            }
            FILE *log=fopen(gore?"gore_capture.log":"terrain_capture.log","at");
            if(log){fprintf(log,"capture=%d format=%d lock=%08x width=%u height=%u\n",gore?requested:_TerrainAuditCapture,(int)desc.Format,(unsigned)result,desc.Width,desc.Height);fclose(log);}
            if(readback)readback->Release();
            surface->Release_Ref();
        }
        if(gore)_GoreAuditCapture=requested;else ++_TerrainAuditCapture;
    }
    if(!gore && elapsed>21000)PostQuitMessage(0);
}
