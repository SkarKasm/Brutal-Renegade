// Included by vrplayer.cpp after its low-level overlay drawing helpers.
static int Wheel_Weapon_Pages()
{
    return ((int)_WheelIDs.size() + 7) / 8;
}
static WeaponClass *Wheel_Weapon(int slot)
{
    WeaponBagClass *bag=COMBAT_STAR?COMBAT_STAR->Get_Weapon_Bag():NULL;
    if(!bag || slot<0 || slot>=(int)_WheelIDs.size())return NULL;
    for(int i=1;i<bag->Get_Count();++i){WeaponClass *w=bag->Peek_Weapon(i);
        if(w && w->Does_Weapon_Exist() && w->Get_ID()==_WheelIDs[slot])return w;}
    return NULL;
}

static bool Update_Weapon_Wheel(const RVR_Frame &frame)
{
    const RVR_Hand &right=frame.Hand[RVR_HAND_RIGHT],&left=frame.Hand[RVR_HAND_LEFT];
    if(!_WheelVisible && right.StickX*right.StickX+right.StickY*right.StickY<.08f)_WheelStickBlocked=false;
    if(right.Trigger<.25f)_WheelBlockRight=false;if(left.Trigger<.25f)_WheelBlockLeft=false;
    bool available=On_Foot_Star() && right.Active && !COMBAT_STAR->Is_On_Ladder() &&
        !COMBAT_CAMERA->Is_Using_Host_Model() && !_MagInHand && _ReloadStage==0;
    if(!available){_WheelVisible=false;return false;}
    if(!_WheelVisible && right.Squeeze>.65f && _Previous[RVR_HAND_RIGHT].Squeeze<=.65f){
        WeaponBagClass *bag=COMBAT_STAR->Get_Weapon_Bag();_WheelIDs.clear();
        if(bag)for(int i=1;i<bag->Get_Count();++i){WeaponClass *w=bag->Peek_Weapon(i);
            if(w && w->Does_Weapon_Exist())_WheelIDs.push_back(w->Get_ID());}
        if (_WheelIDs.empty() && !IS_MISSION) return false;
        _WheelHasQuickSave = IS_MISSION && CombatGameModeClass::Has_Quick_Save();
        _WheelVisible=true;_WheelPage=0;_WheelHover=-1;
        _WheelTracking=Build_Matrix3D(Quaternion(frame.Head.Orientation[0],frame.Head.Orientation[1],frame.Head.Orientation[2],frame.Head.Orientation[3]));
        _WheelTracking.Set_Translation(Vector3(frame.Head.Position[0],frame.Head.Position[1],frame.Head.Position[2]));
        _WheelTracking.Translate(Vector3(0,0,-.70f));
        VRManagerClass::Vibrate(RVR_HAND_RIGHT,.2f,.025f);
    }
    if(!_WheelVisible)return false;
    _SupportLatched=false;_WheelBlockRight=true;_WheelBlockLeft=true;_WheelStickBlocked=true;
    const int weapon_pages=Wheel_Weapon_Pages();
    const int pages=weapon_pages + (IS_MISSION ? 1 : 0);
    bool next=right.Trigger>.55f && _Previous[RVR_HAND_RIGHT].Trigger<=.55f;
    bool previous=left.Trigger>.55f && _Previous[RVR_HAND_LEFT].Trigger<=.55f;
    if(pages>1 && (next || previous)){_WheelPage=(_WheelPage+pages+(next?1:-1))%pages;_WheelHover=-1;}
    bool cancel=(right.Buttons&RVR_BUTTON_SECONDARY) && !(_Previous[RVR_HAND_RIGHT].Buttons&RVR_BUTTON_SECONDARY);
    if(cancel){_WheelVisible=false;return true;}
    const bool quick_page=_WheelPage==weapon_pages;
    int count=WWMath::Min(8,(int)_WheelIDs.size()-_WheelPage*8),hover=-1;
    float radius=sqrtf(right.StickX*right.StickX+right.StickY*right.StickY);
    if (quick_page) {
        if (fabsf(right.StickY)>.35f && fabsf(right.StickY)>fabsf(right.StickX))
            hover=right.StickY>0 ? 0 : 1;
    } else if(radius>.35f){
        float angle=atan2f(right.StickX,right.StickY);if(angle<0)angle+=6.283185307f;
        hover=((int)floorf(angle/(6.283185307f/count)+.5f))%count;
    }
    if(hover!=_WheelHover && hover>=0)VRManagerClass::Vibrate(RVR_HAND_RIGHT,.12f,.018f);
    _WheelHover=hover;
    Matrix3D::Multiply(VRManagerClass::Get_Tracking_To_World(),_WheelTracking,&_WheelWorld);
    if(right.Squeeze<.35f){
        if (quick_page) {
            if (_WheelHover==0) {
                Input::Set_VR_Value(INPUT_FUNCTION_QUICKSAVE, 1.0f);
                VRManagerClass::Vibrate(RVR_HAND_RIGHT,.35f,.035f);
            } else if (_WheelHover==1 && _WheelHasQuickSave) {
                CombatGameModeClass::Queue_Quick_Load();
                VRManagerClass::Vibrate(RVR_HAND_RIGHT,.35f,.035f);
            }
        } else if (_WheelHover>=0) {
            WeaponClass *selected=Wheel_Weapon(_WheelPage*8+_WheelHover);
            if (selected) {
                COMBAT_STAR->Get_Weapon_Bag()->Select_Weapon(selected);
                _LastRoundsFired=-1;
                VRManagerClass::Vibrate(RVR_HAND_RIGHT,.35f,.035f);
            }
        }
        _WheelVisible=false;
    }
    return true; // consume the release frame too: no turn, jump or accidental shot
}

