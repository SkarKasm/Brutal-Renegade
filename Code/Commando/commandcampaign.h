#ifndef BR_COMMAND_CAMPAIGN_H
#define BR_COMMAND_CAMPAIGN_H
#include "gamedata.h"
#include "gdcnc.h"
#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>
#include <stdio.h>

// A fixed battle campaign, independent of normal LAN server preferences.
inline std::string BR_Command_Path(const char *name)
{
    char path[MAX_PATH];GetModuleFileNameA(NULL,path,sizeof(path));
    char *end=strrchr(path,'\\');if(end)end[1]=0;else path[0]=0;
    return std::string(path)+name;
}
inline const char *BR_Command_Session(){return "svrcfg_command_campaign.ini";}
inline std::string BR_Command_Progress(){return BR_Command_Path("command_campaign.ini");}
inline bool BR_Command_Map_Exists(const std::string &map)
{
    if(map.empty() || map.find('/')!=std::string::npos || map.find('\\')!=std::string::npos || map.find(':')!=std::string::npos)return false;
    DWORD attributes=GetFileAttributesA(BR_Command_Path(("Data\\"+map).c_str()).c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
inline std::vector<std::string> BR_Command_Discover_Maps()
{
    const char *ordered[]={"C&C_Under.mix","C&C_Walls.mix","C&C_Canyon.mix","C&C_Mesa.mix","C&C_Complex.mix","C&C_Islands.mix","C&C_Volcano.mix","C&C_Hourglass.mix","C&C_City.mix","C&C_City_Flying.mix","C&C_Walls_Flying.mix","C&C_Glacier_Flying.mix"};
    std::vector<std::string> maps,extra;
    for(int i=0;i<12;++i)if(BR_Command_Map_Exists(ordered[i]))maps.push_back(ordered[i]);
    WIN32_FIND_DATAA data;HANDLE search=FindFirstFileA(BR_Command_Path("Data\\C&C_*.mix").c_str(),&data);
    if(search!=INVALID_HANDLE_VALUE) {
        do {if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;bool known=false;
            for(size_t i=0;i<maps.size();++i)if(!stricmp(maps[i].c_str(),data.cFileName))known=true;
            if(!known)extra.push_back(data.cFileName);
        } while(FindNextFileA(search,&data));FindClose(search);
    }
    std::sort(extra.begin(),extra.end());maps.insert(maps.end(),extra.begin(),extra.end());return maps;
}
inline std::vector<std::string> BR_Command_Saved_Maps()
{
    std::string file=BR_Command_Progress();int count=GetPrivateProfileIntA("Missions","Count",0,file.c_str());
    if(count<0 || count>256)return std::vector<std::string>();
    std::vector<std::string> maps;
    for(int i=0;i<count;++i){char key[32],map[MAX_PATH];sprintf(key,"Map%03d",i);GetPrivateProfileStringA("Missions",key,"",map,sizeof(map),file.c_str());if(!*map)return std::vector<std::string>();maps.push_back(map);}
    return maps;
}
inline void BR_Command_Write_Int(const char *section,const char *key,int value)
{
    char number[32];sprintf(number,"%d",value);WritePrivateProfileStringA(section,key,number,BR_Command_Progress().c_str());
}
inline bool BR_Command_New_Campaign(int team)
{
    std::vector<std::string> maps=BR_Command_Discover_Maps();if(maps.empty())return false;
    std::string file=BR_Command_Progress();
    BR_Command_Write_Int("Missions","Count",(int)maps.size());
    for(size_t i=0;i<maps.size();++i){char key[32];sprintf(key,"Map%03d",(int)i);WritePrivateProfileStringA("Missions",key,maps[i].c_str(),file.c_str());}
    BR_Command_Write_Int("Campaign","Mission",0);BR_Command_Write_Int("Campaign","Completed",0);
    BR_Command_Write_Int("Campaign","PlayerTeam",team);BR_Command_Write_Int("Campaign","Victories",0);BR_Command_Write_Int("Campaign","Defeats",0);
    return true;
}
inline bool BR_Command_Advance(cGameData &game,int winner)
{
    if(game.Get_Ini_Filename().Compare_No_Case(BR_Command_Session())!=0)return false;
    std::string file=BR_Command_Progress();std::vector<std::string> maps=BR_Command_Saved_Maps();
    if(maps.empty()){game.Set_Is_Map_Cycle_Over(true);return true;}
    int mission=GetPrivateProfileIntA("Campaign","Mission",0,file.c_str());
    if(mission<0 || mission>=(int)maps.size()){game.Set_Is_Map_Cycle_Over(true);return true;}
    int team=GetPrivateProfileIntA("Campaign","PlayerTeam",PLAYERTYPE_GDI,file.c_str());
    if(winner==team) {
        ++mission;BR_Command_Write_Int("Campaign","Mission",mission);
        BR_Command_Write_Int("Campaign","Victories",GetPrivateProfileIntA("Campaign","Victories",0,file.c_str())+1);
    } else BR_Command_Write_Int("Campaign","Defeats",GetPrivateProfileIntA("Campaign","Defeats",0,file.c_str())+1);
    bool complete=mission>=(int)maps.size();BR_Command_Write_Int("Campaign","Completed",complete?1:0);
    game.Set_Is_Map_Cycle_Over(complete);
    if(!complete && BR_Command_Map_Exists(maps[mission])) {
        game.Clear_Map_Cycle();game.Set_Map_Cycle(0,maps[mission].c_str());
        game.Set_Map_Name(maps[mission].c_str());game.Save_To_Server_Config();
    }
    else if(!complete)game.Set_Is_Map_Cycle_Over(true);
    return true;
}
#endif
