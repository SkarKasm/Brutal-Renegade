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

#ifndef __VRPLAYER_H__
#define __VRPLAYER_H__

#include "RenegadeVR/renegade_vr_api.h"

class CameraClass;
class TextureClass;

//-----------------------------------------------------------------------------
//
// RENEGADE_VR gameplay: what the player does with the motion controllers.
//
//   - Controllers drive the normal input functions (move, fire, jump...).
//   - The right hand aims; the first-person weapon sits in it.
//   - Snap or smooth turning; walking goes where the head looks.
//   - The HUD (health, armour, radar, weapon, ammo) is a display on the left wrist.
//   - Manual reload: A ejects, off-hand grip takes a chest-pouch magazine,
//     insert at the gun; B releases an empty pistol slide, trigger works actions.
//
// Settings are in renegade_vr.ini, [VR] section; see vrplayer.cpp.
//
//-----------------------------------------------------------------------------
class VRPlayerClass
{
public:
	static void		Init(const char * ini_path);
	static void		Shutdown(void);

	// Once per frame, with the frame's tracking data (also when not in game).
	static void		Update(const RVR_Frame * frame, bool in_game, float dt);

	// Before the eyes are rendered: draws the HUD into the wrist display's texture.
	static void		Prepare_Overlays(void);

	// Inside each eye's render: the wrist display and the laser sight.
	static void		Render_Overlays(CameraClass * eye_camera);
    static void Set_Flat_Panel(bool enabled,int width,int height);
    static void Render_Flat_Panel(CameraClass *eye,TextureClass *texture);
    static void Prepare_Scope();
    static void Prepare_Weapon_Wheel();

	static void		Release_Surfaces(void);

	// For the test scenarios (vrsim.cpp)
	static bool		Is_Holding_Magazine(void);
	static void		Set_Arm_Roll(float degrees);
    static bool Has_Visible_Body();
    static void Set_Test_Body_Model(const char *name);
};

#endif
