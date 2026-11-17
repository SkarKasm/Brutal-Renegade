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
 ***                            Confidential - Westwood Studios                              *** 
 *********************************************************************************************** 
 *                                                                                             * 
 *                 Project Name : Commando                                                     * 
 *                                                                                             * 
 *                     $Archive:: /Commando/Code/Combat/weaponview.h                          $* 
 *                                                                                             * 
 *                      $Author:: Byon_g                                                      $* 
 *                                                                                             * 
 *                     $Modtime:: 1/03/02 5:09p                                               $* 
 *                                                                                             * 
 *                    $Revision:: 18                                                          $* 
 *                                                                                             * 
 *---------------------------------------------------------------------------------------------* 
 * Functions:                                                                                  * 
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef	WEAPONVIEW_H
#define	WEAPONVIEW_H

#ifndef	ALWAYS_H
	#include "always.h"
#endif

#ifndef	VECTOR3_H
	#include "vector3.h"
#endif

class Matrix3D;
struct VREvaVertex;
class	ChunkSaveClass;
class	ChunkLoadClass;

/*
**
*/
class	WeaponViewClass {

public:
	static	void	Init();
	static	void 	Shutdown();
	static	void 	Enable( bool enable );
	static	void 	Think();
	static	void 	Reset();

	static	bool	Save( ChunkSaveClass &csave );
	static	bool	Load( ChunkLoadClass &cload );

	static	Vector3	Get_Muzzle_Pos();
    // True only for a tracked on-foot VR weapon with an authored muzzle bone.
    static bool Get_VR_Muzzle(Matrix3D *tm, int index=0);
    static bool Get_Rendered_Muzzle(Matrix3D *tm,int index=0);
    static void Set_VR_Magazine_Removed(bool removed);
    static bool Is_VR_Magazine_Removed();
    static bool Get_VR_Hand_Pose(int bone, Matrix3D *pose);
    static int Get_VR_EVA_Surface(bool compass, const VREvaVertex **vertices, Matrix3D *pose);
    static bool Get_VR_Reload_Points(Vector3 *feed, Vector3 *action);
    static bool Get_VR_Gun_Frame(Matrix3D *pose);
    static bool Get_VR_Magazine_Geometry(Matrix3D *relative,Vector3 *center);
    static bool Has_VR_Manual_Action();
    static void Set_VR_Action_Progress(float progress);
    static bool Get_VR_Scope(Matrix3D *pose,float *radius);
    static void Hide_VR_For_Scope(bool hidden);
    static void Set_VR_Action_State(int stage);
    static int Get_VR_Reload_Kind(); // magazine=1, shell=2, supply/cell=3, none=0
    static class RenderObjClass *Clone_VR_Magazine();
    static void Log_VR_Weapon_Audit();

	// RENEGADE_VR: when set and it returns true, the first-person hands and weapon model is
	// placed at this world transform (a motion controller) instead of in front of the camera.
	typedef bool (*HandTransformFunc)( class RenderObjClass * hands_model, class RenderObjClass * weapon_model, class Matrix3D * tm );
	static	void	Set_VR_Hand_Transform( HandTransformFunc func )	{ VRHandTransform = func; }
	static	HandTransformFunc	VRHandTransform;
};

#endif
