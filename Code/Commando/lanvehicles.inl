// Offline crew and field-repair controller. Included only by lancommand.inl.
static WeaponClass *LAN_Repair_Weapon(SoldierGameObj *soldier)
{
    WeaponBagClass *bag=soldier?soldier->Get_Weapon_Bag():NULL;if(!bag)return NULL;
    for(int i=1;i<bag->Get_Count();++i) {
        WeaponClass *weapon=bag->Peek_Weapon(i);
        if(!weapon || !weapon->Does_Weapon_Exist() || weapon->Get_Total_Rounds()==0)continue;
        const AmmoDefinitionClass *ammo=WeaponManager::Find_Ammo_Definition(weapon->Get_Definition()->PrimaryAmmoDefID);
        if(ammo && LAN_Is_Repair_Weapon(weapon) && (int)ammo->AmmoType==AmmoDefinitionClass::AMMO_TYPE_NORMAL)return weapon;
    }
    return NULL;
}
static bool LAN_Is_Harvester(VehicleGameObj *vehicle)
{
    if(!vehicle)return false;
    for(int team=0;team<2;++team) {
        BaseControllerClass *base=BaseControllerClass::Find_Base(team);
        if(base && base->Get_Harvester_Vehicle()==vehicle)return true;
    }
    const char *name=vehicle->Get_Definition().Get_Name();
    return name && (strstr(name,"Harvester") || strstr(name,"harvester"));
}
static bool LAN_In_Factory(VehicleGameObj *vehicle)
{
    for(int team=0;team<2;++team) {
        BaseControllerClass *base=BaseControllerClass::Find_Base(team);
        BuildingGameObj *building=base?base->Find_Building(BuildingConstants::TYPE_VEHICLE_FACTORY):NULL;
        VehicleFactoryGameObj *factory=building?building->As_VehicleFactoryGameObj():NULL;
        if(factory && factory->Is_Generating_Object(vehicle))return true;
    }
    return false;
}
static bool LAN_Vehicle_Available(VehicleGameObj *vehicle,SoldierGameObj *soldier,bool explicit_order=false)
{
    if(!vehicle || !soldier || vehicle->Is_Delete_Pending() || vehicle->Is_Turret() ||
        vehicle->Get_Defense_Object()->Get_Health()<=0 || vehicle->Get_Free_Seat()<0 || LAN_Is_Harvester(vehicle) || LAN_In_Factory(vehicle))return false;
    int team=vehicle->Get_Player_Type();
    if(team!=soldier->Get_Player_Type() && team!=PLAYERTYPE_NEUTRAL)return false;
    ScriptableGameObj *lock=vehicle->Get_Lock_Owner();
    DamageableGameObj *owner=lock?lock->As_DamageableGameObj():NULL;
    if(owner && owner->Get_Player_Type()!=soldier->Get_Player_Type())return false;
    SoldierGameObj *buyer=lock?lock->As_SoldierGameObj():NULL;
    // Protect a human's empty purchase; AI reservations may be crewed by any
    // nearby teammate so a distant or dead purchaser cannot strand the tank.
    if(buyer && (buyer==COMBAT_STAR || buyer->Is_Human_Controlled()) && !vehicle->Get_Driver() && !explicit_order)return false;
    return true;
}
static float LAN_Vehicle_Radius(VehicleGameObj *vehicle)
{
    if(!vehicle->Peek_Physical_Object())return 2;
    const Vector3 &extent=vehicle->Peek_Physical_Object()->Get_Cull_Box().Extent;
    return WWMath::Clamp(WWMath::Max(extent.X,extent.Y),1.0f,6.0f);
}
static void LAN_Crew_Log(const char *event,int slot,SoldierGameObj *soldier,VehicleGameObj *vehicle,int seat=-1)
{
    FILE *file=fopen("offline_bots.log","a");if(!file)return;
    fprintf(file,"%s slot %d actor %d vehicle %d seat %d\n",event,slot,soldier?soldier->Get_ID():0,vehicle?vehicle->Get_ID():0,seat);fclose(file);
}
int cGameDataCnc::LAN_Team_Bot_Limit(int team) const
{
    if(!Is_Offline_Command_Mode())return LANBotsPerTeam;
    int humans=cPlayerManager::Tally_Team_Size(team);
    if(COMBAT_STAR && COMBAT_STAR->Get_Player_Type()==team)humans=1;
    int count=32-humans;return count<0?0:(count>32?32:count);
}
bool cGameDataCnc::Update_LAN_Repair(int index,float elapsed)
{
    ScriptableGameObj *object=LANBots[index].Get_Ptr();SoldierGameObj *soldier=object?object->As_SoldierGameObj():NULL;
    WeaponClass *tool=LAN_Repair_Weapon(soldier);if(!tool)return false;
    if(soldier->Get_Vehicle() && soldier->Get_Vehicle()->Get_Driver()==soldier)return false;
    Vector3 position;soldier->Get_Position(&position);
    VehicleGameObj *best=NULL;float best_score=1.0e30f;
    for(SLNode<SmartGameObj> *node=GameObjManager::Get_Smart_Game_Obj_List()->Head();node;node=node->Next()) {
        VehicleGameObj *vehicle=node->Data()->As_VehicleGameObj();
        if(!vehicle || vehicle->Is_Delete_Pending() || vehicle->Get_Player_Type()!=soldier->Get_Player_Type() ||
            vehicle->Is_Turret() || vehicle->Is_Aircraft() || LAN_Is_Harvester(vehicle) || LAN_In_Factory(vehicle))continue;
        DefenseObjectClass *defense=vehicle->Get_Defense_Object();if(defense->Get_Health()<=0)continue;
        bool damaged=defense->Get_Health()+.5f<defense->Get_Health_Max() || defense->Get_Shield_Strength()+.5f<defense->Get_Shield_Strength_Max();
        if(!vehicle->Get_Driver() && !damaged)continue;
        // Riding crews remain aboard until there is a repair job.
        if(soldier->Get_Vehicle() && !damaged)continue;
        Vector3 p;vehicle->Get_Position(&p);float distance=(p-position).Length();if(distance>120)continue;
        int assigned=0;for(int i=0;i<64;++i)if(i!=index && LANBotRepairTarget[i].Get_Ptr()==vehicle && LANBots[i].Get_Ptr())++assigned;
        float score=distance+(damaged?-65.0f:0)+(assigned*24.0f);
        if(LANBotRepairTarget[index].Get_Ptr()==vehicle)score-=12;
        if(vehicle->Get_Vehicle_Type()==VEHICLE_TYPE_TANK)score-=8;
        if(score<best_score){best_score=score;best=vehicle;}
    }
    if(!best){LANBotRepairTarget[index]=NULL;return false;}
    if(LANBotRepairTarget[index].Get_Ptr()!=best)LAN_Crew_Log("repair escort assigned",index,soldier,best);
    LANBotRepairTarget[index]=best;LANBotVehicle[index]=NULL;
    if(soldier->Get_Vehicle()) {
        VehicleGameObj *ride=soldier->Get_Vehicle();int seat=ride->Find_Seat(soldier);Vector3 center;ride->Get_Position(&center);
        ride->Remove_Occupant(soldier);soldier->Exit_Destroyed_Vehicle(seat,center);
        if(!ride->Get_Driver())ride->Get_Action()->Reset(100);
        LAN_Crew_Log("repair crew disembarked",index,soldier,ride,seat);
    }
    DefenseObjectClass *defense=best->Get_Defense_Object();
    bool damaged=defense->Get_Health()+.5f<defense->Get_Health_Max() || defense->Get_Shield_Strength()+.5f<defense->Get_Shield_Strength_Max();
    Vector3 p;best->Get_Position(&p);float hull=LAN_Vehicle_Radius(best);
    // Follow behind the hull rather than pathfinding inside the vehicle's collision.
    Vector3 away=position-p;away.Z=0;if(away.Length2()<1)away=-best->Get_Transform().Get_X_Vector();away.Normalize();
    Vector3 destination=p+away*(hull+(damaged?1.0f:4.0f));destination.Z=position.Z;
    bool visible=damaged && LAN_Close_Threat_Visible(soldier,best);
    bool accepted=LAN_Run_Action(soldier,destination,visible?best:NULL,damaged?1.0f:2.0f,90,visible?7311:7310,true);
    if(!accepted)LAN_Crew_Log("repair action rejected",index,soldier,best);
    return true;
}
bool cGameDataCnc::Update_LAN_Boarding(int index,float elapsed)
{
    SoldierGameObj *soldier=LANBots[index].Get_Ptr()->As_SoldierGameObj();
    if(soldier->Get_Vehicle()) {LANBotPendingVehicle[index]=0;return soldier->Get_Vehicle()->Get_Driver()!=soldier;}
    Vector3 position;soldier->Get_Position(&position);
    LANBotAvoidTime[index]=WWMath::Max(0.0f,LANBotAvoidTime[index]-elapsed);
    bool explicit_order=LANBotSquad[index] && LANBotOrder[index]==4;
    ScriptableGameObj *ref=explicit_order?LANBotOrderTarget[index].Get_Ptr():LANBotVehicle[index].Get_Ptr();
    PhysicalGameObj *physical=ref?ref->As_PhysicalGameObj():NULL;
    VehicleGameObj *vehicle=physical?physical->As_VehicleGameObj():NULL;
    if(LANBotPendingVehicle[index]) {
        for(SLNode<SmartGameObj> *node=GameObjManager::Get_Smart_Game_Obj_List()->Head();node;node=node->Next()) {
            VehicleGameObj *produced=node->Data()->As_VehicleGameObj();
            if(produced && produced->Get_Lock_Owner()==soldier && LAN_Vehicle_Available(produced,soldier)) {vehicle=produced;LANBotPendingVehicle[index]=0;break;}
        }
        if(LANBotPendingVehicle[index]) {
            BaseControllerClass *base=BaseControllerClass::Find_Base(index/32);BuildingGameObj *building=base?base->Find_Building(BuildingConstants::TYPE_VEHICLE_FACTORY):NULL;
            VehicleFactoryGameObj *factory=building?building->As_VehicleFactoryGameObj():NULL;
            if(!factory || !factory->Is_Busy())LANBotPendingVehicle[index]=0;
        }
    }
    if(vehicle && !LAN_Vehicle_Available(vehicle,soldier,explicit_order))vehicle=NULL;
    // Explicit move/attack/hold commands take precedence over automatic boarding.
    if(!vehicle && !explicit_order && !LANBotSquad[index]) {
        float closest=120.0f*120.0f;
        for(SLNode<SmartGameObj> *node=GameObjManager::Get_Smart_Game_Obj_List()->Head();node;node=node->Next()) {
            VehicleGameObj *candidate=node->Data()->As_VehicleGameObj();if(!LAN_Vehicle_Available(candidate,soldier))continue;
            if(LANBotAvoidTime[index]>0 && LANBotAvoidVehicle[index].Get_Ptr()==candidate)continue;
            int assigned=0;for(int i=0;i<64;++i) {
                ScriptableGameObj *o=LANBots[i].Get_Ptr();SoldierGameObj *other=o?o->As_SoldierGameObj():NULL;
                if(i!=index && other && !other->Get_Vehicle() && LANBotVehicle[i].Get_Ptr()==candidate)++assigned;
            }
            if(assigned>=candidate->Get_Seat_Count()-candidate->Get_Occupant_Count())continue;
            Vector3 p;candidate->Get_Position(&p);if(candidate->Is_Aircraft() && fabsf(p.Z-position.Z)>4)continue;
            float distance=(p-position).Length2();if(!candidate->Get_Driver())distance*=.5f;if(distance<closest){closest=distance;vehicle=candidate;}
        }
    }
    if(!vehicle) {
        LANBotVehicle[index]=NULL;LANBotBoardAge[index]=0;
        if(explicit_order){LANBotOrder[index]=5;LANBotOrderPosition[index]=position;}
        return false;
    }
    bool changed=LANBotVehicle[index].Get_Ptr()!=vehicle;
    if(changed){LANBotVehicle[index]=vehicle;LANBotBoardAge[index]=0;LANBotBoardDistance[index]=1.0e30f;LAN_Crew_Log("nearest vehicle assigned",index,soldier,vehicle);}
    Vector3 p;vehicle->Get_Position(&p);float distance=(p-position).Length();float hull=LAN_Vehicle_Radius(vehicle);
    if(distance<hull+1.8f && fabsf(p.Z-position.Z)<4) {
        int seat=vehicle->Get_Free_Seat();
        if(seat>=0) {
            soldier->Get_Action()->Reset(100);vehicle->Add_Occupant(soldier,seat);
            if(soldier->Get_Vehicle()==vehicle) {
                LANBotPendingVehicle[index]=0;LANBotGoal[index]=NULL;LANBotRouteActive[index]=false;
                if(vehicle->Get_Driver()==soldier){if(!vehicle->Get_Driver_Is_Gunner())vehicle->Toggle_Driver_Is_Gunner();vehicle->Set_Control_Owner(SmartGameObj::SERVER_CONTROL_OWNER);vehicle->Get_Action()->Reset(100);}
                if(explicit_order)LANBotOrder[index]=1;
                LAN_Crew_Log("AI boarded vehicle",index,soldier,vehicle,seat);
                return vehicle->Get_Driver()!=soldier;
            }
        }
    }
    if(distance<LANBotBoardDistance[index]-.5f){LANBotBoardAge[index]=0;LANBotBoardDistance[index]=distance;}
    else LANBotBoardAge[index]+=elapsed;
    if(LANBotBoardAge[index]>12) {
        LANBotAvoidVehicle[index]=vehicle;LANBotAvoidTime[index]=25;LANBotVehicle[index]=NULL;LANBotBoardAge[index]=0;
        soldier->Get_Action()->Reset(100);LAN_Crew_Log("boarding route stalled; reassigned",index,soldier,vehicle);
        if(explicit_order){LANBotOrder[index]=5;LANBotOrderPosition[index]=position;}
        return false;
    }
    if(changed || (p-LANBotBoardVehiclePos[index]).Length2()>9) {
        LANBotBoardVehiclePos[index]=p;Vector3 away=position-p;away.Z=0;
        if(away.Length2()<1)away=vehicle->Get_Transform().Get_X_Vector();away.Normalize();
        LANBotBoardPoint[index]=p+away*(hull+1.0f);LANBotBoardPoint[index].Z=position.Z;
        if(PathfindClass::Get_Instance()) {
            Vector3 reachable;if(PathfindClass::Get_Instance()->Find_Random_Spot(LANBotBoardPoint[index],3,&reachable))LANBotBoardPoint[index]=reachable;
        }
    }
    LAN_Run_Action(soldier,LANBotBoardPoint[index],NULL,.75f,95,7400);
    return true;
}
void cGameDataCnc::Log_LAN_State()
{
    FILE *file=fopen("offline_bots.log","a");if(!file)return;
    int alive[2]={0,0},riding[2]={0,0},drivers[2]={0,0},acting[2]={0,0};
    for(int i=0;i<64;++i) {
        ScriptableGameObj *o=LANBots[i].Get_Ptr();SoldierGameObj *s=o?o->As_SoldierGameObj():NULL;
        if(!s || s->Get_Defense_Object()->Get_Health()<=0)continue;
        int team=i/32;++alive[team];VehicleGameObj *v=s->Get_Vehicle();if(v)++riding[team];
        SmartGameObj *actor=s;if(v && v->Get_Driver()==s){actor=v;++drivers[team];}
        ActionClass *a=actor->Get_Action();if(a->Is_Acting())++acting[team];
        Vector3 p;actor->Get_Position(&p);
        fprintf(file,"STATE bot %d id %d class %s vehicle %d action %d priority %d owner %d pos %.1f %.1f %.1f weapon %s pending %d repair %d\n",i,s->Get_ID(),s->Get_Definition().Get_Name(),v?v->Get_ID():0,a->Get_Parameters().ActionID,a->Get_Parameters().Priority,actor->Get_Control_Owner(),p.X,p.Y,p.Z,actor->Get_Weapon()?actor->Get_Weapon()->Get_Name():"none",LANBotPendingVehicle[i],LANBotRepairTarget[i].Get_Ptr()?LANBotRepairTarget[i].Get_Ptr()->Get_ID():0);
    }
    fprintf(file,"POPULATION Nod %d/%d GDI %d/%d total AI %d riding %d/%d drivers %d/%d acting %d/%d RTS %d fullworld %d\n",alive[0],LAN_Team_Bot_Limit(0),alive[1],LAN_Team_Bot_Limit(1),alive[0]+alive[1],riding[0],riding[1],drivers[0],drivers[1],acting[0],acting[1],_LANRTSActive,COMBAT_SCENE?COMBAT_SCENE->Is_Full_Visibility():0);
    for(SLNode<SmartGameObj> *node=GameObjManager::Get_Smart_Game_Obj_List()->Head();node;node=node->Next()) {
        VehicleGameObj *v=node->Data()->As_VehicleGameObj();if(!v || v->Is_Delete_Pending())continue;
        fprintf(file,"VEHICLE id %d class %s team %d seats %d used %d driver %d lock %d factory %d harvester %d\n",v->Get_ID(),v->Get_Definition().Get_Name(),v->Get_Player_Type(),v->Get_Seat_Count(),v->Get_Occupant_Count(),v->Get_Driver()?v->Get_Driver()->Get_ID():0,v->Get_Lock_Owner()?v->Get_Lock_Owner()->Get_ID():0,LAN_In_Factory(v),LAN_Is_Harvester(v));
    }
    fclose(file);
}
