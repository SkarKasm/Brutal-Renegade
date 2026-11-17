#ifndef BR_WEAPON_ICONS_H
#define BR_WEAPON_ICONS_H
#include "weaponmanager.h"
#include "assets.h"

inline bool BR_Is_MK23_Icon(const WeaponDefinitionClass *def)
{
    return false; // Vanilla pistol uses its original definition/atlas icon.
}

// UVs use source pixels; scale controls legacy HUD display size independently.
inline void BR_Weapon_Icon(const WeaponDefinitionClass *def,StringClass &file,RectClass &uv,float &scale)
{
    file="hud_TibRVoltR.tga";uv=RectClass(0,64,128,128);scale=1;
    if(def && !def->IconTextureName.Is_Empty()){
        Strip_Path_From_Filename(file,def->IconTextureName);uv=def->IconTextureUV;
    }
    if(BR_Is_MK23_Icon(def)){file="br_mk23_icon.tga";uv=RectClass(0,128,512,384);scale=.25f;}
}
// Custom atlases have no legacy thumbnail entry. Their source pixel extent
// must not depend on the asynchronous GPU texture's temporary/mip dimensions.
inline float BR_Weapon_Icon_Source_Size(const char *file,float loaded_size)
{
    return file && !stricmp(file,"br_mk23_icon.tga") ? 512.0f : loaded_size;
}
#endif
