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
 *                     $Archive:: /Commando/Code/Commando/vrmanager.cpp                       $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   VRManagerClass::Init -- load RenegadeVR.dll (or the simulator) if -vr was given           *
 *   VRManagerClass::Shutdown -- release surfaces, shut the bridge down                        *
 *   VRManagerClass::Render_Eyes -- one OpenXR frame: gameplay, render and submit both eyes   *
 *   VRManagerClass::Recenter -- make the current head direction forward                       *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "vrmanager.h"
#include "vrplayer.h"
#include "vrsim.h"
#include "ww3d.h"
#include "wwdebug.h"
#include "dx8wrapper.h"
#include "camera.h"
#include "ccamera.h"
#include "combat.h"
#include "soldier.h"
#include "rendobj.h"
#include "texture.h"
#include "formconv.h"
#include "dialogmgr.h"
#include "phys.h"
#include "pscene.h"
#include "backgroundmgr.h"
#include "matrix3d.h"
#include "quat.h"
#include "RenegadeVR/renegade_vr_api.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
** Settings file and log, both next to the exe. All keys are optional.
**
**   [VR]
**   RenderScale=0.7         ; fraction of the headset's recommended eye size
**   FollowCameraPitch=0     ; 1 = mouse/stick pitch also tilts the headset view
**   SimEyeSize=960          ; eye size with VRSIM= (no headset)
**
** Gameplay keys are listed in vrplayer.cpp.
*/
#define VR_DLL_NAME			"RenegadeVR.dll"
#define VR_INI_NAME			"renegade_vr.ini"
static Vector3 _EyeOffset(.08f,0,.10f);
static TextureClass *_FlatPanelTexture=NULL;
static bool _RenderFlatPanel=false;
static unsigned _FlatWidth=0,_FlatHeight=0;
static void Capture_Menu_Frame()
{
    IDirect3DDevice8 *device=DX8Wrapper::_Get_D3D_Device8();if(!device)return;
    IDirect3DSurface8 *back=NULL;if(FAILED(device->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&back)))return;
    D3DSURFACE_DESC desc;if(FAILED(back->GetDesc(&desc)) || !desc.Width || !desc.Height){back->Release();return;}
    if(!_FlatPanelTexture || _FlatWidth!=desc.Width || _FlatHeight!=desc.Height){
        REF_PTR_RELEASE(_FlatPanelTexture);_FlatWidth=desc.Width;_FlatHeight=desc.Height;
        _FlatPanelTexture=NEW_REF(TextureClass,(desc.Width,desc.Height,D3DFormat_To_WW3DFormat(desc.Format),
            TextureClass::MIP_LEVELS_1,TextureClass::POOL_DEFAULT,true));
    }
    IDirect3DSurface8 *surface=_FlatPanelTexture->Get_D3D_Surface_Level();
    if(surface){
        HRESULT hr=device->CopyRects(back,NULL,0,surface,NULL);surface->Release();
        static HRESULT last_error=S_OK;
        if(FAILED(hr) && hr!=last_error){char path[MAX_PATH];VRManagerClass::Get_Exe_Relative_Path("vr_ui.log",path,sizeof(path));
            FILE *log=fopen(path,"a");if(log){fprintf(log,"menu backbuffer copy failed 0x%08lx\n",(unsigned long)hr);fclose(log);}}
        last_error=hr;
    }back->Release();
}
#define VR_LOG_NAME			"renegade_vr.log"

bool										VRManagerClass::Active				= false;
bool										VRManagerClass::Simulated			= false;

static HMODULE							_VRModule							= NULL;
static const RVR_API *				_VRApi								= NULL;
static IDirect3DSurface8 *			_EyeTarget							= NULL;	// lockable, shared by both eyes
static IDirect3DSurface8 *			_EyeDepth							= NULL;
static CameraClass *					_EyeCamera							= NULL;
static int								_EyeWidth							= 0;
static int								_EyeHeight							= 0;
static bool								_FollowCameraPitch				= false;
static bool								_RecenterKeyWasDown				= false;
static Vector3							_LastForward(1.0f, 0.0f, 0.0f);
static FILE *_FrameTimings = NULL;
static bool _SimReadback = false;
static float _EyeReadbackMS[2] = {};
static double _TicksToMS = 0.0;

