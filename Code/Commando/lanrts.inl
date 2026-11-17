// Live overhead commander, sharing the same actors and economy as first person.
#include "ccamera.h"
#include "assetmgr.h"
#include "font3d.h"
#include "physcoltest.h"
#include "lineseg.h"
#include "combatphysobserver.h"



void cGameDataCnc::Open_LAN_Command_View() {if(Is_Offline_Command_Mode())_LANRTSRequested=true;}

bool cGameDataCnc::Possess_LAN_Unit(SoldierGameObj *unit)
{
    if(!Is_Offline_Command_Mode() || !COMBAT_STAR || !unit || unit->Get_Defense_Object()->Get_Health()<=0 || unit->Get_Player_Type()!=COMBAT_STAR->Get_Player_Type())return false;
    if(unit==COMBAT_STAR)return true;
    int slot=-1;for(int i=0;i<64;++i)if(LANBots[i].Get_Ptr()==unit){slot=i;break;}if(slot<0)return false;
    SoldierGameObj *previous=COMBAT_STAR;PlayerDataClass *data=previous->Get_Player_Data();if(!data)return false;
    int player_owner=previous->Get_Control_Owner();if(player_owner<0)player_owner=cNetwork::Get_My_Id();
    previous->Set_Control_Owner(SmartGameObj::SERVER_CONTROL_OWNER);previous->Set_Player_Data(NULL);
    if(previous->Get_Vehicle() && previous->Get_Vehicle()->Get_Driver()==previous){previous->Get_Vehicle()->Clear_Control();previous->Get_Vehicle()->Get_Action()->Reset(100);previous->Get_Vehicle()->Set_Control_Owner(SmartGameObj::SERVER_CONTROL_OWNER);}
    if(previous->Get_Defense_Object()->Get_Health()>0) {
        if(!previous->Get_Innate_Observer()){previous->Set_Innate_Observer(new SoldierObserverClass);previous->Add_Observer(previous->Get_Innate_Observer());previous->Start_Observers();}
        previous->Innate_Disable();previous->Get_Action()->Reset(100);LAN_Ready_Actor(previous);LANBots[slot]=previous;
    } else {LANBots[slot]=NULL;LANBotRespawn[slot]=5;}
    unit->Get_Action()->Reset(100);unit->Set_Control_Owner(player_owner);unit->Set_Player_Data(data);data->Set_GameObj(unit);
    CombatManager::Set_The_Star(unit);LANBotRepairTarget[slot]=NULL;LANBotAvoidVehicle[slot]=NULL;LANBotBoardAge[slot]=0;LANBotBoardDistance[slot]=1.0e30f;LANBotVehicle[slot]=NULL;LANBotGoal[slot]=NULL;LANBotSelected[slot]=false;
    LAN_Log("possessed unit",slot,(float)unit->Get_ID());return true;
}

class LANRTSDialog;
class LANRTSViewport : public DialogControlClass {
    LANRTSDialog *View;bool Dragging;Vector2 Start,End;
public:
    LANRTSViewport(LANRTSDialog *view):View(view),Dragging(false){}
    void Render();
    void On_LButton_Down(const Vector2 &p){Set_Capture();Set_Focus();Start=End=p;Dragging=true;}
    void On_Mouse_Move(const Vector2 &p){if(Dragging)End=p;}
    void On_LButton_Up(const Vector2 &p);
    void On_RButton_Down(const Vector2 &p);
    void Issue_Order(const Vector2 &p,int command=0);
    void On_Mouse_Wheel(int direction);
};

