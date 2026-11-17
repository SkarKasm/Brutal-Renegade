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
** renegade_vr.cpp
**
** RenegadeVR.dll: OpenXR session on a Direct3D 11 device, used by Renegade.exe
** through the C table in renegade_vr_api.h.
**
** Stage 0 transport: the game renders each eye with Direct3D 8, locks the
** surface and passes the pixels here; they are uploaded into the OpenXR
** swapchain image with UpdateSubresource. See VR_MOD_PLAN.md section 3.
*/

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "renegade_vr_api.h"

namespace {

struct EyeSwapchain
{
	XrSwapchain								Handle = XR_NULL_HANDLE;
	std::vector<XrSwapchainImageD3D11KHR>	Images;
	bool										Submitted = false;
};

struct BridgeState
{
	FILE *						Log = nullptr;

	XrInstance					Instance = XR_NULL_HANDLE;
	XrSystemId					System = XR_NULL_SYSTEM_ID;
	XrSession					Session = XR_NULL_HANDLE;
	XrSpace						LocalSpace = XR_NULL_HANDLE;	// raw LOCAL space
	XrSpace						ViewSpace = XR_NULL_HANDLE;		// head
	XrSpace						AppSpace = XR_NULL_HANDLE;		// LOCAL re-centred; all poses are reported in this
	XrSessionState				SessionState = XR_SESSION_STATE_UNKNOWN;
	bool							SessionRunning = false;
	bool							RecenterPending = true;

	ID3D11Device *				Device = nullptr;
	ID3D11DeviceContext *	Context = nullptr;

	DXGI_FORMAT					SwapchainFormat = DXGI_FORMAT_UNKNOWN;
	bool							SwizzleRB = false;				// swapchain is RGBA, game gives BGRA
	std::vector<unsigned int>	SwizzleBuffer;

	int							EyeWidth = 0;
	int							EyeHeight = 0;
	EyeSwapchain				Eyes[RVR_EYE_COUNT];

	bool							FrameBegun = false;
	XrFrameState				FrameState = { XR_TYPE_FRAME_STATE };
	XrView						Views[RVR_EYE_COUNT];

	char							RuntimeName[XR_MAX_RUNTIME_NAME_SIZE + 32] = "none";

	// Controllers (API version 2)
	XrActionSet					ActionSet = XR_NULL_HANDLE;
	XrAction						GripPose = XR_NULL_HANDLE;
	XrAction						AimPose = XR_NULL_HANDLE;
	XrAction						Trigger = XR_NULL_HANDLE;
	XrAction						Squeeze = XR_NULL_HANDLE;
	XrAction						Stick = XR_NULL_HANDLE;
	XrAction						StickClick = XR_NULL_HANDLE;
	XrAction						Primary = XR_NULL_HANDLE;
	XrAction						Secondary = XR_NULL_HANDLE;
	XrAction						Menu = XR_NULL_HANDLE;
	XrAction						Haptic = XR_NULL_HANDLE;
	XrPath						HandPath[RVR_HAND_COUNT] = { XR_NULL_PATH, XR_NULL_PATH };
	XrSpace						GripSpace[RVR_HAND_COUNT] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
	XrSpace						AimSpace[RVR_HAND_COUNT] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
	bool							ActionsReady = false;
};

BridgeState g;

void Log(const char * format, ...)
{
	if (g.Log == nullptr) {
		return;
	}
	va_list args;
	va_start(args, format);
	vfprintf(g.Log, format, args);
	va_end(args);
	fputc('\n', g.Log);
	fflush(g.Log);
}

bool Check(XrResult result, const char * what)
{
	if (XR_SUCCEEDED(result)) {
		return true;
	}
	char name[XR_MAX_RESULT_STRING_SIZE] = "";
	if (g.Instance != XR_NULL_HANDLE) {
		xrResultToString(g.Instance, result, name);
	}
	Log("[RenegadeVR] %s failed: %d %s", what, (int)result, name);
	return false;
}

/*
** Yaw-only version of a pose: keeps position, drops pitch and roll, so a
** recenter never tilts the horizon. Y is up in OpenXR.
*/
XrPosef Yaw_Only(const XrPosef & pose)
{
	const XrQuaternionf & q = pose.orientation;

	// Forward is -Z rotated by q; project it onto the XZ plane.
	float fx = -(2.0f * (q.x * q.z + q.w * q.y));
	float fz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
	float yaw = std::atan2(-fx, -fz);

	XrPosef out;
	out.orientation.x = 0.0f;
	out.orientation.y = std::sin(yaw * 0.5f);
	out.orientation.z = 0.0f;
	out.orientation.w = std::cos(yaw * 0.5f);
	out.position = pose.position;
	return out;
}

void Destroy_App_Space(void)
{
	if (g.AppSpace != XR_NULL_HANDLE) {
		xrDestroySpace(g.AppSpace);
		g.AppSpace = XR_NULL_HANDLE;
	}
}

bool Create_App_Space(const XrPosef & origin)
{
	Destroy_App_Space();
	XrReferenceSpaceCreateInfo info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	info.poseInReferenceSpace = origin;
	return Check(xrCreateReferenceSpace(g.Session, &info, &g.AppSpace), "xrCreateReferenceSpace(app)");
}

void Do_Recenter(XrTime time)
{
	XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
	if (!XR_SUCCEEDED(xrLocateSpace(g.ViewSpace, g.LocalSpace, time, &location))) {
		return;
	}
	const XrSpaceLocationFlags needed = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
	if ((location.locationFlags & needed) != needed) {
		return;	// try again next frame
	}
	if (Create_App_Space(Yaw_Only(location.pose))) {
		g.RecenterPending = false;
		Log("[RenegadeVR] Recentered at (%.3f, %.3f, %.3f)",
			location.pose.position.x, location.pose.position.y, location.pose.position.z);
	}
}

void Poll_Events(void)
{
	XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };
	while (xrPollEvent(g.Instance, &event) == XR_SUCCESS) {
		switch (event.type) {
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
			{
				const XrEventDataSessionStateChanged & changed =
					*reinterpret_cast<const XrEventDataSessionStateChanged *>(&event);
				g.SessionState = changed.state;
				Log("[RenegadeVR] Session state %d", (int)changed.state);

				if (changed.state == XR_SESSION_STATE_READY) {
					XrSessionBeginInfo begin = { XR_TYPE_SESSION_BEGIN_INFO };
					begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
					if (Check(xrBeginSession(g.Session, &begin), "xrBeginSession")) {
						g.SessionRunning = true;
						g.RecenterPending = true;
					}
				} else if (changed.state == XR_SESSION_STATE_STOPPING) {
					Check(xrEndSession(g.Session), "xrEndSession");
					g.SessionRunning = false;
				} else if (changed.state == XR_SESSION_STATE_EXITING || changed.state == XR_SESSION_STATE_LOSS_PENDING) {
					// The game keeps running flatscreen; the session is not restarted in Stage 0.
					g.SessionRunning = false;
				}
				break;
			}
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
				g.RecenterPending = true;	// runtime-level recenter (e.g. Oculus button)
				break;
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
				g.SessionRunning = false;
				break;
			default:
				break;
		}
		event = { XR_TYPE_EVENT_DATA_BUFFER };
	}
}