static LONGLONG Timing_Tick()
{
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

static float Timing_MS(LONGLONG start)
{
    return float((Timing_Tick()-start)*_TicksToMS);
}

static char								_ScreenshotName[64]				= "";
static unsigned long					_LastFrameTime						= 0;


void VRManagerClass::Get_Exe_Relative_Path(const char * file_name, char * buffer, int buffer_size)
{
	buffer[0] = 0;
	if (::GetModuleFileNameA(NULL, buffer, buffer_size) == 0) {
		strncpy(buffer, file_name, buffer_size);
		buffer[buffer_size - 1] = 0;
		return;
	}
	char * slash = strrchr(buffer, '\\');
	if (slash != NULL) {
		slash[1] = 0;
	} else {
		buffer[0] = 0;
	}
	strncat(buffer, file_name, buffer_size - strlen(buffer) - 1);
}


/*
** Accepts "-vr", "/vr", "+vr" or "vr" as a separate word, any case.
*/
bool VRManagerClass::Command_Line_Requests_VR(void)
{
	const char * command_line = ::GetCommandLineA();
	if (command_line == NULL) {
		return false;
	}

	char * copy = strdup(command_line);
	bool found = false;
	for (char * token = strtok(copy, " \t"); token != NULL && !found; token = strtok(NULL, " \t")) {
		if (*token == '-' || *token == '/' || *token == '+') {
			token++;
		}
		if (stricmp(token, "vr") == 0) {
			found = true;
		}
	}
	free(copy);
	return found;
}


void VRManagerClass::Init(void)
{
	if (Active || !Command_Line_Requests_VR()) {
		return;
	}

	char ini_path[MAX_PATH];
	Get_Exe_Relative_Path(VR_INI_NAME, ini_path, sizeof(ini_path));
	char value[32];
	::GetPrivateProfileStringA("VR", "RenderScale", "0.7", value, sizeof(value), ini_path);
	float render_scale = (float)atof(value);
    char eye_value[96];GetPrivateProfileStringA("VR","EyeOffset","0.08,0,0.10",eye_value,sizeof(eye_value),ini_path);
    sscanf(eye_value,"%f,%f,%f",&_EyeOffset.X,&_EyeOffset.Y,&_EyeOffset.Z);
    for(int k=0;k<3;++k)_EyeOffset[k]=WWMath::Clamp(_EyeOffset[k],-.25f,.25f);
	_FollowCameraPitch = (::GetPrivateProfileIntA("VR", "FollowCameraPitch", 0, ini_path) != 0);

	/*
	** No headset: VRSIM=<scenario> fakes one, for testing on the desktop
	*/
	char scenario[64];
	if (VRSimClass::Command_Line_Scenario(scenario, sizeof(scenario))) {
		int size = ::GetPrivateProfileIntA("VR", "SimEyeSize", 960, ini_path);
		_EyeWidth = _EyeHeight = (size < 256) ? 256 : size;
		Simulated = true;
		VRSimClass::Init(scenario);
	} else {
		_VRModule = ::LoadLibraryA(VR_DLL_NAME);
		if (_VRModule == NULL) {
			WWDEBUG_SAY(("VR: %s not found, staying in flatscreen mode\n", VR_DLL_NAME));
			return;
		}

		RVR_Get_API_Func get_api = (RVR_Get_API_Func)::GetProcAddress(_VRModule, RVR_GET_API_NAME);
		_VRApi = (get_api != NULL) ? get_api(RVR_API_VERSION) : NULL;
		if (_VRApi == NULL) {
			WWDEBUG_SAY(("VR: %s has no compatible API (want version %d)\n", VR_DLL_NAME, RVR_API_VERSION));
			::FreeLibrary(_VRModule);
			_VRModule = NULL;
			return;
		}

		char log_path[MAX_PATH];
		Get_Exe_Relative_Path(VR_LOG_NAME, log_path, sizeof(log_path));
		if (!_VRApi->Init(render_scale, log_path)) {
			WWDEBUG_SAY(("VR: no headset or OpenXR runtime, staying in flatscreen mode (see %s)\n", VR_LOG_NAME));
			_VRApi->Shutdown();
			_VRApi = NULL;
			::FreeLibrary(_VRModule);
			_VRModule = NULL;
			return;
		}
		_VRApi->Get_Eye_Size(&_EyeWidth, &_EyeHeight);
	}

    if (strstr(::GetCommandLineA(),"VRPROFILE=1") || GetPrivateProfileIntA("VR","FrameTiming",0,ini_path)) {
        LARGE_INTEGER frequency;
        if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart>0) {
            char path[MAX_PATH];
            Get_Exe_Relative_Path("vr_frame_times.csv",path,sizeof(path));
            _FrameTimings=fopen(path,"w");
            if (_FrameTimings) {
                _TicksToMS=1000.0/frequency.QuadPart;
                _SimReadback=Simulated && strstr(::GetCommandLineA(),"VRREADBACK=1");
                fputs("frame_ms,overlays_ms,wheel_ms,scope_ms,left_ms,right_ms,left_readback_ms,right_readback_ms,screenshot,xr_begin_ms,xr_end_ms\n",_FrameTimings);
            }
        }
    }

	_EyeCamera = NEW_REF(CameraClass, ());
	DX8Wrapper::Set_Device_Release_Callback(&VRManagerClass::Release_Surfaces);

	Active = true;
	VRPlayerClass::Init(ini_path);
	WWDEBUG_SAY(("VR: active on %s, eye %dx%d\n", Simulated ? "simulator" : _VRApi->Get_Runtime_Name(), _EyeWidth, _EyeHeight));
}