class LANRTSDialog : public PopupDialogClass {
    RenderObjClass *Host;Render2DTextClass *Text;int Owner;
    float SavedNear,SavedFar,SavedFogStart,SavedFogEnd;bool SavedFog;
    char Status[160];
public:
    Vector3 Center,Point;float Height;int PendingCommand;
    void Set_Status(const char *message){_snprintf(Status,sizeof(Status),"%s",message);Status[sizeof(Status)-1]=0;}
    LANRTSDialog():PopupDialogClass(IDD_MESSAGEBOX_OK),Host(NULL),Text(NULL),Owner(-1),Center(0,0,0),Point(0,0,0),Height(65),PendingCommand(0){SavedNear=.1f;SavedFar=1000;SavedFogStart=0;SavedFogEnd=1000;SavedFog=false;Set_Status("Select friendly units, then right click to order.");}
    ~LANRTSDialog() {
        CombatManager::Set_Command_View(false);
        if(COMBAT_CAMERA && Host){COMBAT_CAMERA->Set_Host_Model(NULL);COMBAT_CAMERA->Set_Clip_Planes(SavedNear,SavedFar);}
        if(COMBAT_SCENE){COMBAT_SCENE->Set_Fog_Enable(SavedFog);COMBAT_SCENE->Set_Fog_Range(SavedFogStart,SavedFogEnd);}
        if(Host)Host->Release_Ref();delete Text;_LANRTSActive=false;_LANRTSCurrentSelected=false;
        if(_LANCommandGame && _LANCommandGame->Is_Offline_Command_Mode() && COMBAT_STAR && Owner>=0) {
            COMBAT_STAR->Set_Control_Owner(Owner);COMBAT_STAR->Innate_Disable();COMBAT_STAR->Get_Action()->Reset(100);
            COMBAT_STAR->Clear_Control();
            ActionParamsStruct a;COMBAT_STAR->Get_Action()->Follow_Input(a);
            if(COMBAT_STAR->Get_Vehicle()){COMBAT_STAR->Get_Vehicle()->Clear_Control();COMBAT_STAR->Get_Vehicle()->Enable_Engine(true);}
        }
    }
    void Set_Camera() {
        if(!Host || !COMBAT_CAMERA)return;
        Matrix3D tm(true);tm.Look_At(Center+Vector3(0,-Height*.48f,Height),Center,0);
        Host->Set_Transform(tm);COMBAT_CAMERA->Set_Transform(tm);
        COMBAT_CAMERA->Set_Clip_Planes(SavedNear,WWMath::Max(SavedFar,3000.0f));
        // Atmospheric distance fog must not obscure ground merely because the
        // command camera is high above it. This is separate from team spotting.
        if(COMBAT_SCENE)COMBAT_SCENE->Set_Fog_Range(SavedFogStart+Height,SavedFogEnd+Height*2);
    }
    void On_Init_Dialog() {
        while(Get_Control_Count()){DialogControlClass *c=Get_Control(0);Remove_Control(c);delete c;}
        const RectClass &res=Render2DClass::Get_Screen_Resolution();Set_Rect(res);
        if(COMBAT_CAMERA)COMBAT_CAMERA->Get_Clip_Planes(SavedNear,SavedFar);
        if(COMBAT_SCENE){SavedFog=COMBAT_SCENE->Get_Fog_Enable();COMBAT_SCENE->Get_Fog_Range(&SavedFogStart,&SavedFogEnd);}
        if(COMBAT_STAR) {
            COMBAT_STAR->Get_Position(&Center);Point=Center;Owner=COMBAT_STAR->Get_Control_Owner();
            if(Owner<0)Owner=cNetwork::Get_My_Id();
            // The native player lookup uses ControlOwner. Keep it while viewing RTS.
            COMBAT_STAR->Clear_Control();
            if(!COMBAT_STAR->Get_Innate_Observer()){COMBAT_STAR->Set_Innate_Observer(new SoldierObserverClass);COMBAT_STAR->Add_Observer(COMBAT_STAR->Get_Innate_Observer());COMBAT_STAR->Start_Observers();}
            COMBAT_STAR->Innate_Disable();COMBAT_STAR->Get_Action()->Reset(100);COMBAT_STAR->Set_Control_Owner(Owner);
            if(COMBAT_STAR->Get_Vehicle()){COMBAT_STAR->Get_Vehicle()->Clear_Control();COMBAT_STAR->Get_Vehicle()->Get_Action()->Reset(100);}
        }
        Host=WW3DAssetManager::Get_Instance()->Create_Render_Obj("G_CHUNK_SM");
        if(!Host || !COMBAT_CAMERA){LAN_Log("commander camera unavailable",-1,0);End_Dialog();return;}
        if(Host && COMBAT_CAMERA){COMBAT_CAMERA->Set_Host_Model(Host);GameObjManager::Activate_Cinematic_Freeze(false);Set_Camera();}
        _LANRTSActive=true;_LANRTSCurrentSelected=false;
        CombatManager::Set_Command_View(true);CombatManager::Set_Command_Mode(true);
        if(_LANCommandGame)for(int i=0;i<64;++i)_LANCommandGame->LANBotSelected[i]=false;
        Font3DInstanceClass *font=WW3DAssetManager::Get_Instance()->Get_Font3DInstance("FONT12x16.TGA");
        if(font){Text=new Render2DTextClass(font);font->Release_Ref();}
        LANRTSViewport *view=new LANRTSViewport(this);view->Set_ID(7900);view->Set_Window_Rect(RectClass(0,res.Height()*.05f,res.Width()*.73f,res.Bottom));Add_Control(view);
        const WCHAR *labels[]={L"Follow",L"Move",L"Attack",L"Board vehicle",L"Hold position",L"Dismiss",L"Deploy infantry",L"Deploy AI vehicle",L"Take control",L"Select team",L"Return to unit"};
        for(int i=0;i<11;++i){ButtonCtrlClass *b=new ButtonCtrlClass;b->Set_ID(7910+i);b->Set_Text(labels[i]);b->Set_Window_Rect(RectClass(res.Width()*.745f,res.Height()*(.11f+i*.068f),res.Width()*.99f,res.Height()*(.164f+i*.068f)));Add_Control(b);}
        PopupDialogClass::On_Init_Dialog();
    }
    void On_Frame_Update() {
        if(!_LANCommandGame || !_LANCommandGame->Is_Offline_Command_Mode() || _LANCommandGame->IsIntermission.Is_True() || !COMBAT_STAR){End_Dialog();return;}
        DWORD process=0;GetWindowThreadProcessId(GetForegroundWindow(),&process);
        if(process==GetCurrentProcessId()) {
            float step=TimeManager::Get_Frame_Seconds()*Height*.65f;
            if(GetAsyncKeyState('W')&0x8000)Center.Y+=step;if(GetAsyncKeyState('S')&0x8000)Center.Y-=step;
            if(GetAsyncKeyState('A')&0x8000)Center.X-=step;if(GetAsyncKeyState('D')&0x8000)Center.X+=step;
        }
        Set_Camera();DialogBaseClass::On_Frame_Update();
    }
    bool On_Key_Down(uint32 key,uint32 data){if(key==VK_ESCAPE){if(PendingCommand){PendingCommand=0;Set_Status("Targeting cancelled.");}else End_Dialog();return true;}return DialogBaseClass::On_Key_Down(key,data);}
    GameObjReference ClickedRef;
    void On_Command(int id,int message,DWORD param) {
        if(!_LANCommandGame)return;int b=id-7910;if(b<0 || b>10)return;
        if(b==10){End_Dialog();return;}
        if(b==9){for(int i=0;i<64;++i){ScriptableGameObj *o=_LANCommandGame->LANBots[i].Get_Ptr();_LANCommandGame->LANBotSelected[i]=o && COMBAT_STAR && o->As_SoldierGameObj()->Get_Player_Type()==COMBAT_STAR->Get_Player_Type() && o->As_SoldierGameObj()->Get_Defense_Object()->Get_Health()>0;}Set_Status("Friendly team selected. Right click a destination or target.");return;}
        if(b==8){
            SoldierGameObj *unit=NULL;for(int i=0;i<64;++i)if(_LANCommandGame->LANBotSelected[i] && _LANCommandGame->LANBots[i].Get_Ptr()){unit=_LANCommandGame->LANBots[i].Get_Ptr()->As_SoldierGameObj();break;}
            PhysicalGameObj *clicked=ClickedRef.Get_Ptr()?ClickedRef.Get_Ptr()->As_PhysicalGameObj():NULL;
            VehicleGameObj *vehicle=clicked?clicked->As_VehicleGameObj():NULL;
            if(vehicle && (vehicle->Get_Player_Type()==COMBAT_STAR->Get_Player_Type() || vehicle->Get_Player_Type()==PLAYERTYPE_NEUTRAL)){
                vehicle->Reconcile_Occupants();
                if(vehicle->Get_Driver())unit=vehicle->Get_Driver();
                else {
                    VehicleGameObj *old=COMBAT_STAR->Get_Vehicle();if(old)old->Remove_Occupant(COMBAT_STAR);
                    vehicle->Add_Occupant(COMBAT_STAR,0);if(COMBAT_STAR->Get_Vehicle()==vehicle){End_Dialog();return;}
                }
            }
            if(_LANRTSCurrentSelected && !vehicle)unit=COMBAT_STAR;
            if(unit && _LANCommandGame->Possess_LAN_Unit(unit))End_Dialog();return;
        }
        if(b==6 || b==7){
            if(!COMBAT_STAR)return;bool vehicle=b==7;int team=COMBAT_STAR->Get_Player_Type();
            PurchaseSettingsDefClass::TYPE type=vehicle?PurchaseSettingsDefClass::TYPE_VEHICLES:PurchaseSettingsDefClass::TYPE_CLASSES;
            PurchaseSettingsDefClass *table=PurchaseSettingsDefClass::Find_Definition(type,(PurchaseSettingsDefClass::TEAM)(1-team));if(!table)return;
            CNCPurchaseMenuClass *dialog=new CNCPurchaseMenuClass;dialog->Set_Definition(table);dialog->Set_Type(type);dialog->Set_Deploy_AI(true);
            dialog->Set_Team(team==PLAYERTYPE_GDI?PlayerTerminalClass::TYPE_GDI:PlayerTerminalClass::TYPE_NOD);
            BaseControllerClass *base=BaseControllerClass::Find_Base(team);if(base)dialog->Set_Cost_Scaling_Factor(base->Is_Base_Powered()?1:2);
            End_Dialog();dialog->Start_Dialog();dialog->Release_Ref();return;
        }
        if(b==1 || b==2 || b==3) {
            PendingCommand=b+1;
            Set_Status(b==1?"MOVE: click reachable ground.":b==2?"ATTACK: click an enemy unit or building.":"BOARD: click a friendly or empty vehicle.");return;
        }
        PendingCommand=0;
        Set_Status(_LANCommandGame->Order_LAN_Squad(b+1,ClickedRef.Get_Ptr(),Point)?"Order accepted.":"No friendly units selected.");
    }
    void Render() {
        const RectClass &res=Render2DClass::Get_Screen_Resolution();Render2DClass panel;panel.Set_Coordinate_Range(res);panel.Enable_Texturing(false);
        panel.Add_Quad(RectClass(res.Width()*.735f,0,res.Right,res.Bottom),0xF0102118);panel.Add_Quad(RectClass(0,0,res.Right,res.Height()*.05f),0xDF102118);panel.Render();
        if(Text){Text->Reset();Text->Set_Coordinate_Range(res);Text->Set_Location(Vector2(8,8));Text->Draw_Text("COMMAND: drag select / right click order | WASD pan | wheel zoom | Esc return",0xFFACFF58);
            char line[120];_snprintf(line,sizeof(line),"CREDITS %d",COMBAT_STAR && COMBAT_STAR->Get_Player_Data()?(int)COMBAT_STAR->Get_Player_Data()->Get_Money():0);Text->Set_Location(Vector2(res.Width()*.75f,res.Height()*.065f));Text->Draw_Text(line,0xFFFFFF60);
            int counts[2]={0,0};if(_LANCommandGame)for(int i=0;i<64;++i){ScriptableGameObj *o=_LANCommandGame->LANBots[i].Get_Ptr();if(o && o->As_SoldierGameObj() && o->As_SoldierGameObj()->Get_Defense_Object()->Get_Health()>0)++counts[i/32];}
            _snprintf(line,sizeof(line),"BOTS: NOD %d / GDI %d  (target: 63 total)",counts[0],counts[1]);Text->Set_Location(Vector2(res.Width()*.75f,res.Height()*.087f));Text->Draw_Text(line,0xFFACFF58);
            Text->Set_Location(Vector2(8,res.Height()*.055f));Text->Draw_Text(Status,0xFFFFFFFF);Text->Render();}
        DialogBaseClass::Render();
    }
};

