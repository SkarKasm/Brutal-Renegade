// Local offline command UI and purchases. Included by gdcnc.cpp only.
#include "vehicle.h"
#include "vehiclefactorygameobj.h"
#include "purchasesettings.h"
#include "teampurchasesettings.h"
#include "definitionmgr.h"
#include "playerdata.h"
#include "hudinfo.h"
#include "popupdialog.h"
#include "buttonctrl.h"
#include "dialogmgr.h"
#include "resource.h"
#include "dlgcncpurchasemenu.h"
#include "Pathfind.h"
#include "mapmgr.h"
#include "wwphysids.h"
#include "weapons.h"
#include "weaponbag.h"
#include "humanstate.h"
#include "physcoltest.h"
#include "lineseg.h"
#include <stdio.h>
#include <vector>
#include <algorithm>

static cGameDataCnc *_LANCommandGame=NULL;
static bool _LANRTSActive=false,_LANRTSCurrentSelected=false,_LANRTSRequested=false;
static bool _LANCommandKey=false;
static void LAN_Log(const char *event,int slot,float value=0)
{
    FILE *f=fopen("offline_bots.log","a");
    if(f) {fprintf(f,"%s slot %d value %.2f\n",event,slot,value);fclose(f);}
}
static void LAN_Funds(int team,int funds) {if(_LANCommandGame) _LANCommandGame->Credit_LAN_Bots(team,funds);}
static void LAN_Poke(SoldierGameObj *unit,SoldierGameObj *player)
{
    if(_LANCommandGame && player==COMBAT_STAR) _LANCommandGame->Recruit_LAN_Unit(unit);
}
static bool LAN_Vehicle_Allowed(int definition)
{
    DefinitionClass *def=DefinitionMgrClass::Find_Definition(definition);
    if(!def || def->Get_Class_ID()!=CLASSID_GAME_OBJECT_DEF_VEHICLE)return false;
    PhysicalGameObjDef *physical=(PhysicalGameObjDef*)def;
    DefinitionClass *physics=DefinitionMgrClass::Find_Definition(physical->Get_Phys_Def_ID());
    return physics && (MapMgrClass::Are_VTOL_Vehicles_Enabled() || physics->Get_Class_ID()!=CLASSID_VTOLVEHICLEDEF);
}
bool cGameDataCnc::Is_Offline_Command_Mode() const
{
    return LANBotsEnabled && cNetwork::I_Am_Server() && GameInitMgrClass::Is_LAN_Initialized() && cPlayerManager::Count()<=1;
}
void cGameDataCnc::Credit_LAN_Bots(int team,int funds)
{
    if(team<0 || team>1 || !Is_Offline_Command_Mode() || funds<=0) return;
    // The commander temporarily gives its body to AI and leaves the star list.
    if(_LANRTSActive && COMBAT_STAR && COMBAT_STAR->Get_Player_Type()==team && COMBAT_STAR->Get_Player_Data() && !COMBAT_STAR->Is_Human_Controlled())COMBAT_STAR->Get_Player_Data()->Increment_Money(funds);
    for(int slot=0;slot<32;++slot) LANBotCredits[team*32+slot]+=funds;
    LAN_Log("base income",team,(float)funds);
}
// The offline commander owns these actors' actions. Native campaign innate AI
// otherwise holsters their weapons and replaces orders with 60-second sound searches.
static bool LAN_Is_Repair_Weapon(WeaponClass *weapon)
{
    if(!weapon)return false;
    const AmmoDefinitionClass *ammo=WeaponManager::Find_Ammo_Definition(weapon->Get_Definition()->PrimaryAmmoDefID);
    const char *name=weapon->Get_Name();
    // Some definitions encode healing in the warhead multiplier instead of a
    // negative raw damage value. The native repair-gun family covers both.
    return (ammo && (float)ammo->Damage<0.0f) || (name && (strstr(name,"Repair") || strstr(name,"repair")));
}
static void LAN_Ready_Actor(SmartGameObj *actor,bool repair=false)
{
    actor->Enable_Hibernation(false);
    VehicleGameObj *crewed=actor->As_VehicleGameObj();if(crewed && crewed->Get_Driver())crewed->Enable_Engine(true);
    SoldierGameObj *soldier=actor->As_SoldierGameObj();
    if(soldier) {
        if(soldier!=COMBAT_STAR)soldier->Set_Control_Owner(SmartGameObj::SERVER_CONTROL_OWNER);
        soldier->Innate_Disable();
        if(!soldier->Get_Vehicle() && soldier->Peek_Model())soldier->Peek_Model()->Set_Hidden(false);
        soldier->Get_Human_State()->Raise_Weapon();
    }
    WeaponBagClass *bag=actor->Get_Weapon_Bag();
    if(bag) {
        // Prefer a real carried combat weapon. Repair beams and C4 are not
        // suitable for the autonomous ranged-attack action.
        int chosen=0;float best=-1.0e30f;
        for(int i=1;i<bag->Get_Count();++i) {
            WeaponClass *w=bag->Peek_Weapon(i);if(!w || !w->Does_Weapon_Exist() || (w->Get_Total_Rounds()==0 && w->Get_Grenade_Rounds()==0))continue;
            const AmmoDefinitionClass *ammo=WeaponManager::Find_Ammo_Definition(w->Get_Definition()->PrimaryAmmoDefID);
            if(!ammo || (int)ammo->AmmoType!=AmmoDefinitionClass::AMMO_TYPE_NORMAL || LAN_Is_Repair_Weapon(w)!=repair)continue;
            if(!repair && (float)ammo->Damage<=0.0f)continue;
            float score=w->Get_Rating()*100.0f+w->Get_Effective_Range()*.01f;
            if(score>best){best=score;chosen=i;}
        }
        if(chosen && bag->Get_Index()!=chosen)bag->Select_Index(chosen);
    }
    WeaponClass *weapon=actor->Get_Weapon();
    if(weapon && weapon->Is_Reload_Needed())weapon->Do_Reload();
}
static bool LAN_Close_Threat_Visible(SmartGameObj *actor,PhysicalGameObj *other)
{
    PhysClass *physical=actor->Peek_Physical_Object();if(!physical || !COMBAT_SCENE)return false;
    Vector3 from=actor->Get_Look_Transform().Get_Translation(),to=other->Get_Bullseye_Position();
    CastResultStruct result;LineSegClass ray(from,to);PhysRayCollisionTestClass test(ray,&result,BULLET_COLLISION_GROUP);
    physical->Inc_Ignore_Counter();COMBAT_SCENE->Cast_Ray(test);physical->Dec_Ignore_Counter();
    return !result.StartBad && (result.Fraction>=1 || test.CollidedPhysObj==other->Peek_Physical_Object());
}
struct LANEnemyCandidate {
    PhysicalGameObj *Object;
    float DistanceSquared;
    LANEnemyCandidate(PhysicalGameObj *object,float distance):Object(object),DistanceSquared(distance){}
};
static bool LAN_Enemy_Nearer(const LANEnemyCandidate &a,const LANEnemyCandidate &b)
{
    return a.DistanceSquared<b.DistanceSquared;
}
static PhysicalGameObj *LAN_Find_Enemy(SmartGameObj *actor,float radius)
{
    Vector3 position;actor->Get_Position(&position);
    const float limit=radius*radius;
    std::vector<LANEnemyCandidate> candidates;candidates.reserve(64);
    for(SLNode<SmartGameObj> *n=GameObjManager::Get_Smart_Game_Obj_List()->Head();n;n=n->Next()) {
        SmartGameObj *other=n->Data();
        if(other==actor || other->Is_Delete_Pending() || !actor->Is_Enemy(other) ||
            !other->Is_Visible() || other->Is_Stealthed() || other->Get_Defense_Object()->Get_Health()<=0)continue;
        SoldierGameObj *s=other->As_SoldierGameObj();if(s && s->Get_Vehicle())continue;
        Vector3 p;other->Get_Position(&p);float d=(p-position).Length2();
        if(d<limit)candidates.push_back(LANEnemyCandidate(other,d));
    }
    // Preserve nearest-visible targeting (including original list-order ties),
    // but stop casting visibility rays once the nearest valid target is found.
    // The former traversal could ray-test many farther enemies before replacing
    // each with a nearer target, for every one of the 63 bots each AI tick.
    std::stable_sort(candidates.begin(),candidates.end(),LAN_Enemy_Nearer);
    for(unsigned i=0;i<candidates.size();++i) {
        PhysicalGameObj *other=candidates[i].Object;
        if(actor->Is_Obj_Visible(other) || (candidates[i].DistanceSquared<225 && LAN_Close_Threat_Visible(actor,other)))return other;
    }
    return NULL;
}
static float LAN_Attack_Range(SmartGameObj *actor)
{
    WeaponClass *w=actor->Get_Weapon();
    return w?WWMath::Clamp(w->Get_Range(),.5f,120.0f):30.0f;
}
static bool LAN_Run_Action(SmartGameObj *actor,const Vector3 &destination,ScriptableGameObj *target,float arrived,int priority,int id,bool repair=false)
{
    VehicleGameObj *ride=actor?actor->As_VehicleGameObj():NULL;if(ride && !ride->Get_Driver())return false;
    LAN_Ready_Actor(actor,repair);
    ActionClass *action=actor->Get_Action();
    ActionParamsStruct a;a.Set_Basic((long)0,priority,id);a.Set_Movement(destination,1,arrived);
    ScriptableGameObj *following=(id==7401)?COMBAT_STAR:((id==7302 || id==7403) && target && !target->As_BuildingGameObj()?target:NULL);
    if(following){a.Set_Movement(following,1,arrived);a.MoveFollow=true;}
    if(target && actor->Get_Weapon()) {
        a.Set_Attack(target,LAN_Attack_Range(actor),0,true);
        a.AttackCheckBlocked=target->As_BuildingGameObj()==NULL;
        if(repair && target->As_PhysicalGameObj() && target->As_PhysicalGameObj()->Peek_Physical_Object()) {
            const Vector3 &extent=target->As_PhysicalGameObj()->Peek_Physical_Object()->Get_Cull_Box().Extent;
            a.AttackRange+=WWMath::Clamp(WWMath::Max(extent.X,extent.Y),1.0f,6.0f);
            // LOS was validated by the escort controller. Account for hull
            // radius here; the actual repair beam still has its native range.
            a.AttackCheckBlocked=false;
        }
    } else target=NULL;
    if(action->Is_Acting() && action->Get_Parameters().ActionID==id &&
        action->Get_Parameters().AttackObjectRef.Get_Ptr()==target) {
        const SafeActionParamsStruct &old=action->Get_Parameters();
        if(old.MoveObjectRef.Get_Ptr()==following && WWMath::Fabs(old.MoveArrivedDistance-arrived)<.1f &&
            (following || (old.MoveLocation-destination).Length2()<4))return true;
        // Goto::Modify is a no-op in this engine. A changed static destination
        // requires a new path. Moving object targets use native path following.
    }
    int old_id=action->Get_Parameters().ActionID;
    if(old_id>=7300 && old_id<=7450)action->Reset(100);
    return target?action->Attack(a):action->Goto(a);
}
static SoldierGameObj *LAN_Create_Soldier(int definition,int team)
{
    DefinitionClass *def=DefinitionMgrClass::Find_Definition(definition);
    if(!def || def->Get_Class_ID()!=CLASSID_GAME_OBJECT_DEF_SOLDIER) return NULL;
    PhysicalGameObj *obj=ObjectLibraryManager::Create_Object(def->Get_Name());
    SoldierGameObj *bot=obj ? obj->As_SoldierGameObj() : NULL;
    if(!bot) {if(obj)obj->Set_Delete_Pending();return NULL;}
    bot->Set_Player_Type(team);
    Matrix3D tm=SpawnManager::Get_Multiplayer_Spawn_Location(team,bot);
    if(!bot->Peek_Human_Phys() || !bot->Peek_Human_Phys()->Can_Teleport(tm,true)) {bot->Set_Delete_Pending();return NULL;}
    bot->Set_Transform(tm);bot->Enable_Hibernation(false);
    if(!bot->Get_Innate_Observer()) {bot->Set_Innate_Observer(new SoldierObserverClass);bot->Add_Observer(bot->Get_Innate_Observer());}
    bot->Innate_Disable();bot->Start_Observers();LAN_Ready_Actor(bot);
    bot->Get_Innate_Observer()->Set_Is_Stationary(false);bot->Get_Innate_Observer()->Set_Aggressiveness(.75f);
    return bot;
}
#include "lanvehicles.inl"