void VRManagerClass::Shutdown(void)
{
    if (_FrameTimings) { fclose(_FrameTimings); _FrameTimings=NULL; }
    _SimReadback=false;
	if (!Active) {
		return;
	}
	Active = false;

	VRPlayerClass::Shutdown();
	DX8Wrapper::Set_Device_Release_Callback(NULL);
	Release_Surfaces();
	REF_PTR_RELEASE(_EyeCamera);

	if (Simulated) {
		VRSimClass::Shutdown();
		Simulated = false;
	} else {
		_VRApi->Shutdown();
		_VRApi = NULL;
		::FreeLibrary(_VRModule);
		_VRModule = NULL;
	}
}


void VRManagerClass::Recenter(void)
{
	if (Active && !Simulated) {
		_VRApi->Recenter();
	}
}


void VRManagerClass::Vibrate(int hand, float amplitude, float seconds)
{
	if (Active && !Simulated) {
		_VRApi->Vibrate(hand, amplitude, seconds);
	}
}


void VRManagerClass::Request_Screenshot(const char * name)
{
	strncpy(_ScreenshotName, name, sizeof(_ScreenshotName) - 1);
	_ScreenshotName[sizeof(_ScreenshotName) - 1] = 0;
}


/*
** Eye surfaces live in D3DPOOL_DEFAULT, so they are dropped on every device reset
** (alt-tab, resolution change) and recreated on the next frame.
*/
bool VRManagerClass::Create_Surfaces(void)
{
	if (_EyeTarget != NULL) {
		return true;
	}

	IDirect3DDevice8 * device = DX8Wrapper::_Get_D3D_Device8();
	if (device == NULL) {
		return false;
	}

	// Match the depth format the game already uses.
	D3DFORMAT depth_format = D3DFMT_D16;
	IDirect3DSurface8 * default_depth = NULL;
	if (SUCCEEDED(device->GetDepthStencilSurface(&default_depth)) && default_depth != NULL) {
		D3DSURFACE_DESC desc;
		if (SUCCEEDED(default_depth->GetDesc(&desc))) {
			depth_format = desc.Format;
		}
		default_depth->Release();
	}

	HRESULT hr = device->CreateRenderTarget(_EyeWidth, _EyeHeight, D3DFMT_X8R8G8B8,
		D3DMULTISAMPLE_NONE, TRUE, &_EyeTarget);
	if (FAILED(hr)) {
		WWDEBUG_SAY(("VR: CreateRenderTarget %dx%d failed (0x%08x)\n", _EyeWidth, _EyeHeight, hr));
		_EyeTarget = NULL;
		return false;
	}

	hr = device->CreateDepthStencilSurface(_EyeWidth, _EyeHeight, depth_format, D3DMULTISAMPLE_NONE, &_EyeDepth);
	if (FAILED(hr)) {
		WWDEBUG_SAY(("VR: CreateDepthStencilSurface failed (0x%08x)\n", hr));
		Release_Surfaces();
		return false;
	}
	return true;
}


