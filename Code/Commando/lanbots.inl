// Server-owned infantry for LAN C&C matches. Included only by gdcnc.cpp.
#include "spawn.h"
#include "soldier.h"
#include "soldierobserver.h"
#include "objlibrary.h"
#include "gameobjmanager.h"
#include "building.h"
#include "timemgr.h"
#include "phys3.h"
#include "humanphys.h"
#include "goremanager.h"
#include "playermanager.h"
#include "lancommand.inl"

void cGameDataCnc::Clear_LAN_Bots()
{
    Command_Music_Stop(this);
    for (int i=0; i<64; ++i) {
        ScriptableGameObj *obj=LANBots[i].Get_Ptr();
        if (obj && cNetwork::I_Am_Server()) obj->Set_Delete_Pending();
        LANBots[i]=NULL;
        LANBotBoardAge[i]=0;LANBotBoardDistance[i]=1.0e30f;LANBotAvoidTime[i]=0;
        LANBotRepairTarget[i]=NULL;LANBotAvoidVehicle[i]=NULL;
        LANBotRespawn[i]=0;LANBotCredits[i]=-1;LANBotNextBuy[i]=5+i%7;
        LANBotOrder[i]=0;LANBotOrderTime[i]=0;LANBotStuckTime[i]=0;LANBotRole[i]=i%5;LANBotLife[i]=0;
        LANBotUpgraded[i]=false;LANBotSelected[i]=false;LANBotSquad[i]=false;LANBotRouteActive[i]=false;LANBotPendingVehicle[i]=0;
        LANBotOrderTarget[i]=NULL;LANBotVehicle[i]=NULL;LANBotGoal[i]=NULL;
    }
    LANBotThinkTimer=0;LANBotAuditTimer=0;
    CombatManager::Set_Command_Mode(false);CombatManager::Set_Command_View(false);
    GoreManager::Set_Offline_Bot_Mode(false);
    if(_LANCommandGame==this) {_LANRTSRequested=false;_LANRTSCurrentSelected=false;_LANCommandKey=false;_LANCommandGame=NULL;BaseControllerClass::Set_Funds_Observer(NULL);SoldierGameObj::Set_Recruit_Handler(NULL);}
}