bool cGameDataCnc::Recruit_LAN_Unit(SoldierGameObj *unit)
{
    if(!Is_Offline_Command_Mode() || !COMBAT_STAR || !unit || unit->Get_Player_Type()!=COMBAT_STAR->Get_Player_Type() || unit->Get_Defense_Object()->Get_Health()<=0) return false;
    Vector3 a,b;unit->Get_Position(&a);COMBAT_STAR->Get_Position(&b);
    if((a-b).Length2()>25) return false;
    for(int i=0;i<64;++i) if(LANBots[i].Get_Ptr()==unit) {
        LANBotSquad[i]=!LANBotSquad[i];LANBotOrder[i]=LANBotSquad[i]?1:0;LANBotOrderTime[i]=0;
        unit->Get_Action()->Reset(100);LAN_Log(LANBotSquad[i]?"recruited":"released",i);return true;
    }
    return false;
}
bool cGameDataCnc::Deploy_LAN_Unit(int type,int item,int alternate)
{
    if(!Is_Offline_Command_Mode() || !COMBAT_STAR || !COMBAT_STAR->Get_Player_Data() || item<0 || item>=10 || alternate< -1 || alternate>=3) return false;
    bool vehicle=type==PurchaseSettingsDefClass::TYPE_VEHICLES;
    if(!vehicle && type!=PurchaseSettingsDefClass::TYPE_CLASSES) return false;
    int team=COMBAT_STAR->Get_Player_Type();if(team<0 || team>1)return false;
    BaseControllerClass *base=BaseControllerClass::Find_Base(team);if(!base)return false;
    PurchaseSettingsDefClass *table=PurchaseSettingsDefClass::Find_Definition((PurchaseSettingsDefClass::TYPE)type,(PurchaseSettingsDefClass::TEAM)(1-team));
    if(!table)return false;
    int definition=alternate<0?table->Get_Definition(item):table->Get_Alt_Definition(item,alternate);
    int cost=table->Get_Cost(item)*(base->Is_Base_Powered()?1:2);
    if(!definition || cost<0 || COMBAT_STAR->Get_Player_Data()->Get_Money()<cost)return false;
    int chosen=-1;
    for(int i=team*32;i<team*32+LAN_Team_Bot_Limit(team);++i) {
        ScriptableGameObj *obj=LANBots[i].Get_Ptr();SoldierGameObj *bot=obj?obj->As_SoldierGameObj():NULL;
        if(bot && bot->Get_Defense_Object()->Get_Health()>0 && !bot->Get_Vehicle() && !LANBotVehicle[i].Get_Ptr()) {
            if(chosen<0 || !LANBotSquad[i])chosen=i;
            if(!LANBotSquad[i])break;
        }
    }
    if(chosen<0)return false;
    SoldierGameObj *bot=LANBots[chosen].Get_Ptr()->As_SoldierGameObj();
    if(vehicle) {
        BuildingGameObj *b=base->Find_Building(BuildingConstants::TYPE_VEHICLE_FACTORY);
        VehicleFactoryGameObj *factory=b?b->As_VehicleFactoryGameObj():NULL;
        DefinitionClass *vdef=DefinitionMgrClass::Find_Definition(definition);
        if(!LAN_Vehicle_Allowed(definition) || !base->Can_Generate_Vehicles() || !factory || !factory->Is_Available_For_Purchase())return false;
        if(!factory->Request_Vehicle(definition,5*base->Get_Operation_Time_Factor(),bot))return false;
        COMBAT_STAR->Get_Player_Data()->Purchase_Item(cost);LANBotPendingVehicle[chosen]=definition;
    } else {
        if(cost>0 && !base->Can_Generate_Soldiers())return false;
        SoldierGameObj *replacement=LAN_Create_Soldier(definition,team);if(!replacement)return false;
        if(!COMBAT_STAR->Get_Player_Data()->Purchase_Item(cost)) {replacement->Set_Delete_Pending();return false;}
        bot->Set_Delete_Pending();LANBots[chosen]=replacement;LANBotUpgraded[chosen]=true;LANBotRole[chosen]=1;LANBotGoal[chosen]=NULL;
    }
    LANBotSquad[chosen]=true;LANBotOrder[chosen]=1;LANBotOrderTime[chosen]=0;
    LAN_Log(vehicle?"player vehicle ordered":"player character deployed",chosen,(float)cost);return true;
}
bool cGameDataCnc::Order_LAN_Squad(int command,ScriptableGameObj *target,const Vector3 &position)
{
    if(!Is_Offline_Command_Mode() || !COMBAT_STAR || command<1 || command>6)return false;
    if(command==4 && (!target || !target->As_PhysicalGameObj() || !target->As_PhysicalGameObj()->As_VehicleGameObj()))return false;
    if(command==3 && (!target || (!target->As_DamageableGameObj() || target->As_DamageableGameObj()->Get_Player_Type()==COMBAT_STAR->Get_Player_Type())))return false;
    // In the overhead view, an empty selection must not command an unrelated squad.
    int count=0;bool selected=_LANRTSActive;
    for(int i=0;i<64;++i) if((selected?LANBotSelected[i]:LANBotSquad[i]) && LANBots[i].Get_Ptr() && LANBots[i].Get_Ptr()->As_SoldierGameObj()->Get_Player_Type()==COMBAT_STAR->Get_Player_Type()) {
        LANBotSquad[i]=command!=6;LANBotOrder[i]=command;LANBotOrderTarget[i]=target;LANBotOrderPosition[i]=position;LANBotOrderTime[i]=0;
        if(command==5)LANBots[i].Get_Ptr()->As_SoldierGameObj()->Get_Position(&LANBotOrderPosition[i]);
        if(command==6){LANBotSquad[i]=false;LANBotOrder[i]=0;}
        SoldierGameObj *unit=LANBots[i].Get_Ptr()->As_SoldierGameObj();
        SmartGameObj *actor=(unit->Get_Vehicle() && unit->Get_Vehicle()->Get_Driver()==unit)?static_cast<SmartGameObj*>(unit->Get_Vehicle()):unit;
        if(command==3 && target && target->As_BuildingGameObj()) {
            Vector3 source,end;actor->Get_Position(&source);target->Get_Position(&end);Vector3 toward=source-end;toward.Z=0;
            if(toward.Length2()>1)toward.Normalize();else toward=Vector3(1,0,0);
            LANBotOrderPosition[i]=end+toward*(unit->Get_Vehicle()?28.0f:14.0f);
            if(!unit->Get_Vehicle() && PathfindClass::Get_Instance()) {
                Vector3 reachable;if(PathfindClass::Get_Instance()->Find_Random_Spot(LANBotOrderPosition[i],12,&reachable))LANBotOrderPosition[i]=reachable;
            }
        }
        actor->Get_Action()->Reset(100);LANBotStuckTime[i]=0;
        if(command!=6)Update_LAN_Bot_Order(i,0);++count;
    }
    if(_LANRTSActive && _LANRTSCurrentSelected && COMBAT_STAR) {
        SmartGameObj *actor=COMBAT_STAR;if(COMBAT_STAR->Get_Vehicle())actor=COMBAT_STAR->Get_Vehicle();
        LAN_Ready_Actor(actor);actor->Get_Action()->Reset(100);
        ActionParamsStruct a;a.Set_Basic((long)0,95,7450);
        if(command==2){a.Set_Movement(position,1,2);actor->Get_Action()->Goto(a);++count;}
        if(command==3 && target){a.Set_Movement(target,1,15);a.Set_Attack(target,60,0,true);a.AttackCheckBlocked=target->As_BuildingGameObj()==NULL;actor->Get_Action()->Attack(a);++count;}
        if(command==5){Vector3 p;actor->Get_Position(&p);a.Set_Movement(p,0,2);actor->Get_Action()->Goto(a);++count;}
    }
    LAN_Log("squad order",command,(float)count);return count>0;
}