void VRManagerClass::Release_Surfaces(void)
{
    REF_PTR_RELEASE(_FlatPanelTexture);_FlatWidth=_FlatHeight=0;
	if (_EyeTarget != NULL) {
		_EyeTarget->Release();
		_EyeTarget = NULL;
	}
	if (_EyeDepth != NULL) {
		_EyeDepth->Release();
		_EyeDepth = NULL;
	}
	VRPlayerClass::Release_Surfaces();
}


/*
** Strip pitch and roll from the game camera so the horizon follows the player's
** real head, not the mouse. Renegade is Z-up; a W3D camera looks down its local -Z.
*/
static Matrix3D Yaw_Only_Camera_Transform(const Matrix3D & tm)
{
	Vector3 forward(-tm[0][2], -tm[1][2], 0.0f);
	if (forward.Length2() < 0.0001f) {
		forward = _LastForward;		// looking straight up or down: keep the last heading
	} else {
		forward.Normalize();
		_LastForward = forward;
	}

	// Columns: right, up (world Z), back (-forward)
	Matrix3D out(true);
	out[0][0] = forward.Y;	out[0][1] = 0.0f;	out[0][2] = -forward.X;
	out[1][0] = -forward.X;	out[1][1] = 0.0f;	out[1][2] = -forward.Y;
	out[2][0] = 0.0f;			out[2][1] = 1.0f;	out[2][2] = 0.0f;
	out.Set_Translation(tm.Get_Translation());
	return out;
}


Matrix3D VRManagerClass::Get_Tracking_To_World(void)
{
	CCameraClass * game_camera = COMBAT_CAMERA;
	if (game_camera == NULL) {
		return Matrix3D(true);
	}
	Matrix3D base=_FollowCameraPitch ? game_camera->Get_Transform() : Yaw_Only_Camera_Transform(game_camera->Get_Transform());
    SoldierGameObj *star=COMBAT_STAR;
    if(star && !star->Is_Dead() && !star->Get_Vehicle() && !star->Is_On_Ladder() &&
        !star->Is_Sniping() && !game_camera->Is_Using_Host_Model() && CombatManager::Is_First_Person()){
        RenderObjClass *body=star->Peek_Model();int head=body?body->Get_Bone_Index("C HEAD"):-1;
        if(head>0){
            // Bind the re-centered HMD to anatomical eyes, above/forward of the
            // head pivot at the neck. Hands and both eyes share this base.
            Vector3 forward=-base.Get_Z_Vector(),left=-base.Get_X_Vector();
            forward.Z=left.Z=0;if(forward.Length2()>.001f)forward.Normalize();if(left.Length2()>.001f)left.Normalize();
            base.Set_Translation(body->Get_Bone_Transform(head).Get_Translation()+
                forward*_EyeOffset.X+left*_EyeOffset.Y+Vector3(0,0,_EyeOffset.Z));
        }
    }
    return base;
}