void cGameDataCnc::Think_LAN_Bots()
{
    GoreManager::Set_Offline_Bot_Mode(LANBotsEnabled && cNetwork::I_Am_Server() && GameInitMgrClass::Is_LAN_Initialized() && cPlayerManager::Count()<=1);
    if (!cNetwork::I_Am_Server() || !GameInitMgrClass::Is_LAN_Initialized()) return;
    if (!LANBotsEnabled) { Clear_LAN_Bots(); return; }
    if (IsIntermission.Is_True() || !Is_Gameplay_Permitted()) return;
    if(Is_Offline_Command_Mode()) {
        if(_LANCommandGame!=this)LAN_Log("command recovery 2026-10-23: target 63 bots; complete world collection",-1);
        _LANCommandGame=this;CombatManager::Set_Command_Mode(true);BaseControllerClass::Set_Funds_Observer(&LAN_Funds);SoldierGameObj::Set_Recruit_Handler(&LAN_Poke);
        for(int i=0;i<64;++i)if(LANBotCredits[i]<0)LANBotCredits[i]=(float)StartingCredits;
        Update_LAN_Command_Input();
    } else if(_LANCommandGame==this) {_LANRTSRequested=false;_LANRTSCurrentSelected=false;_LANCommandKey=false;_LANCommandGame=NULL;BaseControllerClass::Set_Funds_Observer(NULL);SoldierGameObj::Set_Recruit_Handler(NULL);}

    LANBotThinkTimer+=TimeManager::Get_Frame_Seconds();
    if (LANBotThinkTimer<.25f) return;
    float elapsed=LANBotThinkTimer;
    LANBotThinkTimer=0;LANBotAuditTimer+=elapsed;
    if(Is_Offline_Command_Mode())for(SLNode<SmartGameObj> *n=GameObjManager::Get_Smart_Game_Obj_List()->Head();n;n=n->Next()){
        VehicleGameObj *v=n->Data()->As_VehicleGameObj();if(v && !v->Is_Delete_Pending() && v->Reconcile_Occupants())LAN_Log("crew seat links repaired",v->Get_ID());
    }
    if(LANBotAuditTimer>=10){LANBotAuditTimer=0;Log_LAN_State();}
    for (int team=0; team<2; ++team) {
        int spawned=0;
        for (int slot=0; slot<32; ++slot) {
            int index=team*32+slot;
            ScriptableGameObj *obj=LANBots[index].Get_Ptr();
            SoldierGameObj *bot=obj ? obj->As_SoldierGameObj() : NULL;
            if (slot>=LAN_Team_Bot_Limit(team)) {
                if (bot) bot->Set_Delete_Pending();
                LANBots[index]=NULL;
                continue;
            }
            if (bot && (bot->Is_Delete_Pending() || bot->Get_Defense_Object()->Get_Health()<=0)) {
                // Leave the corpse to the game's normal corpse lifecycle.
                LANBots[index]=NULL;
                bot=NULL;
                LANBotRepairTarget[index]=NULL;LANBotAvoidVehicle[index]=NULL;LANBotBoardAge[index]=0;LANBotBoardDistance[index]=1.0e30f;LANBotAvoidTime[index]=0;LANBotUpgraded[index]=false;LANBotOrder[index]=0;LANBotSquad[index]=false;LANBotSelected[index]=false;LANBotRespawn[index]=5.0f;LANBotGoal[index]=NULL;LANBotRouteActive[index]=false;LANBotVehicle[index]=NULL;LANBotPendingVehicle[index]=0;++LANBotLife[index];
            }
            if (!bot) {
                LANBotRespawn[index]-=elapsed;
                if (LANBotRespawn[index]>0 || spawned>=2) continue;
                // Refuse maps with no team spawn rather than creating actors at origin.
                bool has_spawn=false;
                DynamicVectorClass<SpawnerClass*> spawners=SpawnManager::Get_Spawner_List();
                for (int j=0;j<spawners.Count();++j) {
                    const SpawnerDefClass &def=spawners[j]->Get_Definition();
                    if (def.Is_Soldier_Startup() && !def.Is_Primary() && def.Get_Player_Type()==team) has_spawn=true;
                }
                if (!has_spawn) continue;
                // Enlisted slot 3 is the team's native engineer. No custom
                // presets or repair stats are invented for the support role.
                TeamPurchaseSettingsDefClass *enlisted=TeamPurchaseSettingsDefClass::Get_Definition((TeamPurchaseSettingsDefClass::TEAM)(1-team));
                DefinitionClass *spawn_def=enlisted?DefinitionMgrClass::Find_Definition(enlisted->Get_Enlisted_Definition(slot%8==0?3:0)):NULL;
                const char *spawn_name=spawn_def?spawn_def->Get_Name():(team==PLAYERTYPE_NOD?"CnC_Nod_Minigunner_0":"CnC_GDI_MiniGunner_0");
                PhysicalGameObj *created=ObjectLibraryManager::Create_Object(spawn_name);
                bot=created ? created->As_SoldierGameObj() : NULL;
                if (!bot) { if(created) created->Set_Delete_Pending(); continue; }
                Matrix3D tm=SpawnManager::Get_Multiplayer_Spawn_Location(team,bot);
                Phys3Class *phys=bot->Peek_Human_Phys();
                if (!phys || !phys->Can_Teleport(tm,true)) { bot->Set_Delete_Pending(); continue; }
                bot->Set_Transform(tm);
                bot->Set_Player_Type(team);
                LAN_Ready_Actor(bot);
                if (!bot->Get_Innate_Observer()) {
                    bot->Set_Innate_Observer(new SoldierObserverClass);
                    bot->Add_Observer(bot->Get_Innate_Observer());
                }
                bot->Innate_Disable();
                bot->Start_Observers();LAN_Ready_Actor(bot);
                LANBots[index]=bot;
                LANBotUpgraded[index]=false;LANBotRespawn[index]=5.0f;bot->Get_Position(&LANBotLastPosition[index]);
                bot->Get_Innate_Observer()->Set_Is_Stationary(false);
                bot->Get_Innate_Observer()->Set_Aggressiveness(0.75f);
                ++spawned;
                WWDEBUG_SAY(("LAN bots: spawned team %d slot %d\n",team,slot));
            }
            bot->Enable_Hibernation(false);
            if(Is_Offline_Command_Mode()) {
                LANBotNextBuy[index]-=elapsed;
                if(LANBotNextBuy[index]<=0){LANBotNextBuy[index]=8+(slot%7);Buy_LAN_Bot_Upgrade(index);bot=LANBots[index].Get_Ptr()->As_SoldierGameObj();}
                if(Update_LAN_Bot_Order(index,elapsed))continue;
            }
            SmartGameObj *actor=bot;VehicleGameObj *vehicle=bot->Get_Vehicle();
            if(vehicle && vehicle->Get_Driver()==bot){actor=vehicle;actor->Enable_Hibernation(false);}
            Vector3 position;actor->Get_Position(&position);
            float moved=(position-LANBotLastPosition[index]).Length2();LANBotLastPosition[index]=position;
            LANBotStuckTime[index]=moved<.25f?LANBotStuckTime[index]+elapsed:0;
            bool replan=LANBotStuckTime[index]>14;
            if(replan){actor->Get_Action()->Reset(100);LANBotRouteActive[index]=false;LANBotGoal[index]=NULL;LANBotStuckTime[index]=0;++LANBotLife[index];}
            // Explicit squad orders are handled above. Autonomous decisions are refreshed
            // independently of the native campaign observer's sound/search priorities.
            LAN_Ready_Actor(actor);
            PhysicalGameObj *enemy=LAN_Find_Enemy(actor,LAN_Attack_Range(actor));
            if(enemy) {
                Vector3 p;enemy->Get_Position(&p);
                LAN_Run_Action(actor,p,enemy,WWMath::Clamp(LAN_Attack_Range(actor)*.4f,5.0f,vehicle?30.0f:18.0f),85,7302);
                continue;
            }
            ScriptableGameObj *goal=LANBotGoal[index].Get_Ptr();
            BuildingGameObj *target=goal?goal->As_BuildingGameObj():NULL;
            if(!target || target->Is_Destroyed()) {
                std::vector<BuildingGameObj*> choices;
                bool defend=(slot%5)==0;
                for(SLNode<BuildingGameObj> *n=GameObjManager::Get_Building_Game_Obj_List()->Head();n;n=n->Next()) {
                    BuildingGameObj *b=n->Data();if(!b->Is_Destroyed() && b->Get_Player_Type()==(defend?team:1-team))choices.push_back(b);
                }
                if(!choices.empty())target=choices[(slot/2+LANBotLife[index])%choices.size()];else target=NULL;
                LANBotGoal[index]=target;LANBotRouteActive[index]=false;
                if(target) {
                    Vector3 p;target->Get_Position(&p);Vector3 toward=position-p;toward.Z=0;
                    if(toward.Length2()>1)toward.Normalize();else toward=Vector3(1,0,0);
                    LANBotOrderPosition[index]=p+toward*(vehicle?28.0f:14.0f);
                    if(!vehicle && PathfindClass::Get_Instance()) {
                        Vector3 reachable;if(PathfindClass::Get_Instance()->Find_Random_Spot(LANBotOrderPosition[index],12,&reachable))LANBotOrderPosition[index]=reachable;
                    }
                }
                if(target && !defend && !vehicle && PathfindClass::Get_Instance()) {
                    Vector3 end;target->Get_Position(&end);Vector3 delta=end-position;delta.Z=0;
                    if(delta.Length2()>900) {
                        delta.Normalize();Vector3 side(-delta.Y,delta.X,0);
                        int lane=(slot+LANBotLife[index])%3-1;
                        Vector3 center=(position+end)*.5f+side*(lane*40.0f);
                        LANBotRouteActive[index]=PathfindClass::Get_Instance()->Find_Random_Spot(center,18,&LANBotRoute[index]);
                        if(LANBotRouteActive[index])LAN_Log("route lane",index,(float)lane);
                    }
                }
            }
            if(LANBotRouteActive[index]) {
                if((position-LANBotRoute[index]).Length2()>36) {LAN_Run_Action(actor,LANBotRoute[index],NULL,4,80,7301);continue;}
                LANBotRouteActive[index]=false;
            }
            if(target) {
                Vector3 p;target->Get_Position(&p);
                if((slot%5)==0)LAN_Run_Action(actor,p,NULL,18,80,7301);
                else {
                    // Factory origins are commonly inside walls. Approach a reachable
                    // exterior point rather than requiring a path into the building.
                    Vector3 approach=LANBotOrderPosition[index];
                    LAN_Run_Action(actor,approach,target,vehicle?8.0f:4.0f,80,7301);
                }
            } else {
                Matrix3D enemy_spawn=SpawnManager::Get_Multiplayer_Spawn_Location(1-team,bot);
                LAN_Run_Action(actor,enemy_spawn.Get_Translation(),NULL,8,80,7301);
            }
        }
    }
}