class LANCommandDialog : public PopupDialogClass {
    GameObjReference Target;Vector3 Point;
public:
    LANCommandDialog(ScriptableGameObj *target,const Vector3 &point):PopupDialogClass(IDD_MESSAGEBOX_OK),Point(point){Target=target;}
    void On_Init_Dialog() {
        while(Get_Control_Count()) {DialogControlClass *c=Get_Control(0);Remove_Control(c);delete c;}
        const RectClass &res=Render2DClass::Get_Screen_Resolution();
        Set_Rect(RectClass(res.Width()*.23f,res.Height()*.12f,res.Width()*.77f,res.Height()*.88f));
        const WCHAR *names[]={L"Recruit / release aimed friendly",L"Squad: follow me",L"Squad: move to aimed area",L"Squad: attack aimed enemy",L"Squad: board aimed vehicle",L"Squad: hold position",L"Squad: dismiss",L"Deploy special character",L"Deploy vehicle + AI driver",L"Close"};
        for(int i=0;i<10;++i) {
            ButtonCtrlClass *b=new ButtonCtrlClass;b->Set_ID(7800+i);b->Set_Text(names[i]);
            b->Set_Window_Rect(RectClass(res.Width()*.26f,res.Height()*(.19f+i*.061f),res.Width()*.74f,res.Height()*(.239f+i*.061f)));Add_Control(b);
        }
        Set_Title(L"Offline squad command - aim before opening (F6)");PopupDialogClass::On_Init_Dialog();
    }
    void On_Command(int id,int message,DWORD param) {
        if(!_LANCommandGame || !_LANCommandGame->Is_Offline_Command_Mode()) {End_Dialog();return;}
        int index=id-7800;if(index<0 || index>9)return;
        if(index==9){End_Dialog();return;}
        if(index==7 || index==8) {
            if(!COMBAT_STAR)return;
            PurchaseSettingsDefClass::TYPE type=index==7?PurchaseSettingsDefClass::TYPE_CLASSES:PurchaseSettingsDefClass::TYPE_VEHICLES;
            int team=COMBAT_STAR->Get_Player_Type();
            PurchaseSettingsDefClass *table=PurchaseSettingsDefClass::Find_Definition(type,(PurchaseSettingsDefClass::TEAM)(1-team));if(!table)return;
            CNCPurchaseMenuClass *dialog=new CNCPurchaseMenuClass;dialog->Set_Definition(table);dialog->Set_Type(type);
            dialog->Set_Team(team==PLAYERTYPE_GDI?PlayerTerminalClass::TYPE_GDI:PlayerTerminalClass::TYPE_NOD);dialog->Set_Deploy_AI(true);
            BaseControllerClass *base=BaseControllerClass::Find_Base(team);if(base)dialog->Set_Cost_Scaling_Factor(base->Is_Base_Powered()?1:2);
            End_Dialog();dialog->Start_Dialog();dialog->Release_Ref();return;
        }
        bool ok=index==0?_LANCommandGame->Recruit_LAN_Unit(Target.Get_Ptr()?Target.Get_Ptr()->As_SoldierGameObj():NULL):_LANCommandGame->Order_LAN_Squad(index,Target.Get_Ptr(),Point);
        if(ok)End_Dialog();else Set_Title(L"Order unavailable: recruit nearby allies / aim at a valid target");
    }
};

