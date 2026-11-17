#include "commandcampaign.h"
#include "popupdialog.h"
#include "buttonctrl.h"
#include "dlgmplanhostoptions.h"
#include "netinterface.h"
#include "gamemode.h"
#include "assets.h"

class CommandModeHub : public PopupDialogClass {
public:
    CommandModeHub():PopupDialogClass(IDD_MESSAGEBOX_OK){}
    void On_Init_Dialog() {
        while(Get_Control_Count()){DialogControlClass *c=Get_Control(0);Remove_Control(c);delete c;}
        const RectClass &res=Render2DClass::Get_Screen_Resolution();Set_Rect(RectClass(res.Width()*.21f,res.Height()*.16f,res.Width()*.79f,res.Height()*.86f));
        const WCHAR *labels[]={L"Continue battle campaign",L"New GDI battle campaign",L"New Nod battle campaign",L"Custom battle",L"Saboteur - coming soon",L"Back"};
        for(int i=0;i<6;++i){ButtonCtrlClass *b=new ButtonCtrlClass;b->Set_ID(7950+i);b->Set_Text(labels[i]);b->Set_Window_Rect(RectClass(res.Width()*.25f,res.Height()*(.28f+i*.08f),res.Width()*.75f,res.Height()*(.34f+i*.08f)));Add_Control(b);}
        std::string file=BR_Command_Progress();std::vector<std::string> maps=BR_Command_Saved_Maps();
        int mission=GetPrivateProfileIntA("Campaign","Mission",0,file.c_str());
        Get_Dlg_Item(7950)->Enable(!maps.empty() && mission>=0 && mission<(int)maps.size() && BR_Command_Map_Exists(maps[mission]) && !GetPrivateProfileIntA("Campaign","Completed",0,file.c_str()));
        if(mission>=0 && mission<(int)maps.size()) {
            WideStringClass label;label.Format(L"Continue battle %d / %d",mission+1,(int)maps.size());
            Get_Dlg_Item(7950)->Set_Text(label);
        }
        if(GetPrivateProfileIntA("Campaign","Completed",0,file.c_str()))Get_Dlg_Item(7950)->Set_Text(L"Battle campaign completed");
        Get_Dlg_Item(7954)->Enable(false);Set_Title(L"COMMAND & CONQUER");PopupDialogClass::On_Init_Dialog();
    }
    void On_Command(int id,int message,DWORD param) {
        if(id==IDCANCEL || id==7955){End_Dialog();return;}
        if(id<7950 || id>7953)return;
        if(cNicEnum::Get_Num_Nics()==0){Set_Title(L"No local network interface is available");return;}
        bool custom=id==7953;
        if(!custom && id!=7950 && !BR_Command_New_Campaign(id==7951?PLAYERTYPE_GDI:PLAYERTYPE_NOD)){Set_Title(L"No Command & Conquer maps found");return;}
        std::vector<std::string> maps=BR_Command_Saved_Maps();std::string file=BR_Command_Progress();
        int mission=GetPrivateProfileIntA("Campaign","Mission",0,file.c_str());
        if(!custom && (mission<0 || mission>=(int)maps.size() || !BR_Command_Map_Exists(maps[mission]))){Set_Title(L"The campaign's next map is unavailable");return;}
        if(!GameModeManager::Find("LAN")){Set_Title(L"The local battle interface is unavailable");return;}
        cGameSpyAdmin::Reset();GameInitMgrClass::Initialize_LAN();
        if(PTheGameData){delete PTheGameData;PTheGameData=NULL;}
        PTheGameData=cGameData::Create_Game_Of_Type(cGameData::GAME_TYPE_CNC);
        cGameDataCnc *game=PTheGameData ? PTheGameData->As_Cnc() : NULL;
        if(!game){Set_Title(L"Could not create the battle settings");return;}
        game->Load_From_Server_Config();game->Set_QuickMatch_Server(false);game->Set_LAN_Bots_Enabled(true);game->Set_LAN_Bots_Per_Team(32);
        if(cNetInterface::Get_Nickname().Is_Empty()){WideStringClass nickname(L"Commander");cNetInterface::Set_Nickname(nickname);}
        if(custom){End_Dialog();START_DIALOG(MPLanHostOptionsMenuClass);return;}
        int team=GetPrivateProfileIntA("Campaign","PlayerTeam",PLAYERTYPE_GDI,file.c_str());if(team!=PLAYERTYPE_GDI && team!=PLAYERTYPE_NOD)team=PLAYERTYPE_GDI;
        // Offline population is 32 per faction including the human commander.
        // Think_LAN_Bots reserves one friendly slot for that body: 63 bots.
        game->Set_LAN_Bots_Per_Team(32);WritePrivateProfileStringA("Settings","BotsPerTeam","32",file.c_str());
        game->Set_Ini_Filename(BR_Command_Session());game->Set_Game_Title(L"Command & Conquer battle campaign");
        game->IsDedicated.Set(false);game->IsLaddered.Set(false);game->IsPassworded.Set(false);game->RemixTeams.Set(false);game->IsTeamChangingAllowed.Set(false);
        game->Set_Do_Maps_Loop(false);game->IsAutoRestart.Set(false);game->Set_Max_Players(1);game->Set_Time_Limit_Minutes(0);
        game->BaseDestructionEndsGame.Set(true);game->BeaconPlacementEndsGame.Set(false);game->Set_Starting_Credits(500);
        game->Clear_Map_Cycle();game->Set_Map_Cycle(0,maps[mission].c_str());game->Set_Map_Name(maps[mission].c_str());game->Set_Is_Map_Cycle_Over(false);
        game->Save_To_Server_Config();
        INIClass *saved=Get_INI(BR_Command_Session());
        if(!saved){Set_Title(L"Could not create campaign settings in the Data folder");return;}
        char saved_map[MAX_PATH];saved->Get_String("Settings","MapName","",saved_map,sizeof(saved_map));
        Release_INI(saved);
        if(stricmp(saved_map,maps[mission].c_str())){Set_Title(L"Campaign settings could not be saved");return;}
        cNetInterface::Set_Side_Preference(team);
        CampaignManager::Select_Backdrop_Number_By_MP_Type(game->Get_Game_Type());
        GameInitMgrClass::Set_Is_Client_Required(true);GameInitMgrClass::Set_Is_Server_Required(true);
        End_Dialog();GameInitMgrClass::Start_Game(game->Get_Map_Name(),team,0);
    }
};
