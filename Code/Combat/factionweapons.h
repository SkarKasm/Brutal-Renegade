#ifndef BR_FACTION_WEAPONS_H
#define BR_FACTION_WEAPONS_H
#include "weaponmanager.h"
#include "definitionmgr.h"
#include "combatchunkid.h"
#include "assets.h"

inline bool BR_Weapon_Model_Is(const WeaponDefinitionClass *def,const char *model)
{
    if(!def)return false;StringClass name;Strip_Path_From_Filename(name,def->FirstPersonModel);
    if(!stricmp(name,model))return true;
    StringClass prototype(model);int length=prototype.Get_Length();
    if(length>4 && !stricmp((const char*)prototype+length-4,".w3d"))prototype[length-4]=0;
    if(!stricmp(name,prototype))return true;
    // AI-only definitions can omit their first-person model; classify by world model too.
    StringClass world;Strip_Path_From_Filename(world,def->Model);StringClass expected;
    if(!strnicmp(prototype,"f_gm_",5))expected.Format("w_%s",(const char*)prototype+5);else return false;
    if(!stricmp(world,expected))return true;expected+=".w3d";return !stricmp(world,expected);
}
inline const WeaponDefinitionClass *BR_Find_Weapon_Model(const char *model)
{
    static int rifle_id=0,grenade_id=0;
    int *cached=!stricmp(model,"f_gm_rifl.w3d") ? &rifle_id : (!stricmp(model,"f_gm_gren.w3d") ? &grenade_id : NULL);
    if(cached && *cached){DefinitionClass *d=DefinitionMgrClass::Find_Definition(*cached,false);
        if(d && d->Get_Class_ID()==CLASSID_DEF_WEAPON && BR_Weapon_Model_Is((WeaponDefinitionClass*)d,model))return (WeaponDefinitionClass*)d;}
    const WeaponDefinitionClass *fallback=NULL;
    for(DefinitionClass *d=DefinitionMgrClass::Get_First(CLASSID_DEF_WEAPON);d;d=DefinitionMgrClass::Get_Next(d,CLASSID_DEF_WEAPON)){
        const WeaponDefinitionClass *w=(WeaponDefinitionClass*)d;
        if(BR_Weapon_Model_Is(w,model)){
            if(strstr(w->Get_Name(),"_Player") && !strstr(w->Get_Name(),"CnC_")){if(cached)*cached=w->Get_ID();return w;}
            if(!fallback)fallback=w;
        }
    }if(cached && fallback)*cached=fallback->Get_ID();return fallback;
}
inline const AmmoDefinitionClass *BR_Grenade_Ammo()
{
    const WeaponDefinitionClass *w=BR_Find_Weapon_Model("f_gm_gren.w3d");
    return w?WeaponManager::Find_Ammo_Definition(w->PrimaryAmmoDefID):NULL;
}
#endif
