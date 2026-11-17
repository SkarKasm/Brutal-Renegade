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

#ifndef __VRSIM_H__
#define __VRSIM_H__

#include "RenegadeVR/renegade_vr_api.h"

//-----------------------------------------------------------------------------
//
// RENEGADE_VR test harness: a scripted headset and controllers, so the VR code can
// be run and checked without a headset.
//
//   Renegade.exe -vr MISSION=M01 VRSIM=tour
//
// Scenarios: "idle" (stand still), "wrist" (look at the wrist display, quit), "tour" (wrist display, aim and fire, empty the
// clip, manual reload, snap turn, walk, then quit). Results go to vr_sim.log and
// left-eye screenshots to vr_shots\.
//
//-----------------------------------------------------------------------------
class VRSimClass
{
public:
	static bool		Command_Line_Scenario(char * buffer, int size);
	static void		Init(const char * scenario);
	static void		Shutdown(void);

	// dt is 0 while not in game; the scenario clock only runs in game.
	static void		Fill_Frame(RVR_Frame * frame, float dt, bool in_game);
};

#endif