/*
** 24-bit BMP of a locked X8R8G8B8 surface
*/
static void Save_BMP(const char * name, const void * pixels, int pitch, int width, int height)
{
	char dir[MAX_PATH], path[MAX_PATH];
	VRManagerClass::Get_Exe_Relative_Path("vr_shots", dir, sizeof(dir));
	::CreateDirectoryA(dir, NULL);
	_snprintf(path, sizeof(path), "%s\\%s.bmp", dir, name);
	FILE * f = fopen(path, "wb");
	if (f == NULL) {
		return;
	}
	int row = (width * 3 + 3) & ~3;
	BITMAPFILEHEADER fh = { 0 };
	BITMAPINFOHEADER ih = { 0 };
	fh.bfType = 0x4D42;
	fh.bfOffBits = sizeof(fh) + sizeof(ih);
	fh.bfSize = fh.bfOffBits + row * height;
	ih.biSize = sizeof(ih);
	ih.biWidth = width;
	ih.biHeight = height;		// bottom-up
	ih.biPlanes = 1;
	ih.biBitCount = 24;
	fwrite(&fh, sizeof(fh), 1, f);
	fwrite(&ih, sizeof(ih), 1, f);
	unsigned char * line = new unsigned char[row];
	memset(line, 0, row);
	for (int y = height - 1; y >= 0; y--) {
		const unsigned char * src = (const unsigned char *)pixels + y * pitch;
		for (int x = 0; x < width; x++) {
			line[x * 3 + 0] = src[x * 4 + 0];
			line[x * 3 + 1] = src[x * 4 + 1];
			line[x * 3 + 2] = src[x * 4 + 2];
		}
		fwrite(line, row, 1, f);
	}
	delete [] line;
	fclose(f);
}


void VRManagerClass::Render_Eye(int eye, const void * frame_ptr)
{
	const RVR_Frame & frame = *(const RVR_Frame *)frame_ptr;
	CCameraClass * game_camera = COMBAT_CAMERA;

	/*
	** Eye pose is in the bridge's tracking space, which uses the same axes as a W3D
	** camera's local frame (+Y up, -Z forward, metres), so it composes directly.
	*/
	Matrix3D base = Get_Tracking_To_World();
	const RVR_Pose & pose = frame.Eye[eye];
	Matrix3D head = Build_Matrix3D(Quaternion(pose.Orientation[0], pose.Orientation[1], pose.Orientation[2], pose.Orientation[3]));
	head.Set_Translation(Vector3(pose.Position[0], pose.Position[1], pose.Position[2]));

	Matrix3D eye_tm;
	Matrix3D::Multiply(base, head, &eye_tm);

	float znear, zfar;
	if(game_camera)game_camera->Get_Clip_Planes(znear,zfar);else{znear=.03f;zfar=100.0f;}
	if (znear > 0.05f) {
		znear = 0.05f;		// hands and the wrist display are close to the eye
	}

	const RVR_Fov & fov = frame.Fov[eye];
	_EyeCamera->Set_Transform(eye_tm);
	_EyeCamera->Set_Clip_Planes(znear, zfar);
	_EyeCamera->Set_View_Plane(Vector2(fov.TanLeft, fov.TanDown), Vector2(fov.TanRight, fov.TanUp));
	_EyeCamera->Set_Viewport(Vector2(0.0f, 0.0f), Vector2(1.0f, 1.0f));

	// Each eye has its own view. Authored visibility sectors assume the
	// original camera positions, so use the spatial trees and eye frustum.
	if (!_RenderFlatPanel) {
		COMBAT_SCENE->Prepare_View(*_EyeCamera, false);
	}

	// Same bracket the texture projectors use for off-screen passes.
	DX8Wrapper::Set_Render_Target(_EyeTarget);
	DX8Wrapper::_Get_D3D_Device8()->SetRenderTarget(_EyeTarget, _EyeDepth);	// wrapper sets no depth buffer

	WW3D::Begin_Render(true, true, BackgroundMgrClass::Get_Clear_Color());
    if(_RenderFlatPanel)VRPlayerClass::Render_Flat_Panel(_EyeCamera,_FlatPanelTexture);
    else{
	    WW3D::Render(CombatManager::Get_Background_Scene(), _EyeCamera);
	    WW3D::Render(COMBAT_SCENE, _EyeCamera);
	    VRPlayerClass::Render_Overlays(_EyeCamera);
    }
	WW3D::End_Render(false);

	DX8Wrapper::Set_Render_Target((IDirect3DSurface8 *)NULL);

	// Stage 0 transport: read back and hand to the bridge (stalls the GPU; see VR_MOD_PLAN.md).
	bool want_shot = (eye == 0 && _ScreenshotName[0] != 0);
	if (Simulated && !want_shot && !_SimReadback) {
		return;
	}
    const LONGLONG begin = _FrameTimings ? Timing_Tick() : 0;
	D3DLOCKED_RECT locked;
	if (SUCCEEDED(_EyeTarget->LockRect(&locked, NULL, D3DLOCK_READONLY))) {
		if (want_shot) {
			Save_BMP(_ScreenshotName, locked.pBits, locked.Pitch, _EyeWidth, _EyeHeight);
			_ScreenshotName[0] = 0;
		}
		if (!Simulated) {
			_VRApi->Submit_Eye(eye, locked.pBits, locked.Pitch, _EyeWidth, _EyeHeight);
		}
		_EyeTarget->UnlockRect();
	}
    if (_FrameTimings) _EyeReadbackMS[eye] = Timing_MS(begin);
}