#include "lanrts.inl"

void cGameDataCnc::Update_LAN_Command_Input()
{
    bool down=(GetAsyncKeyState(VK_F6)&0x8000)!=0;
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if((_LANRTSRequested || (down && !_LANCommandKey && !DialogMgrClass::Get_Active_Dialog())) && foreground==GetCurrentProcessId() && Is_Offline_Command_Mode() && COMBAT_STAR && !_LANRTSActive) {
        _LANRTSRequested=false;
        if(GameModeManager::Find("Menu")->Is_Active())GameInitMgrClass::Continue_Game();
        DamageableGameObj *aim=HUDInfo::Get_Weapon_Target_Object();
        LANRTSDialog *dialog=new LANRTSDialog;dialog->Start_Dialog();dialog->Release_Ref();
    }
    _LANCommandKey=down;
}

void cGameDataCnc::Buy_LAN_Bot_Upgrade(int index)
{
    int team=index/32;ScriptableGameObj *obj=LANBots[index].Get_Ptr();SoldierGameObj *bot=obj?obj->As_SoldierGameObj():NULL;
    if(!bot || bot->Get_Vehicle() || LAN_Repair_Weapon(bot) || LANBotVehicle[index].Get_Ptr() || LANBotPendingVehicle[index] || LANBotSquad[index] || LANBotUpgraded[index] || LANBotSelected[index])return;
    BaseControllerClass *base=BaseControllerClass::Find_Base(team);if(!base)return;
    bool vehicle=(index%4)==1;
    if(vehicle && !base->Can_Generate_Vehicles())return;
    if(!vehicle && !base->Can_Generate_Soldiers())return;
    PurchaseSettingsDefClass *table=PurchaseSettingsDefClass::Find_Definition(vehicle?PurchaseSettingsDefClass::TYPE_VEHICLES:PurchaseSettingsDefClass::TYPE_CLASSES,(PurchaseSettingsDefClass::TEAM)(1-team));if(!table)return;
    int item=(index%32*3+LANBotLife[index])%10;
    int definition=table->Get_Definition(item),cost=table->Get_Cost(item)*(base->Is_Base_Powered()?1:2);
    if(!definition || cost<=0 || LANBotCredits[index]<cost)return;
    if(vehicle) {
        BuildingGameObj *b=base->Find_Building(BuildingConstants::TYPE_VEHICLE_FACTORY);VehicleFactoryGameObj *factory=b?b->As_VehicleFactoryGameObj():NULL;
        DefinitionClass *def=DefinitionMgrClass::Find_Definition(definition);
        if(!LAN_Vehicle_Allowed(definition) || !factory || !factory->Is_Available_For_Purchase())return;
        if(!factory->Request_Vehicle(definition,5*base->Get_Operation_Time_Factor(),bot))return;
        LANBotPendingVehicle[index]=definition;
    } else {
        if(bot->Get_Definition().Get_ID()==definition)return;
        SoldierGameObj *replacement=LAN_Create_Soldier(definition,team);if(!replacement)return;
        bot->Set_Delete_Pending();LANBots[index]=replacement;LANBotGoal[index]=NULL;LANBotRouteActive[index]=false;
    }
    LANBotUpgraded[index]=true;LANBotCredits[index]-=cost;LAN_Log(vehicle?"bot vehicle purchased":"bot special purchased",index,(float)cost);
}