bool Create_D3D11_Device(void)
{
	PFN_xrGetD3D11GraphicsRequirementsKHR get_requirements = nullptr;
	if (!Check(xrGetInstanceProcAddr(g.Instance, "xrGetD3D11GraphicsRequirementsKHR",
			reinterpret_cast<PFN_xrVoidFunction *>(&get_requirements)), "xrGetInstanceProcAddr(D3D11 requirements)")) {
		return false;
	}

	// Must be called before xrCreateSession.
	XrGraphicsRequirementsD3D11KHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
	if (!Check(get_requirements(g.Instance, g.System, &requirements), "xrGetD3D11GraphicsRequirementsKHR")) {
		return false;
	}

	IDXGIFactory1 * factory = nullptr;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)))) {
		Log("[RenegadeVR] CreateDXGIFactory1 failed");
		return false;
	}

	IDXGIAdapter1 * adapter = nullptr;
	for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);
		if (std::memcmp(&desc.AdapterLuid, &requirements.adapterLuid, sizeof(LUID)) == 0) {
			Log("[RenegadeVR] HMD adapter: %ls", desc.Description);
			break;
		}
		adapter->Release();
		adapter = nullptr;
	}
	factory->Release();

	if (adapter == nullptr) {
		Log("[RenegadeVR] No DXGI adapter matches the runtime's LUID");
		return false;
	}

	const D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
	};
	UINT level_count = 0;
	while (level_count < ARRAYSIZE(levels) && levels[level_count] >= requirements.minFeatureLevel) {
		++level_count;
	}
	if (level_count == 0) {
		level_count = 1;
	}

	HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
		levels, level_count, D3D11_SDK_VERSION, &g.Device, nullptr, &g.Context);
	adapter->Release();

	if (FAILED(hr)) {
		Log("[RenegadeVR] D3D11CreateDevice failed: 0x%08x", (unsigned)hr);
		return false;
	}
	return true;
}

bool Choose_Swapchain_Format(void)
{
	uint32_t count = 0;
	if (!Check(xrEnumerateSwapchainFormats(g.Session, 0, &count, nullptr), "xrEnumerateSwapchainFormats")) {
		return false;
	}
	std::vector<int64_t> formats(count);
	if (!Check(xrEnumerateSwapchainFormats(g.Session, count, &count, formats.data()), "xrEnumerateSwapchainFormats")) {
		return false;
	}

	/*
	** The game's pixels are already gamma-encoded. Labelling them as sRGB makes the
	** compositor decode them correctly; a UNORM format would look washed out.
	*/
	struct Candidate { DXGI_FORMAT Format; bool Swizzle; };
	const Candidate preferred[] = {
		{ DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, false },
		{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, true },
		{ DXGI_FORMAT_B8G8R8A8_UNORM, false },
		{ DXGI_FORMAT_R8G8B8A8_UNORM, true },
	};
	for (const Candidate & c : preferred) {
		for (int64_t f : formats) {
			if (f == c.Format) {
				g.SwapchainFormat = c.Format;
				g.SwizzleRB = c.Swizzle;
				Log("[RenegadeVR] Swapchain format %d%s", (int)c.Format, c.Swizzle ? " (R/B swizzle)" : "");
				return true;
			}
		}
	}
	Log("[RenegadeVR] Runtime offers no 8-bit BGRA/RGBA swapchain format");
	return false;
}