void VRManagerClass::Render_Eyes(bool render_scene)
{
	if (!Active) {
		return;
	}

	// Ctrl+Home recenters (the left Y button does too, see vrplayer.cpp)
	bool key_down = ((::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) && ((::GetAsyncKeyState(VK_HOME) & 0x8000) != 0);
	if (key_down && !_RecenterKeyWasDown) {
		Recenter();
	}
	_RecenterKeyWasDown = key_down;

	unsigned long now = ::timeGetTime();
	float dt = (_LastFrameTime != 0) ? (float)(now - _LastFrameTime) * 0.001f : 0.0f;
	_LastFrameTime = now;
	if (dt > 0.25f) {
		dt = 0.25f;
	}

	bool in_game = render_scene && COMBAT_SCENE != NULL && COMBAT_CAMERA != NULL;
    _RenderFlatPanel=!in_game || CombatManager::Is_Game_Paused() || DialogMgrClass::Get_Active_Dialog()!=NULL;
    if(_RenderFlatPanel)Capture_Menu_Frame();

    RVR_Frame frame;
    const bool profile=_FrameTimings && in_game && !_RenderFlatPanel;
    const LONGLONG xr_begin=profile && !Simulated ? Timing_Tick() : 0;
    if (Simulated) {
        memset(&frame, 0, sizeof(frame));
        VRSimClass::Fill_Frame(&frame, dt, in_game);
    } else if (!_VRApi->Begin_Frame(&frame)) {
        return;
    }
    const float xr_begin_ms=xr_begin ? Timing_MS(xr_begin) : 0.f;

    VRPlayerClass::Update(&frame, in_game && !_RenderFlatPanel, dt);
    VRPlayerClass::Set_Flat_Panel(_RenderFlatPanel,(int)_FlatWidth,(int)_FlatHeight);

    const bool screenshot=_ScreenshotName[0]!=0;
    float overlays=0.f, wheel=0.f, scope=0.f, eye_ms[2]={};
    _EyeReadbackMS[0]=_EyeReadbackMS[1]=0.f;
    const bool render=frame.ShouldRender && Create_Surfaces();
    if (render) {
        LONGLONG begin=profile ? Timing_Tick() : 0;
        VRPlayerClass::Prepare_Overlays();
        overlays=profile ? Timing_MS(begin) : 0.f;
        if (profile) begin=Timing_Tick();
        if (!_RenderFlatPanel) VRPlayerClass::Prepare_Weapon_Wheel();
        wheel=profile ? Timing_MS(begin) : 0.f;
        if (profile) begin=Timing_Tick();
        if (!_RenderFlatPanel) VRPlayerClass::Prepare_Scope();
        scope=profile ? Timing_MS(begin) : 0.f;
        for (int eye=0;eye<RVR_EYE_COUNT;++eye) {
            if (profile) begin=Timing_Tick();
            Render_Eye(eye,&frame);
            if (profile) eye_ms[eye]=Timing_MS(begin);
        }
    }

    // With no eyes submitted the bridge ends the frame with no layers.
    const LONGLONG xr_end=profile && !Simulated ? Timing_Tick() : 0;
    if (!Simulated) {
        _VRApi->End_Frame();
    }
    if (profile && render) {
        fprintf(_FrameTimings,"%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%.3f,%.3f\n",
            dt*1000.f,overlays,wheel,scope,eye_ms[0],eye_ms[1],_EyeReadbackMS[0],_EyeReadbackMS[1],
            screenshot,xr_begin_ms,xr_end ? Timing_MS(xr_end) : 0.f);
    }
}
