// Developer-only offline battle for fixed-camera terrain regression captures.
// Uses its own server configuration; campaign progression is not modified.
static bool Start_Terrain_Audit_Battle(const char *map)
{
    if(!map || stricmp(map,"C&C_Under.mix"))return false;
    if(cNicEnum::Get_Num_Nics()==0 || !GameModeManager::Find("LAN"))return false;
    cGameSpyAdmin::Reset();GameInitMgrClass::Initialize_LAN();
    if(PTheGameData){delete PTheGameData;PTheGameData=NULL;}
    PTheGameData=cGameData::Create_Game_Of_Type(cGameData::GAME_TYPE_CNC);
    cGameDataCnc *game=PTheGameData?PTheGameData->As_Cnc():NULL;
    if(!game)return false;
    game->Set_Ini_Filename("terrain_audit_session.ini");
    game->Load_From_Server_Config();game->Set_QuickMatch_Server(false);
    game->Set_LAN_Bots_Enabled(true);game->Set_LAN_Bots_Per_Team(32);
    game->Set_Game_Title(L"Terrain development audit");
    if(cNetInterface::Get_Nickname().Is_Empty()){WideStringClass nickname(L"TerrainAudit");cNetInterface::Set_Nickname(nickname);}
    game->IsDedicated.Set(false);game->IsLaddered.Set(false);game->IsPassworded.Set(false);
    game->RemixTeams.Set(false);game->IsTeamChangingAllowed.Set(false);
    game->Set_Do_Maps_Loop(false);game->IsAutoRestart.Set(false);game->Set_Max_Players(1);
    game->Set_Time_Limit_Minutes(0);game->BaseDestructionEndsGame.Set(false);
    game->BeaconPlacementEndsGame.Set(false);game->Set_Starting_Credits(500);
    game->Clear_Map_Cycle();game->Set_Map_Cycle(0,map);game->Set_Map_Name(map);game->Set_Is_Map_Cycle_Over(false);
    game->Save_To_Server_Config();cNetInterface::Set_Side_Preference(PLAYERTYPE_GDI);
    CampaignManager::Select_Backdrop_Number_By_MP_Type(game->Get_Game_Type());
    GameInitMgrClass::Set_Is_Client_Required(true);GameInitMgrClass::Set_Is_Server_Required(true);
    GameInitMgrClass::Start_Game(map,PLAYERTYPE_GDI,0);
    return true;
}