static bool LAN_Screen_Point(const Vector3 &p,Vector2 &screen)
{
    if(!COMBAT_CAMERA)return false;Vector3 projected;
    if(COMBAT_CAMERA->Project(projected,p)!=CameraClass::INSIDE_FRUSTUM)return false;
    const RectClass &res=Render2DClass::Get_Screen_Resolution();screen.Set((projected.X+1)*res.Width()*.5f,(1-projected.Y)*res.Height()*.5f);return true;
}
static bool LAN_World_Point(const Vector2 &p,Vector3 &point,ScriptableGameObj **hit=NULL)
{
    if(!COMBAT_CAMERA)return false;const RectClass &res=Render2DClass::Get_Screen_Resolution();Vector3 rayPoint;
    COMBAT_CAMERA->Un_Project(rayPoint,Vector2(p.X/res.Width()*2-1,1-p.Y/res.Height()*2));
    Vector3 start=COMBAT_CAMERA->Get_Transform().Get_Translation(),direction=rayPoint-start;direction.Normalize();
    CastResultStruct result;LineSegClass ray(start,start+direction*3000);PhysRayCollisionTestClass test(ray,&result,DEFAULT_COLLISION_GROUP,COLLISION_TYPE_PHYSICAL);
    COMBAT_SCENE->Cast_Ray(test);if(result.StartBad || result.Fraction>=1)return false;point=start+direction*(3000*result.Fraction);
    if(hit){*hit=NULL;if(test.CollidedPhysObj && test.CollidedPhysObj->Get_Observer())*hit=((CombatPhysObserverClass*)test.CollidedPhysObj->Get_Observer())->As_DamageableGameObj();}
    return true;
}
void LANRTSViewport::On_Mouse_Wheel(int direction){View->Height=WWMath::Clamp(View->Height*(direction>0?.85f:1.15f),25.0f,220.0f);View->Set_Camera();}
void LANRTSViewport::On_LButton_Up(const Vector2 &p)
{
    Release_Capture();if(!_LANCommandGame || !COMBAT_STAR)return;End=p;Dragging=false;
    if(View->PendingCommand){Issue_Order(p,View->PendingCommand);return;}
    bool add=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,box=(End-Start).Length2()>64;
    if(!add){for(int i=0;i<64;++i)_LANCommandGame->LANBotSelected[i]=false;_LANRTSCurrentSelected=false;}
    RectClass rect(WWMath::Min(Start.X,End.X)-8,WWMath::Min(Start.Y,End.Y)-8,WWMath::Max(Start.X,End.X)+8,WWMath::Max(Start.Y,End.Y)+8);
    float closest=324;int chosen=-1;
    for(int i=0;i<64;++i) {
        ScriptableGameObj *o=_LANCommandGame->LANBots[i].Get_Ptr();SoldierGameObj *bot=o?o->As_SoldierGameObj():NULL;
        if(!bot || bot->Get_Player_Type()!=COMBAT_STAR->Get_Player_Type() || bot->Get_Defense_Object()->Get_Health()<=0)continue;
        Vector3 pos;if(bot->Get_Vehicle())bot->Get_Vehicle()->Get_Position(&pos);else bot->Get_Position(&pos);Vector2 screen;if(!LAN_Screen_Point(pos+Vector3(0,0,1),screen))continue;
        if(box){if(screen.X>=rect.Left && screen.X<=rect.Right && screen.Y>=rect.Top && screen.Y<=rect.Bottom)_LANCommandGame->LANBotSelected[i]=true;}
        else {float distance=(screen-p).Length2();if(distance<closest){closest=distance;chosen=i;}}
    }
    if(chosen>=0)_LANCommandGame->LANBotSelected[chosen]=true;
    if(!box && chosen<0){Vector3 pos;COMBAT_STAR->Get_Position(&pos);Vector2 screen;if(LAN_Screen_Point(pos+Vector3(0,0,1),screen) && (screen-p).Length2()<324)_LANRTSCurrentSelected=true;}
    ScriptableGameObj *clicked=NULL;LAN_World_Point(p,View->Point,&clicked);View->ClickedRef=clicked;
    PhysicalGameObj *physical=clicked?clicked->As_PhysicalGameObj():NULL;VehicleGameObj *vehicle=physical?physical->As_VehicleGameObj():NULL;
    if(vehicle && (vehicle->Get_Player_Type()==COMBAT_STAR->Get_Player_Type() || vehicle->Get_Player_Type()==PLAYERTYPE_NEUTRAL)){
        vehicle->Reconcile_Occupants();SoldierGameObj *driver=vehicle->Get_Driver();
        if(!box && driver)for(int i=0;i<64;++i)if(_LANCommandGame->LANBots[i].Get_Ptr()==driver)_LANCommandGame->LANBotSelected[i]=true;
        if(driver==COMBAT_STAR)_LANRTSCurrentSelected=true;
    }
    int count=_LANRTSCurrentSelected?1:0;for(int i=0;i<64;++i)if(_LANCommandGame->LANBotSelected[i])++count;
    char status[120];_snprintf(status,sizeof(status),"%d friendly unit(s) selected. Right click to issue an order.",count);View->Set_Status(status);
}
void LANRTSViewport::On_RButton_Down(const Vector2 &p){Issue_Order(p,View->PendingCommand);}
void LANRTSViewport::Issue_Order(const Vector2 &p,int requested)
{
    if(!_LANCommandGame || !COMBAT_STAR)return;
    ScriptableGameObj *target=NULL;
    if(!LAN_World_Point(p,View->Point,&target)){View->Set_Status("No ground or target under the cursor.");return;}
    float best=400;
    // A direct ray hit is preferable to the unit's small projected origin.
    if(target && !target->As_DamageableGameObj())target=NULL;
    if(!target)
    for(SLNode<SmartGameObj> *n=GameObjManager::Get_Smart_Game_Obj_List()->Head();n;n=n->Next()) {
        SmartGameObj *o=n->Data();if(o->Is_Delete_Pending() || o->Get_Defense_Object()->Get_Health()<=0)continue;
        Vector3 pos;o->Get_Position(&pos);Vector2 screen;if(!LAN_Screen_Point(pos+Vector3(0,0,1),screen))continue;
        float d=(screen-p).Length2();if(d<best){best=d;target=o;}
    }
    // Buildings are not in the smart-object list.
    if(!target)for(SLNode<BuildingGameObj> *n=GameObjManager::Get_Building_Game_Obj_List()->Head();n;n=n->Next()) {
        BuildingGameObj *b=n->Data();if(b->Is_Destroyed() || b->Get_Player_Type()==COMBAT_STAR->Get_Player_Type())continue;
        Vector3 pos;b->Get_Position(&pos);if((pos-View->Point).Length2()<900){target=b;break;}
    }
    View->ClickedRef=target;int command=2;
    DamageableGameObj *d=target?target->As_DamageableGameObj():NULL;
    if(d && d->Get_Player_Type()!=PLAYERTYPE_NEUTRAL && d->Get_Player_Type()!=COMBAT_STAR->Get_Player_Type())command=3;
    else if(target && target->As_PhysicalGameObj() && target->As_PhysicalGameObj()->As_VehicleGameObj())command=4;
    if(requested)command=requested;
    if(command==2 && PathfindClass::Get_Instance()) {
        Vector3 reachable;
        if(!PathfindClass::Get_Instance()->Find_Sector(View->Point,2) && PathfindClass::Get_Instance()->Find_Random_Spot(View->Point,10,&reachable))View->Point=reachable;
    }
    bool accepted=_LANCommandGame->Order_LAN_Squad(command,target,View->Point);
    View->Set_Status(accepted?"Order accepted.":"Order unavailable: select allies and choose a valid target.");
    if(accepted)View->PendingCommand=0;
}
void LANRTSViewport::Render()
{
    if(!_LANCommandGame || !COMBAT_STAR)return;Render2DClass marks;marks.Set_Coordinate_Range(Render2DClass::Get_Screen_Resolution());marks.Enable_Texturing(false);
    for(int i=0;i<64;++i){ScriptableGameObj *o=_LANCommandGame->LANBots[i].Get_Ptr();SoldierGameObj *b=o?o->As_SoldierGameObj():NULL;if(!b || b->Get_Defense_Object()->Get_Health()<=0)continue;
        Vector3 pos;if(b->Get_Vehicle())b->Get_Vehicle()->Get_Position(&pos);else b->Get_Position(&pos);Vector2 screen;if(!LAN_Screen_Point(pos+Vector3(0,0,1),screen) || screen.X>Rect.Right)continue;
        unsigned long color=b->Get_Player_Type()==COMBAT_STAR->Get_Player_Type()?0xFF99ED60:0xFFE24A38;
        marks.Add_Outline(RectClass(screen.X-7,screen.Y-7,screen.X+7,screen.Y+7),_LANCommandGame->LANBotSelected[i]?3.0f:1.0f,color);
    }
    if(Dragging)marks.Add_Outline(RectClass(WWMath::Min(Start.X,End.X),WWMath::Min(Start.Y,End.Y),WWMath::Max(Start.X,End.X),WWMath::Max(Start.Y,End.Y)),1,0xFF99ED60);marks.Render();
}