bool Create_Swapchains(float render_scale)
{
	uint32_t count = 0;
	XrViewConfigurationView config[RVR_EYE_COUNT] = {
		{ XR_TYPE_VIEW_CONFIGURATION_VIEW }, { XR_TYPE_VIEW_CONFIGURATION_VIEW }
	};
	if (!Check(xrEnumerateViewConfigurationViews(g.Instance, g.System, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			RVR_EYE_COUNT, &count, config), "xrEnumerateViewConfigurationViews") || count != RVR_EYE_COUNT) {
		return false;
	}

	// One size for both eyes keeps the game side simple.
	int w = (int)(config[0].recommendedImageRectWidth * render_scale + 0.5f);
	int h = (int)(config[0].recommendedImageRectHeight * render_scale + 0.5f);
	if (w > (int)config[0].maxImageRectWidth) w = (int)config[0].maxImageRectWidth;
	if (h > (int)config[0].maxImageRectHeight) h = (int)config[0].maxImageRectHeight;
	if (w < 64) w = 64;
	if (h < 64) h = 64;
	g.EyeWidth = w;
	g.EyeHeight = h;
	Log("[RenegadeVR] Recommended eye %ux%u, using %dx%d (scale %.2f)",
		config[0].recommendedImageRectWidth, config[0].recommendedImageRectHeight, w, h, render_scale);

	for (int eye = 0; eye < RVR_EYE_COUNT; ++eye) {
		XrSwapchainCreateInfo info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
		info.format = g.SwapchainFormat;
		info.sampleCount = 1;
		info.width = (uint32_t)w;
		info.height = (uint32_t)h;
		info.faceCount = 1;
		info.arraySize = 1;
		info.mipCount = 1;
		if (!Check(xrCreateSwapchain(g.Session, &info, &g.Eyes[eye].Handle), "xrCreateSwapchain")) {
			return false;
		}

		uint32_t images = 0;
		xrEnumerateSwapchainImages(g.Eyes[eye].Handle, 0, &images, nullptr);
		g.Eyes[eye].Images.assign(images, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
		if (!Check(xrEnumerateSwapchainImages(g.Eyes[eye].Handle, images, &images,
				reinterpret_cast<XrSwapchainImageBaseHeader *>(g.Eyes[eye].Images.data())), "xrEnumerateSwapchainImages")) {
			return false;
		}
	}
	return true;
}

/*
** Controllers: one action set, bound for Touch, Index and the generic simple controller.
*/
XrPath Path(const char * text)
{
	XrPath path = XR_NULL_PATH;
	xrStringToPath(g.Instance, text, &path);
	return path;
}

bool Create_Action(XrActionType type, const char * name, const char * localized, XrAction * action)
{
	XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
	info.actionType = type;
	std::snprintf(info.actionName, sizeof(info.actionName), "%s", name);
	std::snprintf(info.localizedActionName, sizeof(info.localizedActionName), "%s", localized);
	info.countSubactionPaths = RVR_HAND_COUNT;
	info.subactionPaths = g.HandPath;
	return Check(xrCreateAction(g.ActionSet, &info, action), name);
}

struct Binding { XrAction * Action; const char * Path; };

void Suggest(const char * profile, const Binding * bindings, int count)
{
	std::vector<XrActionSuggestedBinding> list;
	for (int i = 0; i < count; ++i) {
		XrPath path = Path(bindings[i].Path);
		if (path != XR_NULL_PATH && *bindings[i].Action != XR_NULL_HANDLE) {
			list.push_back({ *bindings[i].Action, path });
		}
	}
	XrInteractionProfileSuggestedBinding suggested = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	suggested.interactionProfile = Path(profile);
	suggested.countSuggestedBindings = (uint32_t)list.size();
	suggested.suggestedBindings = list.data();
	XrResult result = xrSuggestInteractionProfileBindings(g.Instance, &suggested);
	if (!XR_SUCCEEDED(result)) {
		Log("[RenegadeVR] Bindings for %s not accepted (%d)", profile, (int)result);
	}
}

bool Create_Actions(void)
{
	g.HandPath[RVR_HAND_LEFT] = Path("/user/hand/left");
	g.HandPath[RVR_HAND_RIGHT] = Path("/user/hand/right");

	XrActionSetCreateInfo set_info = { XR_TYPE_ACTION_SET_CREATE_INFO };
	std::snprintf(set_info.actionSetName, sizeof(set_info.actionSetName), "%s", "gameplay");
	std::snprintf(set_info.localizedActionSetName, sizeof(set_info.localizedActionSetName), "%s", "Gameplay");
	if (!Check(xrCreateActionSet(g.Instance, &set_info, &g.ActionSet), "xrCreateActionSet")) {
		return false;
	}

	if (!Create_Action(XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Hand", &g.GripPose) ||
		 !Create_Action(XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim", &g.AimPose) ||
		 !Create_Action(XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", &g.Trigger) ||
		 !Create_Action(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Grip", &g.Squeeze) ||
		 !Create_Action(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", &g.Stick) ||
		 !Create_Action(XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click", "Thumbstick click", &g.StickClick) ||
		 !Create_Action(XR_ACTION_TYPE_BOOLEAN_INPUT, "primary", "A / X", &g.Primary) ||
		 !Create_Action(XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary", "B / Y", &g.Secondary) ||
		 !Create_Action(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu", &g.Menu) ||
		 !Create_Action(XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Vibration", &g.Haptic)) {
		return false;
	}

	const Binding touch[] = {
		{ &g.GripPose, "/user/hand/left/input/grip/pose" },		{ &g.GripPose, "/user/hand/right/input/grip/pose" },
		{ &g.AimPose, "/user/hand/left/input/aim/pose" },			{ &g.AimPose, "/user/hand/right/input/aim/pose" },
		{ &g.Trigger, "/user/hand/left/input/trigger/value" },	{ &g.Trigger, "/user/hand/right/input/trigger/value" },
		{ &g.Squeeze, "/user/hand/left/input/squeeze/value" },	{ &g.Squeeze, "/user/hand/right/input/squeeze/value" },
		{ &g.Stick, "/user/hand/left/input/thumbstick" },			{ &g.Stick, "/user/hand/right/input/thumbstick" },
		{ &g.StickClick, "/user/hand/left/input/thumbstick/click" }, { &g.StickClick, "/user/hand/right/input/thumbstick/click" },
		{ &g.Primary, "/user/hand/left/input/x/click" },			{ &g.Primary, "/user/hand/right/input/a/click" },
		{ &g.Secondary, "/user/hand/left/input/y/click" },			{ &g.Secondary, "/user/hand/right/input/b/click" },
		{ &g.Menu, "/user/hand/left/input/menu/click" },
		{ &g.Haptic, "/user/hand/left/output/haptic" },				{ &g.Haptic, "/user/hand/right/output/haptic" },
	};
	Suggest("/interaction_profiles/oculus/touch_controller", touch, ARRAYSIZE(touch));

	const Binding index[] = {
		{ &g.GripPose, "/user/hand/left/input/grip/pose" },		{ &g.GripPose, "/user/hand/right/input/grip/pose" },
		{ &g.AimPose, "/user/hand/left/input/aim/pose" },			{ &g.AimPose, "/user/hand/right/input/aim/pose" },
		{ &g.Trigger, "/user/hand/left/input/trigger/value" },	{ &g.Trigger, "/user/hand/right/input/trigger/value" },
		{ &g.Squeeze, "/user/hand/left/input/squeeze/value" },	{ &g.Squeeze, "/user/hand/right/input/squeeze/value" },
		{ &g.Stick, "/user/hand/left/input/thumbstick" },			{ &g.Stick, "/user/hand/right/input/thumbstick" },
		{ &g.StickClick, "/user/hand/left/input/thumbstick/click" }, { &g.StickClick, "/user/hand/right/input/thumbstick/click" },
		{ &g.Primary, "/user/hand/left/input/a/click" },			{ &g.Primary, "/user/hand/right/input/a/click" },
		{ &g.Secondary, "/user/hand/left/input/b/click" },			{ &g.Secondary, "/user/hand/right/input/b/click" },
		{ &g.Menu, "/user/hand/left/input/system/click" },
		{ &g.Haptic, "/user/hand/left/output/haptic" },				{ &g.Haptic, "/user/hand/right/output/haptic" },
	};
	Suggest("/interaction_profiles/valve/index_controller", index, ARRAYSIZE(index));

	const Binding simple[] = {
		{ &g.GripPose, "/user/hand/left/input/grip/pose" },		{ &g.GripPose, "/user/hand/right/input/grip/pose" },
		{ &g.AimPose, "/user/hand/left/input/aim/pose" },			{ &g.AimPose, "/user/hand/right/input/aim/pose" },
		{ &g.Primary, "/user/hand/left/input/select/click" },		{ &g.Primary, "/user/hand/right/input/select/click" },
		{ &g.Menu, "/user/hand/left/input/menu/click" },
		{ &g.Haptic, "/user/hand/left/output/haptic" },				{ &g.Haptic, "/user/hand/right/output/haptic" },
	};
	Suggest("/interaction_profiles/khr/simple_controller", simple, ARRAYSIZE(simple));

	for (int hand = 0; hand < RVR_HAND_COUNT; ++hand) {
		XrActionSpaceCreateInfo space = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
		space.subactionPath = g.HandPath[hand];
		space.poseInActionSpace.orientation.w = 1.0f;
		space.action = g.GripPose;
		if (!Check(xrCreateActionSpace(g.Session, &space, &g.GripSpace[hand]), "xrCreateActionSpace(grip)")) {
			return false;
		}
		space.action = g.AimPose;
		if (!Check(xrCreateActionSpace(g.Session, &space, &g.AimSpace[hand]), "xrCreateActionSpace(aim)")) {
			return false;
		}
	}

	XrSessionActionSetsAttachInfo attach = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attach.countActionSets = 1;
	attach.actionSets = &g.ActionSet;
	if (!Check(xrAttachSessionActionSets(g.Session, &attach), "xrAttachSessionActionSets")) {
		return false;
	}
	g.ActionsReady = true;
	Log("[RenegadeVR] Controller actions ready");
	return true;
}

void Copy_Pose(const XrPosef & in, RVR_Pose * out)
{
	out->Orientation[0] = in.orientation.x;
	out->Orientation[1] = in.orientation.y;
	out->Orientation[2] = in.orientation.z;
	out->Orientation[3] = in.orientation.w;
	out->Position[0] = in.position.x;
	out->Position[1] = in.position.y;
	out->Position[2] = in.position.z;
}

bool Locate(XrSpace space, XrTime time, RVR_Pose * out)
{
	XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
	if (space == XR_NULL_HANDLE || !XR_SUCCEEDED(xrLocateSpace(space, g.AppSpace, time, &location))) {
		return false;
	}
	if ((location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0) {
		return false;
	}
	Copy_Pose(location.pose, out);
	return true;
}

float Get_Float(XrAction action, int hand)
{
	XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
	info.action = action;
	info.subactionPath = g.HandPath[hand];
	XrActionStateFloat state = { XR_TYPE_ACTION_STATE_FLOAT };
	if (action == XR_NULL_HANDLE || !XR_SUCCEEDED(xrGetActionStateFloat(g.Session, &info, &state)) || !state.isActive) {
		return 0.0f;
	}
	return state.currentState;
}

bool Get_Bool(XrAction action, int hand)
{
	XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
	info.action = action;
	info.subactionPath = g.HandPath[hand];
	XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };
	if (action == XR_NULL_HANDLE || !XR_SUCCEEDED(xrGetActionStateBoolean(g.Session, &info, &state)) || !state.isActive) {
		return false;
	}
	return state.currentState != XR_FALSE;
}

void Read_Controllers(XrTime time, RVR_Frame * frame)
{
	if (!g.ActionsReady || g.SessionState != XR_SESSION_STATE_FOCUSED) {
		return;		// inputs only reach a focused session
	}
	XrActiveActionSet active = { g.ActionSet, XR_NULL_PATH };
	XrActionsSyncInfo sync = { XR_TYPE_ACTIONS_SYNC_INFO };
	sync.countActiveActionSets = 1;
	sync.activeActionSets = &active;
	if (!XR_SUCCEEDED(xrSyncActions(g.Session, &sync))) {
		return;
	}

	for (int hand = 0; hand < RVR_HAND_COUNT; ++hand) {
		RVR_Hand & h = frame->Hand[hand];
		h.Active = Locate(g.GripSpace[hand], time, &h.Grip) ? 1 : 0;
		if (!Locate(g.AimSpace[hand], time, &h.Aim)) {
			h.Aim = h.Grip;
		}
		h.Trigger = Get_Float(g.Trigger, hand);
		h.Squeeze = Get_Float(g.Squeeze, hand);

		XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
		info.action = g.Stick;
		info.subactionPath = g.HandPath[hand];
		XrActionStateVector2f stick = { XR_TYPE_ACTION_STATE_VECTOR2F };
		if (XR_SUCCEEDED(xrGetActionStateVector2f(g.Session, &info, &stick)) && stick.isActive) {
			h.StickX = stick.currentState.x;
			h.StickY = stick.currentState.y;
		}

		h.Buttons = 0;
		if (Get_Bool(g.Primary, hand)) h.Buttons |= RVR_BUTTON_PRIMARY;
		if (Get_Bool(g.Secondary, hand)) h.Buttons |= RVR_BUTTON_SECONDARY;
		if (Get_Bool(g.StickClick, hand)) h.Buttons |= RVR_BUTTON_STICK;
		if (hand == RVR_HAND_LEFT && Get_Bool(g.Menu, hand)) h.Buttons |= RVR_BUTTON_MENU;
	}
}

/*
** API implementation
*/

void RVR_Shutdown(void);

int RVR_Init(float render_scale, const char * log_path)
{
	if (g.Instance != XR_NULL_HANDLE) {
		return 1;
	}
	if (log_path != nullptr) {
		g.Log = std::fopen(log_path, "w");
	}
	if (render_scale < 0.25f) render_scale = 0.25f;
	if (render_scale > 2.0f) render_scale = 2.0f;

	Log("[RenegadeVR] Init (API %d)", RVR_API_VERSION);

	// Is D3D11 supported by the active (32-bit) runtime?
	uint32_t ext_count = 0;
	if (!Check(xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr), "xrEnumerateInstanceExtensionProperties")) {
		Log("[RenegadeVR] No OpenXR runtime available to this 32-bit process");
		RVR_Shutdown();
		return 0;
	}
	std::vector<XrExtensionProperties> exts(ext_count, { XR_TYPE_EXTENSION_PROPERTIES });
	xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, exts.data());
	bool has_d3d11 = false;
	for (const XrExtensionProperties & e : exts) {
		if (std::strcmp(e.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0) {
			has_d3d11 = true;
		}
	}
	if (!has_d3d11) {
		Log("[RenegadeVR] Runtime does not offer %s", XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
		RVR_Shutdown();
		return 0;
	}

	const char * enabled[] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME };
	XrInstanceCreateInfo create = { XR_TYPE_INSTANCE_CREATE_INFO };
	std::snprintf(create.applicationInfo.applicationName, sizeof(create.applicationInfo.applicationName), "%s", "Renegade VR");
	create.applicationInfo.applicationVersion = 1;
	std::snprintf(create.applicationInfo.engineName, sizeof(create.applicationInfo.engineName), "%s", "W3D");
	create.applicationInfo.engineVersion = 1;
	create.applicationInfo.apiVersion = XR_API_VERSION_1_0;	// widest runtime support
	create.enabledExtensionCount = 1;
	create.enabledExtensionNames = enabled;
	if (!Check(xrCreateInstance(&create, &g.Instance), "xrCreateInstance")) {
		RVR_Shutdown();
		return 0;
	}

	XrInstanceProperties props = { XR_TYPE_INSTANCE_PROPERTIES };
	if (XR_SUCCEEDED(xrGetInstanceProperties(g.Instance, &props))) {
		std::snprintf(g.RuntimeName, sizeof(g.RuntimeName), "%s %u.%u.%u", props.runtimeName,
			XR_VERSION_MAJOR(props.runtimeVersion), XR_VERSION_MINOR(props.runtimeVersion), XR_VERSION_PATCH(props.runtimeVersion));
		Log("[RenegadeVR] Runtime: %s", g.RuntimeName);
	}

	XrSystemGetInfo system = { XR_TYPE_SYSTEM_GET_INFO };
	system.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (!Check(xrGetSystem(g.Instance, &system, &g.System), "xrGetSystem (is the headset connected?)")) {
		RVR_Shutdown();
		return 0;
	}

	if (!Create_D3D11_Device()) {
		RVR_Shutdown();
		return 0;
	}

	XrGraphicsBindingD3D11KHR binding = { XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
	binding.device = g.Device;
	XrSessionCreateInfo session = { XR_TYPE_SESSION_CREATE_INFO };
	session.next = &binding;
	session.systemId = g.System;
	if (!Check(xrCreateSession(g.Instance, &session, &g.Session), "xrCreateSession")) {
		RVR_Shutdown();
		return 0;
	}

	XrReferenceSpaceCreateInfo space = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	space.poseInReferenceSpace.orientation.w = 1.0f;
	space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	if (!Check(xrCreateReferenceSpace(g.Session, &space, &g.LocalSpace), "xrCreateReferenceSpace(LOCAL)")) {
		RVR_Shutdown();
		return 0;
	}
	space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	if (!Check(xrCreateReferenceSpace(g.Session, &space, &g.ViewSpace), "xrCreateReferenceSpace(VIEW)")) {
		RVR_Shutdown();
		return 0;
	}
	XrPosef identity = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } };
	if (!Create_App_Space(identity)) {
		RVR_Shutdown();
		return 0;
	}

	if (!Choose_Swapchain_Format() || !Create_Swapchains(render_scale)) {
		RVR_Shutdown();
		return 0;
	}

	if (!Create_Actions()) {
		Log("[RenegadeVR] No controller input; head tracking only");		// not fatal
	}

	Log("[RenegadeVR] Ready");
	return 1;
}

void RVR_Shutdown(void)
{
	for (EyeSwapchain & eye : g.Eyes) {
		if (eye.Handle != XR_NULL_HANDLE) {
			xrDestroySwapchain(eye.Handle);
		}
		eye.Handle = XR_NULL_HANDLE;
		eye.Images.clear();
	}
	for (int hand = 0; hand < RVR_HAND_COUNT; ++hand) {
		if (g.GripSpace[hand] != XR_NULL_HANDLE) xrDestroySpace(g.GripSpace[hand]);
		if (g.AimSpace[hand] != XR_NULL_HANDLE) xrDestroySpace(g.AimSpace[hand]);
		g.GripSpace[hand] = g.AimSpace[hand] = XR_NULL_HANDLE;
	}
	if (g.ActionSet != XR_NULL_HANDLE) xrDestroyActionSet(g.ActionSet);	// destroys its actions too
	g.ActionSet = XR_NULL_HANDLE;
	g.GripPose = g.AimPose = g.Trigger = g.Squeeze = g.Stick = g.StickClick = XR_NULL_HANDLE;
	g.Primary = g.Secondary = g.Menu = g.Haptic = XR_NULL_HANDLE;
	g.ActionsReady = false;
	Destroy_App_Space();
	if (g.ViewSpace != XR_NULL_HANDLE) xrDestroySpace(g.ViewSpace);
	if (g.LocalSpace != XR_NULL_HANDLE) xrDestroySpace(g.LocalSpace);
	g.ViewSpace = g.LocalSpace = XR_NULL_HANDLE;
	if (g.Session != XR_NULL_HANDLE) xrDestroySession(g.Session);
	g.Session = XR_NULL_HANDLE;
	if (g.Context != nullptr) g.Context->Release();
	if (g.Device != nullptr) g.Device->Release();
	g.Context = nullptr;
	g.Device = nullptr;
	if (g.Instance != XR_NULL_HANDLE) xrDestroyInstance(g.Instance);
	g.Instance = XR_NULL_HANDLE;
	g.SessionRunning = false;
	g.FrameBegun = false;

	if (g.Log != nullptr) {
		Log("[RenegadeVR] Shutdown");
		std::fclose(g.Log);
		g.Log = nullptr;
	}
}

void RVR_Get_Eye_Size(int * width, int * height)
{
	if (width != nullptr) *width = g.EyeWidth;
	if (height != nullptr) *height = g.EyeHeight;
}

int RVR_Begin_Frame(RVR_Frame * frame)
{
	if (frame != nullptr) {
		std::memset(frame, 0, sizeof(*frame));
	}
	if (g.Instance == XR_NULL_HANDLE || g.FrameBegun) {
		return 0;
	}

	Poll_Events();
	if (!g.SessionRunning) {
		return 0;
	}

	XrFrameWaitInfo wait = { XR_TYPE_FRAME_WAIT_INFO };
	g.FrameState = { XR_TYPE_FRAME_STATE };
	if (!Check(xrWaitFrame(g.Session, &wait, &g.FrameState), "xrWaitFrame")) {
		return 0;
	}
	XrFrameBeginInfo begin = { XR_TYPE_FRAME_BEGIN_INFO };
	if (!Check(xrBeginFrame(g.Session, &begin), "xrBeginFrame")) {
		return 0;
	}
	g.FrameBegun = true;
	for (EyeSwapchain & eye : g.Eyes) {
		eye.Submitted = false;
	}

	if (g.RecenterPending) {
		Do_Recenter(g.FrameState.predictedDisplayTime);
	}

	XrViewLocateInfo locate = { XR_TYPE_VIEW_LOCATE_INFO };
	locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locate.displayTime = g.FrameState.predictedDisplayTime;
	locate.space = g.AppSpace;
	XrViewState view_state = { XR_TYPE_VIEW_STATE };
	XrView views[RVR_EYE_COUNT] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
	uint32_t view_count = 0;
	bool located = XR_SUCCEEDED(xrLocateViews(g.Session, &locate, &view_state, RVR_EYE_COUNT, &view_count, views))
		&& view_count == RVR_EYE_COUNT
		&& (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
	if (located) {
		// Keep the last good views on tracking loss; End_Frame submits whatever we rendered with.
		g.Views[0] = views[0];
		g.Views[1] = views[1];
	}

	if (frame != nullptr) {
		frame->ShouldRender = (g.FrameState.shouldRender && g.Views[0].type == XR_TYPE_VIEW) ? 1 : 0;
		frame->PoseValid = located ? 1 : 0;
		for (int eye = 0; eye < RVR_EYE_COUNT; ++eye) {
			const XrView & v = g.Views[eye];
			frame->Eye[eye].Orientation[0] = v.pose.orientation.x;
			frame->Eye[eye].Orientation[1] = v.pose.orientation.y;
			frame->Eye[eye].Orientation[2] = v.pose.orientation.z;
			frame->Eye[eye].Orientation[3] = v.pose.orientation.w;
			frame->Eye[eye].Position[0] = v.pose.position.x;
			frame->Eye[eye].Position[1] = v.pose.position.y;
			frame->Eye[eye].Position[2] = v.pose.position.z;
			frame->Fov[eye].TanLeft = std::tan(v.fov.angleLeft);
			frame->Fov[eye].TanRight = std::tan(v.fov.angleRight);
			frame->Fov[eye].TanUp = std::tan(v.fov.angleUp);
			frame->Fov[eye].TanDown = std::tan(v.fov.angleDown);
		}
		if (!Locate(g.ViewSpace, g.FrameState.predictedDisplayTime, &frame->Head)) {
			frame->Head = frame->Eye[0];
		}
		Read_Controllers(g.FrameState.predictedDisplayTime, frame);
	}
	return 1;
}

void RVR_Submit_Eye(int eye, const void * pixels, int pitch, int width, int height)
{
	if (!g.FrameBegun || eye < 0 || eye >= RVR_EYE_COUNT || pixels == nullptr) {
		return;
	}
	if (width != g.EyeWidth || height != g.EyeHeight) {
		Log("[RenegadeVR] Submit_Eye size %dx%d does not match %dx%d", width, height, g.EyeWidth, g.EyeHeight);
		return;
	}

	EyeSwapchain & sc = g.Eyes[eye];
	uint32_t index = 0;
	XrSwapchainImageAcquireInfo acquire = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	if (!Check(xrAcquireSwapchainImage(sc.Handle, &acquire, &index), "xrAcquireSwapchainImage")) {
		return;
	}
	XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	wait.timeout = XR_INFINITE_DURATION;
	if (Check(xrWaitSwapchainImage(sc.Handle, &wait), "xrWaitSwapchainImage")) {
		const void * src = pixels;
		UINT src_pitch = (UINT)pitch;

		if (g.SwizzleRB) {
			g.SwizzleBuffer.resize((size_t)width * height);
			for (int y = 0; y < height; ++y) {
				const unsigned int * in = reinterpret_cast<const unsigned int *>(static_cast<const char *>(pixels) + (size_t)y * pitch);
				unsigned int * out = &g.SwizzleBuffer[(size_t)y * width];
				for (int x = 0; x < width; ++x) {
					unsigned int p = in[x];
					out[x] = (p & 0xff00ff00u) | ((p & 0x00ff0000u) >> 16) | ((p & 0x000000ffu) << 16);
				}
			}
			src = g.SwizzleBuffer.data();
			src_pitch = (UINT)width * 4;
		}
		g.Context->UpdateSubresource(sc.Images[index].texture, 0, nullptr, src, src_pitch, 0);
		sc.Submitted = true;
	}
	XrSwapchainImageReleaseInfo release = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
	Check(xrReleaseSwapchainImage(sc.Handle, &release), "xrReleaseSwapchainImage");
}

void RVR_End_Frame(void)
{
	if (!g.FrameBegun) {
		return;
	}
	g.FrameBegun = false;

	XrCompositionLayerProjectionView views[RVR_EYE_COUNT];
	XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	const XrCompositionLayerBaseHeader * layers[1] = { reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer) };

	bool submit = g.FrameState.shouldRender && g.Eyes[0].Submitted && g.Eyes[1].Submitted;
	if (submit) {
		for (int eye = 0; eye < RVR_EYE_COUNT; ++eye) {
			views[eye] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
			views[eye].pose = g.Views[eye].pose;
			views[eye].fov = g.Views[eye].fov;
			views[eye].subImage.swapchain = g.Eyes[eye].Handle;
			views[eye].subImage.imageRect.offset = { 0, 0 };
			views[eye].subImage.imageRect.extent = { g.EyeWidth, g.EyeHeight };
			views[eye].subImage.imageArrayIndex = 0;
		}
		layer.space = g.AppSpace;
		layer.viewCount = RVR_EYE_COUNT;
		layer.views = views;
	}

	XrFrameEndInfo end = { XR_TYPE_FRAME_END_INFO };
	end.displayTime = g.FrameState.predictedDisplayTime;
	end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	end.layerCount = submit ? 1 : 0;
	end.layers = submit ? layers : nullptr;
	Check(xrEndFrame(g.Session, &end), "xrEndFrame");
}

void RVR_Recenter(void)
{
	g.RecenterPending = true;
}

int RVR_Is_Session_Focused(void)
{
	return (g.SessionRunning && g.SessionState == XR_SESSION_STATE_FOCUSED) ? 1 : 0;
}

const char * RVR_Get_Runtime_Name(void)
{
	return g.RuntimeName;
}

void RVR_Vibrate(int hand, float amplitude, float seconds)
{
	if (!g.ActionsReady || hand < 0 || hand >= RVR_HAND_COUNT || seconds <= 0.0f) {
		return;
	}
	XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };
	vibration.amplitude = (amplitude < 0.0f) ? 0.0f : ((amplitude > 1.0f) ? 1.0f : amplitude);
	vibration.duration = (XrDuration)(seconds * 1.0e9f);
	vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
	XrHapticActionInfo info = { XR_TYPE_HAPTIC_ACTION_INFO };
	info.action = g.Haptic;
	info.subactionPath = g.HandPath[hand];
	xrApplyHapticFeedback(g.Session, &info, reinterpret_cast<XrHapticBaseHeader *>(&vibration));
}

const RVR_API Api = {
	RVR_API_VERSION,
	RVR_Init,
	RVR_Shutdown,
	RVR_Get_Eye_Size,
	RVR_Begin_Frame,
	RVR_Submit_Eye,
	RVR_End_Frame,
	RVR_Recenter,
	RVR_Is_Session_Focused,
	RVR_Get_Runtime_Name,
	RVR_Vibrate,
};

} // namespace

extern "C" __declspec(dllexport) const RVR_API * RVR_Get_API(int requested_version)
{
	return (requested_version == RVR_API_VERSION) ? &Api : nullptr;
}
