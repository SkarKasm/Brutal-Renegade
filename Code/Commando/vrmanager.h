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
 *                 Project Name : Commando                                                     *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/Commando/vrmanager.h                         $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef __VRMANAGER_H__
#define __VRMANAGER_H__

#include "matrix3d.h"

class CameraClass;

//-----------------------------------------------------------------------------
//
// Optional VR output (RENEGADE_VR).
//
// VR is enabled only when the game is started with -vr AND RenegadeVR.dll loads
// and finds a headset. Otherwise every call here is a no-op and the game runs
// exactly as the flatscreen build. See VR_MOD_PLAN.md and Code/RenegadeVR/README.md.
//
// Each frame renders the combat scene once per eye into an off-screen target (with
// the head pose applied on top of the game camera) and hands the pixels to the bridge.
// The monitor keeps showing the normal game view. Gameplay (controllers, hand aiming,
// the wrist display, manual reloads) is in vrplayer.cpp.
//
// -vr VRSIM=<scenario> runs without a headset: a scripted head and controllers
// (vrsim.cpp) drive the same code, and screenshots of the left eye go to vr_shots\.
//
//-----------------------------------------------------------------------------
class	VRManagerClass
{
public:

	static void		Init(void);				// after the render device exists
	static void		Shutdown(void);			// before WW3D::Shutdown

	static bool		Is_Active(void)			{ return Active; }
	static bool		Is_Simulated(void)		{ return Simulated; }

	//
	// Call once per frame, outside WW3D::Begin_Render/End_Render.
	// render_scene is false when there is no combat scene to show (menus, loading).
	//
	static void		Render_Eyes(bool render_scene);

	static void		Recenter(void);

	// Tracking space (metres, +Y up, -Z forward, origin = head at recenter) to world,
	// for this frame: the game camera's position and heading.
	static Matrix3D	Get_Tracking_To_World(void);

	static void		Vibrate(int hand, float amplitude, float seconds);

	// Save the next left-eye image as vr_shots\<name>.bmp
	static void		Request_Screenshot(const char * name);

	static void		Get_Exe_Relative_Path(const char * file_name, char * buffer, int buffer_size);

private:

	static bool		Command_Line_Requests_VR(void);
	static bool		Create_Surfaces(void);
	static void		Release_Surfaces(void);	// device reset / release callback
	static void		Render_Eye(int eye, const void * frame);

	static bool		Active;
	static bool		Simulated;
};

#endif	// __VRMANAGER_H__
