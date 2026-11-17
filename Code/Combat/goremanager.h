/*
**	Command & Conquer Renegade(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Combat                                                       *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/Combat/goremanager.h                         $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef	GOREMANAGER_H
#define	GOREMANAGER_H

#ifndef	ALWAYS_H
	#include "always.h"
#endif

class SoldierGameObj;
class OffenseObjectClass;
class Vector3;
class ChunkSaveClass;
class ChunkLoadClass;

/*
** GoreManager (RENEGADE_VR "brutal" gore)
**
** Purely cosmetic, client-side and single-player only: blood sprays and splatter decals,
** blood pools under bodies, dismemberment of killed soldiers (heads, arms, legs), flying
** limb/head gibs cut from the soldier's own model, and Soldier of Fortune 2 stump caps,
** chunks and organs converted to W3D (Code/Tools/GoreAssets). Never touches damage, AI,
** networking. Thermal damage uses an optional versioned save chunk; legacy
** serialized structures and W3D asset layouts stay unchanged.
**
** Settings come from gore.ini next to the exe (Gore.Enabled=0 turns all of it off).
** Missing gore assets are skipped, so a build without the converted SoF2 files still
** shows blood and dismemberment, just without caps and chunks.
**
** Dismemberment works by hiding the skin of a bone and its children
** (RenderObjClass::Set_Bone_Skin_Hidden) and spawning a copy of the model frozen in the
** same pose with only those bones visible (Control_Bone_Root_Relative).
*/
class GoreManager {

public:
	static void Warm_Level_Skins();
	static void		Init( void );				// reads gore.ini
	static void		Shutdown( void );
	static void		Level_Unloaded( void );		// drops every gib and effect
	static void		Update( float dt );			// once per frame, after the physics scene

	static void Save_Thermal_State(SoldierGameObj *soldier,ChunkSaveClass &save);
	static void Load_Thermal_State(SoldierGameObj *soldier,ChunkLoadClass &load);
	static void Restore_Thermal_State(SoldierGameObj *soldier);
	static void Set_Offline_Bot_Mode(bool enabled);
	static bool		Is_Enabled( void );
	static bool		Corpse_Hits_Enabled( void );
	static bool Keep_Chemical_Corpse(SoldierGameObj *soldier);
    static bool Prepare_Voxel_Bullet(SoldierGameObj *soldier,const OffenseObjectClass &offense,
        const Vector3 &direction,const Vector3 &point,const Vector3 &normal,const char *box,int ammo_id,bool explosive);
	// Precise projectile contact, before hit-box names lose their surface position.
	static void Bullet_Hit(SoldierGameObj *soldier, const OffenseObjectClass &offense,
		const Vector3 &direction, const Vector3 &point, const Vector3 &normal, const char *box, bool was_dead);
	// WOUNDTEST fixture only: retain damage processing while preventing death.
	static void Prepare_Inspection_Damage(SoldierGameObj *soldier);
    static void Explosion_Hit(SoldierGameObj *soldier,const OffenseObjectClass &offense,
        const Vector3 &origin,float pressure,float health_before);
    static void Burning_Status_Death(SoldierGameObj *soldier);
	static bool		Test_Fire( void );			// FIRETEST: hold the player's trigger

	//
	// Called from SoldierGameObj::Apply_Damage_Extended once damage has been applied.
	// collision_box_name is the hit box ("model.K_HEAD"), NULL for explosions.
	// explosion_scale is the explosion's falloff at the victim (1 = point blank).
	//
	static void		Soldier_Damaged(	SoldierGameObj * soldier,
												const OffenseObjectClass & damager,
												const Vector3 & direction,
												const char * collision_box_name,
												float explosion_scale,
												float health_before,
												float health_after );
};

#endif	// GOREMANAGER_H