static void Wheel_Label(const char *text,float x,float y,unsigned color)
{
    if(!_WheelText || !text)return;
    _WheelText->Reset();_WheelText->Set_Coordinate_Range(RectClass(0,0,1024,1024));
    WideStringClass wide(text);
    _WheelText->Set_Location(Vector2(x-_WheelText->Get_Text_Extents(wide).X*.5f,y));
    _WheelText->Draw_Text(wide,color);
    _WheelText->Render();
}

void VRPlayerClass::Prepare_Weapon_Wheel()
{
    _WheelValid=false;if(!_WheelVisible || !_InGame)return;
    if(!_WheelTexture)_WheelTexture=DX8Wrapper::Create_Render_Target(1024,1024,WW3D_FORMAT_A8R8G8B8);
    if(!_WheelTexture)return;
    if(!_WheelText){Font3DInstanceClass *font=WW3DAssetManager::Get_Instance()->Get_Font3DInstance("FONT12x16.TGA");
        if(font){font->Set_Scale(2.f);_WheelText=new Render2DTextClass(font);font->Release_Ref();}}
    DX8Wrapper::Set_Render_Target(_WheelTexture);Render2DClass::Set_Render_Target_Size(1024,1024);
    WW3D::Begin_Render(true,false,Vector3(0,0,0));
    Render2DClass art;
    art.Set_Coordinate_Range(RectClass(0,0,1024,1024));
    art.Enable_Texturing(false);
    art.Get_Shader()->Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    const Vector2 center(512,512),uv(0,0);
    const int weapon_pages=Wheel_Weapon_Pages();
    const bool quick_page=_WheelPage==weapon_pages;
    const int count=quick_page ? 2 : WWMath::Min(8,(int)_WheelIDs.size()-_WheelPage*8);
    for(int i=0;i<count;++i){
        const bool enabled=!quick_page || i==0 || _WheelHasQuickSave;
        unsigned color=enabled && i==_WheelHover ? 0xff235e34 : 0xff101f19;
        for(int segment=0;segment<12;++segment){
            float start=(i-.48f)*6.283185307f/count;
            float a=start+segment*.96f*6.283185307f/(count*12),b=start+(segment+1)*.96f*6.283185307f/(count*12);
            Vector2 da(sinf(a),-cosf(a)),db(sinf(b),-cosf(b));
            art.Add_Tri(center+da*204,center+da*480,center+db*480,uv,uv,uv,color);
            art.Add_Tri(center+da*204,center+db*480,center+db*204,uv,uv,uv,color);
        }
    }
    // Opaque central disc keeps instructions legible against the world.
    for(int i=0;i<64;++i){float a=i*6.283185307f/64,b=(i+1)*6.283185307f/64;
        art.Add_Tri(center,center+Vector2(sinf(a),-cosf(a))*196,center+Vector2(sinf(b),-cosf(b))*196,uv,uv,uv,0xff0b1711);}
    art.Render();
    for(int i=0;i<count;++i){
        float angle=i*6.283185307f/count;Vector2 point=center+Vector2(sinf(angle),-cosf(angle))*338;
        if (quick_page) {
            const bool enabled=i==0 || _WheelHasQuickSave;
            Wheel_Label(i==0 ? "QUICKSAVE" : "QUICKLOAD",512,point.Y-17,enabled ? 0xffa5ff8e : 0xff78877d);
            continue;
        }
        WeaponClass *w=Wheel_Weapon(_WheelPage*8+i);if(!w)continue;
        StringClass file;RectClass source;float scale;BR_Weapon_Icon(w->Get_Definition(),file,source,scale);
        Render2DClass icon;icon.Set_Coordinate_Range(RectClass(0,0,1024,1024));icon.Set_Texture(file);
        TextureClass *texture=icon.Peek_Texture();
        if(texture && texture->Get_Width()>0 && texture->Get_Height()>0){
            float aspect=source.Width()/WWMath::Max(source.Height(),1.0f);
            float width=WWMath::Min(174.0f,90.0f*aspect),height=width/aspect;
            source.Scale(Vector2(1.0f/BR_Weapon_Icon_Source_Size(file,texture->Get_Width()),1.0f/BR_Weapon_Icon_Source_Size(file,texture->Get_Height())));
            unsigned tint=w->Get_Total_Rounds()==0 && w->Get_Grenade_Rounds()==0?0xffffff00:0xff00ff00;
            icon.Add_Quad(RectClass(point.X-width*.5f,point.Y-height*.5f,point.X+width*.5f,point.Y+height*.5f),source,tint);icon.Render();
        }
        char ammo[40];int reserve=w->Get_Inventory_Rounds();
        if(reserve<0)_snprintf(ammo,sizeof(ammo),"%d / INF",w->Get_Clip_Rounds());
        else _snprintf(ammo,sizeof(ammo),"%d / %d",w->Get_Clip_Rounds(),reserve);
        if(w->Has_M203())_snprintf(ammo,sizeof(ammo),"%d/%d G:%d",w->Get_Clip_Rounds(),WWMath::Max(0,reserve),w->Get_Grenade_Rounds());
        Wheel_Label(ammo,point.X,point.Y+51,0xffc3e9bd);
    }
    WeaponClass *hover=!quick_page && _WheelHover>=0 ? Wheel_Weapon(_WheelPage*8+_WheelHover) : NULL;
    const char *name=quick_page ? "QUICK ACTIONS" : "WEAPONS";
    if(_WheelHover>=0 && hover){name=BR_Is_MK23_Icon(hover->Get_Definition())?"MK23":TranslateDBClass::Get_English_String(hover->Get_Definition()->IconNameID);
        if(!name || !*name)name=hover->Get_Name();}
    char short_name[25];lstrcpynA(short_name,name,sizeof(short_name));Wheel_Label(short_name,512,453,0xffa5ff8e);
    const char *hint="Centre to cancel";
    if (_WheelHover>=0) {
        hint="Release to equip";
        if (quick_page) {
            hint=_WheelHover==0 ? "Release to save" : (_WheelHasQuickSave ? "Release to load" : "No quicksave");
        }
    }
    Wheel_Label(hint,512,503,0xffd8e3d6);
    if (quick_page) {
        Wheel_Label("Up: save / Down: load",512,551,0xff7ca97c);
    } else {
        char page[45];
        _snprintf(page,sizeof(page),"%d / %d",_WheelPage+1,weapon_pages);
        Wheel_Label(page,512,551,0xff7ca97c);
    }
    if (IS_MISSION && _WheelPage==0 && weapon_pages>0) {
        Wheel_Label("Left trigger: Quick Actions",512,980,0xffc3e9bd);
    } else if (weapon_pages + (IS_MISSION ? 1 : 0)>1) {
        Wheel_Label("Triggers: previous / next page",512,980,0xffc3e9bd);
    }
    WW3D::End_Render(false);Render2DClass::Set_Render_Target_Size(0,0);
    DX8Wrapper::Set_Render_Target((IDirect3DSurface8*)NULL);_WheelValid=true;
}

static void Render_Weapon_Wheel(CameraClass *eye)
{
    if(!_WheelVisible || !_WheelValid || !_WheelTexture)return;
    eye->Apply();OverlayVertex panel[4];
    Set_Quad(panel,_WheelWorld.Get_Translation(),_WheelWorld.Get_X_Vector(),_WheelWorld.Get_Y_Vector(),.28f,.28f,RectClass(0,0,1,1),0xffffffff);
    ShaderClass shader=ShaderClass::_PresetAlphaShader;shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    Draw_Quads(panel,1,shader,_WheelTexture);
}
