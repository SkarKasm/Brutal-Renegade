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

/*
** renegade_vr_api.h
**
** C interface between Renegade.exe (built with VC++ 6.0, Direct3D 8) and
** RenegadeVR.dll (built with a modern compiler, OpenXR + Direct3D 11).
**
** This header is included by BOTH sides, so it must stay VC6-safe:
** plain C, no stdint.h, no OpenXR or D3D11 types.
**
** The game loads the DLL with LoadLibrary and calls RVR_Get_API. If the DLL
** is missing, the version doesn't match, or Init fails, the game stays in
** flatscreen mode.
*/

#ifndef RENEGADE_VR_API_H
#define RENEGADE_VR_API_H

#ifdef __cplusplus
extern "C" {
#endif

#define RVR_API_VERSION		2
#define RVR_EYE_COUNT		2
#define RVR_HAND_COUNT		2
#define RVR_HAND_LEFT		0
#define RVR_HAND_RIGHT		1
#define RVR_GET_API_NAME	"RVR_Get_API"

/* RVR_Hand.Buttons */
#define RVR_BUTTON_PRIMARY		0x01		/* A on the right controller, X on the left */
#define RVR_BUTTON_SECONDARY		0x02		/* B on the right controller, Y on the left */
#define RVR_BUTTON_STICK			0x04		/* thumbstick click */
#define RVR_BUTTON_MENU			0x08		/* menu button (left controller) */

/*
** Pose in the tracking space the bridge reports (OpenXR LOCAL space,
** re-centred by RVR_Recenter). Metres. Right-handed, +Y up, -Z forward,
** which is the same convention as a W3D camera's local frame.
*/
typedef struct
{
	float	Orientation[4];		/* quaternion x, y, z, w */
	float	Position[3];		/* metres */
} RVR_Pose;

/*
** Field of view as tangents of the half-angles (left and down are negative).
** These plug straight into CameraClass::Set_View_Plane(min, max).
*/
typedef struct
{
	float	TanLeft;
	float	TanRight;
	float	TanUp;
	float	TanDown;
} RVR_Fov;

/*
** One motion controller, in the same space as the eyes.
** Grip: the hand holding the controller (-Z along the closed fist, +Y up from the back of the hand).
** Aim: the pointing ray (-Z forward).
*/
typedef struct
{
	int			Active;					/* 1 while the controller is tracked */
	RVR_Pose		Grip;
	RVR_Pose		Aim;
	float			Trigger;					/* 0 .. 1 */
	float			Squeeze;					/* grip button, 0 .. 1 */
	float			StickX;					/* -1 (left) .. 1 (right) */
	float			StickY;					/* -1 (back) .. 1 (forward) */
	int			Buttons;					/* RVR_BUTTON_* */
} RVR_Hand;

typedef struct
{
	int			ShouldRender;			/* 0 when the runtime wants no frame (headset idle, etc.) */
	int			PoseValid;				/* 0 when tracking is lost; keep last pose */
	RVR_Pose		Eye[RVR_EYE_COUNT];
	RVR_Fov		Fov[RVR_EYE_COUNT];
	RVR_Pose		Head;						/* between the eyes */
	RVR_Hand		Hand[RVR_HAND_COUNT];	/* RVR_HAND_LEFT, RVR_HAND_RIGHT */
} RVR_Frame;

typedef struct
{
	int			Version;					/* RVR_API_VERSION */

	/*
	** Init creates the OpenXR instance, session and swapchains.
	** render_scale multiplies the runtime's recommended eye size (0.25 .. 2.0).
	** log_path may be NULL. Returns 1 on success.
	*/
	int			(*Init)(float render_scale, const char * log_path);
	void			(*Shutdown)(void);

	/* Eye image size the game must render at (valid after Init). */
	void			(*Get_Eye_Size)(int * width, int * height);

	/*
	** Once per game frame:
	**   Begin_Frame -> (if frame->ShouldRender) Submit_Eye for each eye -> End_Frame
	** Begin_Frame blocks in xrWaitFrame, so it also paces the game to the headset.
	** Returns 0 if no frame was begun (session not running); do not call End_Frame then.
	*/
	int			(*Begin_Frame)(RVR_Frame * frame);

	/*
	** pixels: 32-bit B,G,R,X rows (D3DFMT_X8R8G8B8 / A8R8G8B8 as locked from D3D8),
	** gamma-encoded. pitch in bytes. width/height must equal Get_Eye_Size.
	*/
	void			(*Submit_Eye)(int eye, const void * pixels, int pitch, int width, int height);
	void			(*End_Frame)(void);

	/* Make the current head yaw and position the new forward / origin. */
	void			(*Recenter)(void);

	/* 1 while the session is running and focused (headset on). */
	int			(*Is_Session_Focused)(void);

	/* Null-terminated runtime name, for logs. */
	const char *(*Get_Runtime_Name)(void);

	/* Version 2: buzz a controller. amplitude 0 .. 1, seconds > 0. */
	void			(*Vibrate)(int hand, float amplitude, float seconds);
} RVR_API;

typedef const RVR_API * (*RVR_Get_API_Func)(int requested_version);

#ifdef __cplusplus
}
#endif

#endif /* RENEGADE_VR_API_H */