bool cGameDataCnc::Update_LAN_Bot_Order(int index,float elapsed)
{
    ScriptableGameObj *object=LANBots[index].Get_Ptr();
    SoldierGameObj *bot=object?object->As_SoldierGameObj():NULL;if(!bot)return false;
    if(!LANBotSquad[index] && Update_LAN_Repair(index,elapsed))return true;
    LAN_Ready_Actor(bot);
    if(Update_LAN_Boarding(index,elapsed))return true;
    Vector3 position;bot->Get_Position(&position);
    VehicleGameObj *v=bot->Get_Vehicle();
    if(v && v->Get_Driver()!=bot)return true; // Passengers never issue steering actions.
    if(!LANBotSquad[index] || !COMBAT_STAR)return false;
    SmartGameObj *actor=v?static_cast<SmartGameObj*>(v):bot;
    actor->Get_Position(&position);LAN_Ready_Actor(actor);
    int command=LANBotOrder[index];Vector3 destination=LANBotOrderPosition[index];float arrived=v?6.0f:2.0f;
    ScriptableGameObj *attack=NULL;
    if(command==1) {
        COMBAT_STAR->Get_Position(&destination);destination+=Vector3((index%3-1)*3.0f,(index%2)*3.0f,0);arrived=v?12.0f:4.0f;
    } else if(command==2)destination+=Vector3((index%3-1)*2.0f,(index%2)*2.0f,0);
    else if(command==3) {
        attack=LANBotOrderTarget[index].Get_Ptr();DamageableGameObj *d=attack?attack->As_DamageableGameObj():NULL;
        if(!attack || attack->Is_Delete_Pending() || !d || d->Get_Defense_Object()->Get_Health()<=0) {
            LANBotOrder[index]=5;LANBotOrderPosition[index]=position;destination=position;attack=NULL;
        } else {if(attack->As_BuildingGameObj())destination=LANBotOrderPosition[index];else attack->Get_Position(&destination);arrived=attack->As_BuildingGameObj()?(v?8.0f:4.0f):WWMath::Clamp(LAN_Attack_Range(actor)*.4f,5.0f,v?30.0f:18.0f);}
    }
    // Follow, move, and hold fire at nearby visible threats without losing their destination.
    if(!attack)attack=LAN_Find_Enemy(actor,LAN_Attack_Range(actor));
    if(command==1 && (destination-position).Length2()<arrived*arrived)destination=position;
    bool accepted=LAN_Run_Action(actor,destination,attack,arrived,95,7400+LANBotOrder[index]);
    if(!accepted)LAN_Log("order action rejected",index,(float)actor->Get_Action()->Get_Parameters().Priority);
    return true;
}
