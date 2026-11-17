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
** RENEGADE_VR test harness. See vrsim.h.
**
** Poses are in tracking space: metres, +Y up, -Z forward, the head at the origin
** when standing straight (the recenter point).
*/

#include "vrsim.h"
#include "vrmanager.h"
#include "vrplayer.h"
#include "mainloop.h"
#include "dialogmgr.h"
#include "savegame.h"
#include "objectives.h"
#include "c4.h"
#include "gameobjmanager.h"
#include "combat.h"
#include "ccamera.h"
#include "soldier.h"
#include "humanphys.h"
#include "weapons.h"
#include "weaponview.h"
#include "weaponbag.h"
#include "weaponmanager.h"
#include "vehicle.h"
#include "vehiclephys.h"
#include "objlibrary.h"
#include "transition.h"
#include "playertype.h"
#include "definitionmgr.h"
#include "combatchunkid.h"
#include "matrix3d.h"
#include "quat.h"
#include "wwmath.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

enum {
	PHASE_WAIT = 0,
	PHASE_NEUTRAL,
	PHASE_WRIST,
	PHASE_AIM,
	PHASE_FIRE,
	PHASE_EMPTY,
	PHASE_BELT,
	PHASE_INSERT,
	PHASE_AFTER,
	PHASE_TURN,
	PHASE_WALK,
	PHASE_END,
	PHASE_DONE
};

static char		_Scenario[32]		= "";
static FILE *	_Log					= NULL;
static int		_Phase				= PHASE_WAIT;
static float	_PhaseTime			= 0.0f;
static float	_GameTime			= 0.0f;
static float	_NextLog				= 0.0f;
static bool		_ShotTaken			= false;
static int		_RoundsAtStart		= 0;
static float	_HeadingAtStart	= 0.0f;
static Vector3	_PositionAtStart(0, 0, 0);
static Vector3	_WalkDirection(0, 0, 0);
static bool		_SawEmpty			= false;


static void Sim_Log(const char * format, ...)
{
	if (_Log == NULL) {
		return;
	}
	va_list args;
	va_start(args, format);
	fprintf(_Log, "[%6.2f] ", _GameTime);
	vfprintf(_Log, format, args);
	va_end(args);
	fflush(_Log);
}


bool VRSimClass::Command_Line_Scenario(char * buffer, int size)
{
	const char * arg = strstr(::GetCommandLineA(), "VRSIM=");
	if (arg == NULL) {
		return false;
	}
	arg += 6;
	int n = 0;
	while (arg[n] != 0 && arg[n] != ' ' && arg[n] != '\t' && n < size - 1) {
		buffer[n] = arg[n];
		n++;
	}
	buffer[n] = 0;
	return n > 0;
}


void VRSimClass::Init(const char * scenario)
{
	strncpy(_Scenario, scenario, sizeof(_Scenario) - 1);
	char path[MAX_PATH];
	VRManagerClass::Get_Exe_Relative_Path("vr_sim.log", path, sizeof(path));
	_Log = fopen(path, "w");
	_Phase = PHASE_WAIT;
	_PhaseTime = 0.0f;
	_GameTime = 0.0f;
	Sim_Log("VR simulator, scenario '%s'\n", _Scenario);
}


void VRSimClass::Shutdown(void)
{
	Sim_Log("simulator shut down in phase %d\n", _Phase);
	if (_Log != NULL) {
		fclose(_Log);
		_Log = NULL;
	}
}


/*
** Pose helpers
*/
static void Set_Pose(RVR_Pose * pose, const Vector3 & pos, const Vector3 & x, const Vector3 & y, const Vector3 & z)
{
	Matrix3D m(true);
	for (int r = 0; r < 3; r++) {
		m[r][0] = x[r];
		m[r][1] = y[r];
		m[r][2] = z[r];
	}
	Quaternion q = Build_Quaternion(m);
	pose->Orientation[0] = q.X;
	pose->Orientation[1] = q.Y;
	pose->Orientation[2] = q.Z;
	pose->Orientation[3] = q.W;
	pose->Position[0] = pos.X;
	pose->Position[1] = pos.Y;
	pose->Position[2] = pos.Z;
}

static void Look_Pose(RVR_Pose * pose, const Vector3 & pos, const Vector3 & forward)
{
	Vector3 z = -forward;
	z.Normalize();
	Vector3 x;
	Vector3::Cross_Product(Vector3(0.0f, 1.0f, 0.0f), z, &x);
	if (x.Length2() < 0.0001f) {
		x.Set(1.0f, 0.0f, 0.0f);
	}
	x.Normalize();
	Vector3 y;
	Vector3::Cross_Product(z, x, &y);
	Set_Pose(pose, pos, x, y, z);
}

static Vector3 Direction(float yaw_degrees, float pitch_degrees)
{
	float yaw = DEG_TO_RADF(yaw_degrees), pitch = DEG_TO_RADF(pitch_degrees);
	return Vector3(-sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch));
}

static Vector3 Lerp3(const Vector3 & a, const Vector3 & b, float t)
{
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return a + (b - a) * t;
}

static const Vector3 RIGHT_HAND_POS(0.18f, -0.32f, -0.38f);
static const Vector3 LEFT_REST_POS(-0.22f, -0.55f, -0.15f);
static const Vector3 LEFT_BELT_POS(-0.22f, -0.62f, -0.05f);


static void Shot(const char * name, float at)
{
	if (!_ShotTaken && _PhaseTime >= at) {
		_ShotTaken = true;
		VRManagerClass::Request_Screenshot(name);
		Sim_Log("screenshot %s\n", name);
	}
}

static void Next_Phase(void)
{
	_Phase++;
	_PhaseTime = 0.0f;
	_ShotTaken = false;
	_NextLog = 0.0f;
}

static WeaponClass * Star_Weapon(void)
{
	return (COMBAT_STAR != NULL) ? COMBAT_STAR->Get_Weapon() : NULL;
}

static void Log_Weapon(const char * what)
{
	WeaponClass * w = Star_Weapon();
	if (w == NULL) {
		Sim_Log("%s: no weapon\n", what);
		return;
	}
	Sim_Log("%s: %s clip %d inventory %d reloading %d fired %d holding magazine %d\n", what, w->Get_Name(),
		w->Get_Clip_Rounds(), w->Get_Inventory_Rounds(), w->Is_Reloading(), w->Get_Total_Rounds_Fired(),
		VRPlayerClass::Is_Holding_Magazine());
}


static void Test_Interaction(float dt,RVR_Hand &left,RVR_Hand &right)
{
    enum { WARMUP, WHEEL_IDLE, WHEEL_OPEN, WHEEL_SECOND, WHEEL_SELECT,
           VEHICLE_SETUP, VEHICLE_ENTER, VEHICLE_DRIVE, VEHICLE_EXIT,
           LADDER_SETUP, LADDER_IDLE, LADDER_ENTER, LADDER_CLIMB, DONE };
    static int step = WARMUP;
    static float time = 0;
    static VehicleGameObj *vehicle = NULL;
    static int starting_weapon = 0, starting_shots = 0;
    static float ladder_z = 0.f;
    static Vector3 parked;
    static Vector3 parked_forward;
    static bool shot = false, entered_vehicle = false;
    SoldierGameObj *star = COMBAT_STAR;
    time += dt;
    const auto next = [&]() { ++step; time = 0; shot = false; };
    const auto capture = [&](const char *name) {
        if (!shot) { VRManagerClass::Request_Screenshot(name); shot = true; }
    };
    if (step == WARMUP && time >= 1.f) {
        WeaponBagClass *bag = star->Get_Weapon_Bag();
        for (DefinitionClass *d = DefinitionMgrClass::Get_First(CLASSID_DEF_WEAPON); d && bag->Get_Count() < 13;
             d = DefinitionMgrClass::Get_Next(d,CLASSID_DEF_WEAPON)) {
            const auto *weapon = static_cast<const WeaponDefinitionClass*>(d);
            if (!weapon->FirstPersonModel.Is_Empty() && int(weapon->ClipSize) > 0)
                bag->Add_Weapon(weapon,30);
        }
        if (bag->Get_Count() > 1) {
            bag->Select_Weapon(bag->Peek_Weapon(1));
            starting_weapon = Star_Weapon()->Get_ID();
            starting_shots = Star_Weapon()->Get_Total_Rounds_Fired();
        }
        Sim_Log("wheel test inventory %d starting weapon %d\n",bag->Get_Count()-1,starting_weapon);
        next();
    } else if (step == WHEEL_IDLE && time >= 3.f) {
        next();
    } else if (step == WHEEL_OPEN) {
        right.Squeeze = 1.f;
        if (time >= .75f) capture("wheel_first");
        if (time >= 2.5f) next();
    } else if (step == WHEEL_SECOND) {
        right.Squeeze = 1.f;
        right.Trigger = time < .3f ? 1.f : 0.f;
        right.StickY = 1.f;
        if (time >= .75f) capture("wheel_second");
        if (time >= 2.5f) next();
    } else if (step == WHEEL_SELECT) {
        right.StickY = time < .2f ? 1.f : 0.f;
        if (time >= .7f) {
            WeaponClass *first = star->Get_Weapon_Bag()->Peek_Weapon(1);
            const bool changed = Star_Weapon() && Star_Weapon()->Get_ID()!=starting_weapon;
            const bool blocked = first && first->Get_Total_Rounds_Fired()==starting_shots;
            Sim_Log("WHEEL selection changed=%d page trigger blocked=%d %s\n",
                changed,blocked,changed && blocked ? "PASS" : "FAIL");
            next();
        }
    } else if (step == VEHICLE_SETUP) {
        const uint32 type = CLASSID_GAME_OBJECT_DEF_VEHICLE;
        const char *preset = NULL;
        for (DefinitionClass *d=DefinitionMgrClass::Get_First(type);d;d=DefinitionMgrClass::Get_Next(d,type)) {
            const char *name = d->Get_Name();
            if (!strstr(name,"GDI") || !strstr(name,"Medium")) continue;
            const auto &transitions = static_cast<const VehicleGameObjDef*>(d)->Get_Transition_List();
            bool enter=false, exit=false;
            for (int i=0;i<transitions.Count();++i) {
                enter |= transitions[i]->Get_Type()==TransitionDataClass::VEHICLE_ENTER;
                exit |= transitions[i]->Get_Type()==TransitionDataClass::VEHICLE_EXIT;
            }
            if (!enter || !exit) continue;
            PhysicalGameObj *object = ObjectLibraryManager::Create_Object(d->Get_ID());
            if (object && object->As_VehicleGameObj()) {
                vehicle = object->As_VehicleGameObj();
                preset = name;
                break;
            }
        }
        if (vehicle) {
            Vector3 pos; star->Get_Position(&pos);
            Matrix3D tm = star->Get_Transform();
            tm.Set_Translation(pos+Vector3(0,0,2.0f)+tm.Get_X_Vector()*10.f+tm.Get_Y_Vector()*3.f);
            vehicle->Set_Transform(tm);
            vehicle->Set_Player_Type(star->Get_Player_Type());
            vehicle->Start_Observers();
            Sim_Log("vehicle fixture %s\n",preset);
        } else Sim_Log("VEHICLE no suitable fixture\n");
        next();
    } else if (step == VEHICLE_ENTER) {
        if (vehicle) {
            if (time >= 1.05f && !shot) {
                const auto &entries = vehicle->Get_Definition().Get_Transition_List();
                Sim_Log("vehicle entries=%d permitted=%d upright=%d\n",
                    entries.Count(),vehicle->Is_Entry_Permitted(star),star->Is_Upright());
                for (int i=0;i<entries.Count();++i) {
                    auto *entry = entries[i];
                    if (entry->Get_Type()!=TransitionDataClass::VEHICLE_ENTER) continue;
                    const Vector3 pos=vehicle->Get_Transform()*entry->Get_Zone().Center;
                    star->Set_Position(pos);
                    star->Peek_Human_Phys()->Set_Velocity(Vector3(0,0,0));
                    Sim_Log("vehicle at entry %.2f %.2f %.2f\n",pos.X,pos.Y,pos.Z);
                    break;
                }
                shot = true;
            }
            if (time >= 1.05f && time < 1.25f) left.Buttons |= RVR_BUTTON_PRIMARY;
        }
        if (time >= 1.8f) {
            entered_vehicle = vehicle && vehicle->Get_Driver()==star;
            Sim_Log("VEHICLE enter via left primary %s\n",entered_vehicle ? "PASS" : "FAIL");
            if (vehicle) {
                vehicle->Get_Position(&parked);
                parked_forward=vehicle->Get_Transform().Get_X_Vector();
            }
            next();
        }
    } else if (step == VEHICLE_DRIVE) {
        if (vehicle && star->Get_Vehicle()==vehicle) {
            left.StickY = .9f;
            left.StickX = time>.55f ? .6f : 0.f;
            Look_Pose(&right.Grip,RIGHT_HAND_POS,Direction(20.f,0.f));
            if (time > .75f) right.Trigger = 1.f;
            if (time > 1.f) capture("vehicle_driving");
        }
        if (time >= 2.f) {
            if (vehicle) {
                Vector3 pos; vehicle->Get_Position(&pos);
                const Vector3 offset = pos-parked;
                const float turn = RAD_TO_DEGF(acosf(WWMath::Clamp(Vector3::Dot_Product(
                    parked_forward,vehicle->Get_Transform().Get_X_Vector()),-1.f,1.f)));
                Sim_Log("VEHICLE drive %.2f m horizontal, turn %.1f deg, shots %d, engine %d\n",
                    sqrtf(offset.X*offset.X+offset.Y*offset.Y),turn,
                    vehicle->Get_Weapon()?vehicle->Get_Weapon()->Get_Total_Rounds_Fired():0,vehicle->Is_Engine_Enabled());
                auto &control=vehicle->Get_Control();
                Sim_Log("VEHICLE control forward %.2f turn %.2f; start Z %.2f end Z %.2f\n",
                    control.Get_Analog(ControlClass::ANALOG_MOVE_FORWARD),control.Get_Analog(ControlClass::ANALOG_TURN_LEFT),
                    parked.Z,pos.Z);
                if (auto *body=vehicle->Peek_Vehicle_Phys()) {
                    Sim_Log("VEHICLE physical dt %.4f sim %d immovable %d asleep %d wheels %d throttle %.2f\n",
                        body->Get_Last_Timestep(),body->Is_Object_Simulating(),body->Is_Immovable(),body->Is_Asleep(),
                        body->Get_Wheel_Count(),body->Get_Controller()?body->Get_Controller()->Get_Move_Forward():-9.f);
                }
            }
            next();
        }
    } else if (step == VEHICLE_EXIT) {
        if (time >= .4f && time < .65f) right.Buttons |= RVR_BUTTON_STICK;
        if (time >= 1.2f) {
            Sim_Log("VEHICLE exit via right stick click %s\n",
                entered_vehicle && star->Get_Vehicle()==NULL && vehicle->Get_Driver()!=star ? "PASS" : "FAIL");
            next();
        }
    } else if (step == LADDER_SETUP) {
        DynamicVectorClass<TransitionInstanceClass*> ladders;
        TransitionManager::Build_Ladder_List(ladders);
        TransitionInstanceClass *entry = NULL;
        for (int i=0;i<ladders.Count();++i) {
            if (ladders[i]->Get_Type()!=TransitionDataClass::LADDER_ENTER_BOTTOM) continue;
            for (int j=0;j<ladders.Count();++j) {
                if (ladders[j]->Get_Type()==TransitionDataClass::LADDER_EXIT_TOP &&
                    ladders[j]->Get_Ladder_Index()==ladders[i]->Get_Ladder_Index() &&
                    ladders[j]->Get_Zone().Center.Z-ladders[i]->Get_Zone().Center.Z>1.5f) {
                    entry=ladders[i]; break;
                }
            }
            if (entry) break;
        }
        if (!entry || star->Get_Vehicle()) {
            Sim_Log("LADDER no usable native entry (%d transitions, vehicle=%d)\n",ladders.Count(),star->Get_Vehicle()!=NULL);
            step=DONE;
        } else {
            star->Set_Position(entry->Get_Zone().Center);
            Sim_Log("LADDER staged at native bottom entry, index %d\n",entry->Get_Ladder_Index());
            next();
        }
    } else if (step == LADDER_IDLE && time >= .45f) {
        Sim_Log("LADDER mounts without an input %s\n",star->Is_On_Ladder() ? "yes" : "no");
        next();
    } else if (step == LADDER_ENTER) {
        if (time < .2f) left.Buttons |= RVR_BUTTON_PRIMARY;
        if (time >= .45f) {
            Vector3 pos; star->Get_Position(&pos); ladder_z=pos.Z;
            Sim_Log("LADDER enter via left primary %s\n",star->Is_On_Ladder() ? "PASS" : "FAIL");
            next();
        }
    } else if (step == LADDER_CLIMB) {
        left.StickY = 1.f;
        if (time > .55f) capture("ladder_climbing");
        if (time >= 3.5f) {
            Vector3 pos; star->Get_Position(&pos);
            Sim_Log("LADDER climb %.2f m, still mounted %d\n",pos.Z-ladder_z,star->Is_On_Ladder());
            next();
        }
    } else if (step == DONE) {
        Sim_Log("interaction audit finished\n");
        Stop_Main_Loop(0);
    }
}

static void Test_VR_Controls(float dt, bool in_game, bool ready, RVR_Hand &left, RVR_Hand &right, Vector3 &head_forward)
{
    enum Step { START, HEAD_ONLY, OPEN_MENU, PAUSED, RESUME, OPEN_SAVE, AIM_SAVE, SAVE,
                OPEN_LOAD, AIM_LOAD, LOAD, VERIFY_LOAD };
    static Step step=START;
    static float age=0.f;
    static float heading=0.f;
    static int saved_clip=0;
    static bool shot=false;
    const char *slot="save\\quicksaveA.sav";
    const auto next=[&](Step value) { step=value; age=0.f; shot=false; };
    const auto capture=[&](const char *name) {
        if (!shot) {
            VRManagerClass::Request_Screenshot(name);
            shot=true;
        }
    };

    age+=dt;
    _GameTime+=dt;
    if (age>15.f) {
        Sim_Log("VR controls timed out in step %d FAIL\n", step);
        Stop_Main_Loop(1);
        return;
    }

    switch (step) {
        case START:
            if (ready && !DialogMgrClass::Get_Active_Dialog() && age>2.f) {
                heading=COMBAT_CAMERA->Get_Heading();
                capture("forward");
                next(HEAD_ONLY);
            }
            break;
        case HEAD_ONLY:
            head_forward=age<1.f ? Direction(135.f,0.f) : Direction(55.f,45.f);
            if (age>.6f && age<1.f) capture("head_yaw");
            if (age>=1.f && age<1.2f) shot=false;
            if (age>1.5f) capture("head_pitch");
            if (age>2.f) {
                Sim_Log("Head-only turns kept body heading %s\n",
                    fabsf(COMBAT_CAMERA->Get_Heading()-heading)<.001f ? "PASS" : "FAIL");
                next(OPEN_MENU);
            }
            break;
        case OPEN_MENU:
            if (in_game) left.Buttons|=RVR_BUTTON_STICK;
            if (DialogMgrClass::Get_Active_Dialog()) next(PAUSED);
            break;
        case PAUSED:
            if (age>.4f) capture("pause_panel");
            if (age>.9f) {
                Sim_Log("Left stick click opened pause panel PASS\n");
                next(RESUME);
            }
            break;
        case RESUME:
            if (DialogMgrClass::Get_Active_Dialog()) left.Buttons|=RVR_BUTTON_STICK;
            if (ready && !DialogMgrClass::Get_Active_Dialog()) {
                Sim_Log("Left stick click resumed combat PASS\n");
                next(OPEN_SAVE);
            }
            break;
        case OPEN_SAVE:
        case OPEN_LOAD:
            if (age>.2f && ready) right.Squeeze=1.f;
            if (age>.6f && ready) next(step==OPEN_SAVE ? AIM_SAVE : AIM_LOAD);
            break;
        case AIM_SAVE:
        case AIM_LOAD:
            right.Squeeze=1.f;
            left.Trigger=age<.2f ? 1.f : 0.f;
            right.StickY=step==AIM_SAVE ? 1.f : -1.f;
            if (age>.3f) capture(step==AIM_SAVE ? "quick_actions_save" : "quick_actions_load");
            if (age>.8f) {
                if (step==AIM_SAVE && Star_Weapon()) saved_clip=Star_Weapon()->Get_Clip_Rounds();
                next(step==AIM_SAVE ? SAVE : LOAD);
            }
            break;
        case SAVE:
            right.StickY=1.f;
            if (age>.4f) {
                StringClass map;
                const bool saved=SaveGameManager::Peek_Map_Name(slot,map);
                Sim_Log("Wheel Quicksave made native slot (clip %d) %s\n",saved_clip,saved ? "PASS" : "FAIL");
                if (!saved || !Star_Weapon() || saved_clip<=0) {
                    Stop_Main_Loop(1);
                    return;
                }
                Star_Weapon()->Set_Clip_Rounds(saved_clip-1);
                next(OPEN_LOAD);
            }
            break;
        case LOAD:
            right.StickY=-1.f;
            next(VERIFY_LOAD);
            break;
        case VERIFY_LOAD:
            if (age>2.f && ready) {
                const int clip=Star_Weapon() ? Star_Weapon()->Get_Clip_Rounds() : -1;
                Sim_Log("Wheel Quickload restored clip %d (expected %d) %s\n",
                    clip,saved_clip,clip==saved_clip ? "PASS" : "FAIL");
                Stop_Main_Loop(clip==saved_clip ? 0 : 1);
            }
            break;
    }
}

void VRSimClass::Fill_Frame(RVR_Frame * frame, float dt, bool in_game)
{
	/*
	** Defaults: standing, looking ahead, gun forward, left hand down
	*/
	frame->ShouldRender = 1;
	frame->PoseValid = 1;
	RVR_Hand & left = frame->Hand[RVR_HAND_LEFT];
	RVR_Hand & right = frame->Hand[RVR_HAND_RIGHT];
	left.Active = right.Active = 1;
	Vector3 head_forward(0.0f, 0.0f, -1.0f);
	Look_Pose(&right.Grip, RIGHT_HAND_POS, Vector3(0.0f, 0.0f, -1.0f));
	Look_Pose(&left.Grip, LEFT_REST_POS, Direction(0.0f, -60.0f));

	bool run = in_game && COMBAT_STAR != NULL && COMBAT_CAMERA != NULL && !COMBAT_CAMERA->Is_Using_Host_Model();

    if (run && !stricmp(_Scenario,"c4eva")) {
        static float time=0;
        static WeaponClass *remote=NULL;
        static bool placed_c4=false;
        static int captured=-1;
        _Phase=PHASE_DONE;
        time+=dt;
        if (!remote) {
            for (DefinitionClass *def=DefinitionMgrClass::Get_First(CLASSID_DEF_WEAPON);def;
                 def=DefinitionMgrClass::Get_Next(def,CLASSID_DEF_WEAPON)) {
                const auto *weapon=static_cast<const WeaponDefinitionClass*>(def);
                StringClass model=weapon->FirstPersonModel;
                _strlwr(&model[0]);
                if (strstr(model,"f_gm_c4-r")) {
                    WeaponBagClass *bag=COMBAT_STAR->Get_Weapon_Bag();
                    remote=bag->Add_Weapon(weapon,12);
                    bag->Select_Weapon(remote);
                    Sim_Log("C4 fixture %s\n",weapon->Get_Name());
                    break;
                }
            }
            if (!remote) Stop_Main_Loop(1);
        }

        const int pose=time<3.f ? 0 : 1;
        Vector3 left_hand=pose ? Vector3(.04f,-.24f,-.41f) : Vector3(-.24f,-.24f,-.39f);
        Look_Pose(&left.Grip,left_hand,pose ? Direction(-75,-10) : Direction(-10,-18));
        Look_Pose(&right.Grip,Vector3(.24f,-.26f,-.52f),Direction(0,-12));
        head_forward=Vector3(-.07f,-.49f,-1.f);
        head_forward.Normalize();
        if (time>2.f && captured!=pose && (pose==0 || time>4.f)) {
            captured=pose;
            WeaponViewClass::Log_VR_Weapon_Audit();
            VRManagerClass::Request_Screenshot(pose ? "c4_wrist_across" : "c4_wrist_forward");
        }
        if (time>4.7f && time<5.1f) right.Trigger=1.f;
        if (time>5.5f) {
            int charges=0;
            for (auto *node=GameObjManager::Get_Game_Obj_List()->Head();node;node=node->Next()) {
                auto *physical=node->Data()->As_PhysicalGameObj();
                C4GameObj *c4=physical ? physical->As_C4GameObj() : NULL;
                if (c4 && c4->Get_Owner()==COMBAT_STAR) ++charges;
            }
            if (time<6.f && charges>0) placed_c4=true;
            if (time>6.f && time<6.2f) left.Trigger=1.f;
            if (time>7.f) {
                const bool detonated=placed_c4 && !charges;
                Sim_Log("C4 right trigger placed=%d, left trigger detonated=%d\n",placed_c4,detonated);
                Stop_Main_Loop(detonated ? 0 : 1);
            }
        }
    }

    if (run && !stricmp(_Scenario,"evacompass")) {
        const int objective=9901;
        static bool placed=false;
        static int capture=-1;
        _Phase=PHASE_DONE;
        _GameTime+=dt;
        if (!placed) {
            ObjectiveManager::Add_Objective(objective,ObjectiveManager::TYPE_PRIMARY,
                ObjectiveManager::STATUS_IS_PENDING,0,0,NULL);
            const Matrix3D base=VRManagerClass::Get_Tracking_To_World();
            const Vector3 target=base*Vector3(0,0,-60);
            ObjectiveManager::Set_Objective_HUD_Info(objective,999.f,"POG_M01_1_01.TGA",0,target);
            placed=true;
        }
        if (_GameTime>3.f) {
            const int view=int((_GameTime-3.f)/1.0f);
            if (view>=3) Stop_Main_Loop(0);
            else {
                head_forward=Direction(90.f*view,0.f);
                if (capture!=view) {
                    capture=view;
                    const char *names[]={"compass_forward","compass_left_turn","compass_behind"};
                    VRManagerClass::Request_Screenshot(names[view]);
                }
            }
        }
    }

    if (!stricmp(_Scenario,"vrcontrols")) {
        Test_VR_Controls(dt,in_game,run,left,right,head_forward);
        _Phase=PHASE_DONE;
    }

    if (run && !stricmp(_Scenario,"interact")) {
        Test_Interaction(dt,left,right);
        _Phase=PHASE_DONE;
    }

    if(run && !stricmp(_Scenario,"bodylife")) {
        static float clock=0;static int checked=-1;clock+=dt;
        int phase=(int)(clock/2.0f);float t=clock-phase*2.0f;
        if(phase>=5) {Sim_Log("body tracking lifecycle finished\n");Stop_Main_Loop(0);}
        else {
            if(t>1.0f && checked!=phase) {
                checked=phase;bool wanted=phase==0 || phase==4;
                Sim_Log("BODY_LIFECYCLE phase %d visible %d expected %d %s\n",phase,VRPlayerClass::Has_Visible_Body(),wanted,VRPlayerClass::Has_Visible_Body()==wanted?"PASS":"FAIL");
            }
            if(phase==1) left.Active=0;
            if(phase==2) right.Active=0;
            if(phase==3) frame->PoseValid=0;
        }
        _Phase=PHASE_DONE;
    }

    if(run && !stricmp(_Scenario,"bodyik")) {
        static const char *models[]={"c_ag_havoc","c_ag_havocd","c_ag_havocw","c_ag_havocn"};
        static float clock=0;static int previous=-1,shot=-1;clock+=dt;
        int sample=(int)(clock/1.8f),outfit=sample/8,pose=sample%8;
        if(outfit>=4) {Sim_Log("body audit finished 4 outfits x 8 poses\n");Stop_Main_Loop(0);}
        else {
            if(outfit!=previous) {previous=outfit;VRPlayerClass::Set_Test_Body_Model(models[outfit]);Sim_Log("body audit selected %s\n",models[outfit]);}
            if(pose==0) head_forward=Direction(0,-78);
            else if(pose>=1 && pose<=5) {
                Vector3 hand(0.08f,-0.22f,-0.30f),z(-1,0,0),x(0,.59f,.81f),y;Vector3::Cross_Product(z,x,&y);
                Matrix3D roll(true);roll.Rotate_Z(DEG_TO_RADF(pose==2?40.0f:pose==3?-40.0f:0.0f));
                Vector3 xx=x*roll[0][0]+y*roll[1][0],yy=x*roll[0][1]+y*roll[1][1];
                if(pose==4 || pose==5) {
                    float angle=DEG_TO_RADF(pose==4?35.0f:-35.0f);Vector3 zz=z*cosf(angle)+y*sinf(angle);yy=y*cosf(angle)-z*sinf(angle);z=zz;
                }
                Set_Pose(&left.Grip,hand,xx,yy,z);head_forward=hand;head_forward.Normalize();
            } else if(pose==6) {
                Look_Pose(&left.Grip,Vector3(-.36f,-.2f,-.35f),Direction(0,-20));head_forward=Direction(0,-30);
            } else {Look_Pose(&right.Grip,RIGHT_HAND_POS,Direction(20,30));head_forward=Direction(0,-20);}
            if(clock-sample*1.8f>1.4f && shot!=sample) {
                shot=sample;char name[64];sprintf(name,"body_%d_pose_%d",outfit,pose);VRManagerClass::Request_Screenshot(name);Sim_Log("body audit capture %s\n",name);
            }
        }
        _Phase=PHASE_DONE;
    }

    if(run && !stricmp(_Scenario,"reloadall")) {
        static const WeaponDefinitionClass *defs[64];static int count=0,current=-1,checked=-1;
        static float clock=0;clock+=dt;
        if(!count) for(DefinitionClass *def=DefinitionMgrClass::Get_First(CLASSID_DEF_WEAPON);def;def=DefinitionMgrClass::Get_Next(def,CLASSID_DEF_WEAPON)) {
            const WeaponDefinitionClass *w=(const WeaponDefinitionClass *)def;
            if(w->FirstPersonModel.Is_Empty() || (int)w->ClipSize<=0 || (int)w->MaxInventoryRounds==0) continue;
            if(strstr(w->FirstPersonModel,"c4-") || strstr(w->FirstPersonModel,"ionb") || strstr(w->FirstPersonModel,"nuke") || strstr(w->FirstPersonModel,"rprg")) continue;
            bool duplicate=false;for(int i=0;i<count;++i) if(!stricmp(defs[i]->FirstPersonModel,w->FirstPersonModel)) duplicate=true;
            if(!duplicate && count<64) defs[count++]=w;
        }
        int next=(int)(clock/12.0f);float t=clock-next*12.0f;
        if(next<count && next!=current) {
            current=next;WeaponClass *w=COMBAT_STAR->Get_Weapon_Bag()->Add_Weapon(defs[current],30);
            COMBAT_STAR->Get_Weapon_Bag()->Select_Weapon(w);w->Set_Clip_Rounds(0);w->Set_Inventory_Rounds(30);
            Sim_Log("reload matrix selected %s clip 0 reserve 30\n",defs[current]->Get_Name());
        }
        if(next>=count) {Sim_Log("reload matrix finished %d weapons\n",count);Stop_Main_Loop(0);}
        else {
            // Move the gun into a side view. Gesture coordinates follow the
            // animation-derived feed/action points in that actual gun pose.
            Look_Pose(&right.Grip,Vector3(0,-0.12f,-1.15f),Direction(90,15));
            head_forward=Vector3(0,-0.12f,-1.15f);head_forward.Normalize();
            Vector3 feed,action;Matrix3D muzzle;
            if(WeaponViewClass::Get_VR_Reload_Points(&feed,&action) && WeaponViewClass::Get_VR_Muzzle(&muzzle)) {
                Vector3 target;
                if(t>=1.2f && t<1.45f && WeaponViewClass::Get_VR_Reload_Kind()==1) {
                    Matrix3D::Inverse_Transform_Vector(VRManagerClass::Get_Tracking_To_World(),feed,&target);Look_Pose(&left.Grip,target,Direction(0,0));left.Squeeze=1;
                } else if(t>=1.65f && t<2.7f) {
                    Vector3 gun;Matrix3D::Inverse_Transform_Vector(VRManagerClass::Get_Tracking_To_World(),feed,&gun);
                    Look_Pose(&left.Grip,Lerp3(LEFT_BELT_POS,gun,(t-2.0f)/0.6f),Direction(0,0));left.Squeeze=1;
                } else if(t>=2.9f && t<3.7f) {
                    float pull=WWMath::Clamp((t-3.1f)*0.25f,0.0f,0.12f);
                    Matrix3D::Inverse_Transform_Vector(VRManagerClass::Get_Tracking_To_World(),action-muzzle.Get_X_Vector()*pull,&target);Look_Pose(&left.Grip,target,Direction(0,0));left.Squeeze=1;
                }
            }
            if(t>=8.0f && checked!=current) {
                checked=current;WeaponClass *w=Star_Weapon();int kind=WeaponViewClass::Get_VR_Reload_Kind();
                int expected=kind==2 ? 1 : min(30,(int)defs[current]->ClipSize);
                Sim_Log("RELOAD_CHECK %s kind %d clip %d expected %d reserve %d expected %d %s\n",defs[current]->Get_Name(),kind,w->Get_Clip_Rounds(),expected,w->Get_Inventory_Rounds(),30-expected,(w->Get_Clip_Rounds()==expected && w->Get_Inventory_Rounds()==30-expected && !WeaponViewClass::Is_VR_Magazine_Removed())?"PASS":"FAIL");
                WeaponViewClass::Log_VR_Weapon_Audit();char name[48];sprintf(name,"reload_matrix_%02d",current);VRManagerClass::Request_Screenshot(name);
            }
            if(t>=8.4f && t<8.55f) right.Trigger=1;
        }
        _Phase=PHASE_DONE;
    }

    // Native all-weapon asset/animation audit, opt-in developer simulator only.
    if(run && (!stricmp(_Scenario,"weapons") || !stricmp(_Scenario,"weaponsides") || !stricmp(_Scenario,"grips"))) {
        static const WeaponDefinitionClass *defs[64];static int count=0,current=-1;
        static float clock=0;clock+=dt;
        if(!count) {
            for(DefinitionClass *def=DefinitionMgrClass::Get_First(CLASSID_DEF_WEAPON);def;def=DefinitionMgrClass::Get_Next(def,CLASSID_DEF_WEAPON)) {
                const WeaponDefinitionClass *w=(const WeaponDefinitionClass *)def;
                if(w->FirstPersonModel.Is_Empty()) continue;
                if(!stricmp(_Scenario,"grips") && !strstr(w->FirstPersonModel,"f_gm_rock") && !strstr(w->FirstPersonModel,"f_gm_chng")) continue;
                bool duplicate=false;for(int i=0;i<count;++i) if(!stricmp(defs[i]->FirstPersonModel,w->FirstPersonModel)) duplicate=true;
                if(!duplicate && count<64) defs[count++]=w;
            }
            Sim_Log("audit unique first-person weapon definitions %d\n",count);
        }
        bool sides=!stricmp(_Scenario,"weaponsides");
        int sample=(int)(clock/1.8f),next=sides ? sample/4 : sample;
        if(sides) {
            Look_Pose(&right.Grip,Vector3(0,-0.12f,-1.15f),Direction((sample%4)*90.0f,15.0f));
            head_forward=Vector3(0,-0.12f,-1.15f);head_forward.Normalize();
        }
        if(!stricmp(_Scenario,"grips")) {
            Look_Pose(&right.Grip,Vector3(0,-0.12f,-1.15f),Direction(90,15));
            head_forward=Vector3(0,-0.12f,-1.15f);head_forward.Normalize();
        }
        if(next<count && next!=current) {
            current=next;WeaponClass *w=COMBAT_STAR->Get_Weapon_Bag()->Add_Weapon(defs[current],999);
            COMBAT_STAR->Get_Weapon_Bag()->Select_Weapon(w);
            Sim_Log("audit selected %s model %s\n",defs[current]->Get_Name(),(const char *)defs[current]->FirstPersonModel);
        }
        static int shot=-1;
        if(current>=0 && current<count && clock-sample*1.8f>1.4f && shot!=sample) {
            shot=sample;WeaponViewClass::Log_VR_Weapon_Audit();char name[48];
            if(sides) sprintf(name,"weapon_%02d_side_%d",current,sample%4);else sprintf(name,"weapon_%02d",current);
            VRManagerClass::Request_Screenshot(name);
        }
        if(next>=count && count>0) {Sim_Log("weapon audit finished\n");Stop_Main_Loop(0);}
        _Phase=PHASE_DONE;
    }

	if (run) {
		_GameTime += dt;
		_PhaseTime += dt;
	}

	/*
	** "arm": the left arm at each ArmRoll, held out ahead and turned to look at the wrist
	*/
	if (stricmp(_Scenario, "arm") == 0 && _Phase == PHASE_NEUTRAL) {
		int step = (int)(_PhaseTime / 1.0f);
		if (step >= 8) {
			Next_Phase();
			_Phase = PHASE_END;
		} else {
			int roll = (step % 4) * 90;
			VRPlayerClass::Set_Arm_Roll((float)roll);
			if (step < 4) {
				Look_Pose(&left.Grip, Vector3(-0.12f, -0.22f, -0.35f), Vector3(0.0f, 0.0f, -1.0f));
				head_forward = Vector3(-0.15f, -0.3f, -0.6f);
			} else {
				Vector3 hand(0.08f, -0.22f, -0.30f);
				Vector3 z(-1.0f, 0.0f, 0.0f);
				Vector3 x(0.0f, 0.59f, 0.81f);
				Vector3 y;
				Vector3::Cross_Product(z, x, &y);
				Set_Pose(&left.Grip, hand, x, y, z);
				head_forward = hand + z * 0.16f;
			}
			head_forward.Normalize();
			float in_step = _PhaseTime - (float)step;
			static int shot_step = -1;
			if (in_step > 0.6f && shot_step != step) {
				shot_step = step;
				char name[32];
				sprintf(name, "arm_%s_%03d", (step < 4) ? "fwd" : "wrist", roll);
				VRManagerClass::Request_Screenshot(name);
			}
		}
	}

	switch (_Phase) {
		case PHASE_WAIT:
			if (run && _PhaseTime > 4.0f) {
				Log_Weapon("start");
				Next_Phase();
			}
			break;

		case PHASE_NEUTRAL:
			if (stricmp(_Scenario, "arm") == 0) {
				break;		// handled above
			}
			Shot("01_neutral", 1.5f);
			if (_PhaseTime > 2.5f) {
				if (stricmp(_Scenario, "idle") == 0) {
					_Phase = PHASE_DONE;		// stay standing
				} else {
					Next_Phase();
				}
			}
			break;

		case PHASE_WRIST:
		{
			if (_PhaseTime < 1.2f) {
				// first the left hand held out in front, pointing ahead like the right one
				Look_Pose(&left.Grip, Vector3(-0.15f, -0.25f, -0.40f), Vector3(0.0f, 0.0f, -1.0f));
				head_forward = Vector3(-0.1f, -0.4f, -1.0f);
				head_forward.Normalize();
				if (_PhaseTime > 1.0f && !_ShotTaken) {
					VRManagerClass::Request_Screenshot("02a_left_forward");
					_ShotTaken = true;
				}
				break;
			}
			if (_PhaseTime < 1.25f) {
				_ShotTaken = false;
			}
			// left forearm across the chest, back of the wrist toward the eyes
			Vector3 hand(0.08f, -0.22f, -0.30f);
			Vector3 z(-1.0f, 0.0f, 0.0f);				// fist points right
			Vector3 x(0.0f, 0.59f, 0.81f);				// back of the hand toward the head
			Vector3 y;
			Vector3::Cross_Product(z, x, &y);
			Set_Pose(&left.Grip, hand, x, y, z);
			Vector3 wrist = hand + z * 0.16f + x * 0.04f;
			head_forward = wrist;
			head_forward.Normalize();
			Shot("02_wrist", 2.7f);
			if (_PhaseTime > 3.7f) {
				Next_Phase();
				if (stricmp(_Scenario, "wrist") == 0) {
					_Phase = PHASE_END;		// short run: just the wrist display
				}
			}
			break;
		}

		case PHASE_AIM:
			Look_Pose(&right.Grip, RIGHT_HAND_POS + Vector3(0.05f, 0.05f, 0.0f), Direction(-20.0f, 3.0f));
			head_forward = Direction(-10.0f, 0.0f);
			Shot("03_aim", 1.0f);
			if (_PhaseTime > 1.5f) {
				if (COMBAT_STAR != NULL) {
					Vector3 t = COMBAT_STAR->Get_Targeting_Pos();
					Sim_Log("aiming 20 deg right: targeting (%.1f %.1f %.1f), soldier heading %.1f deg, camera heading %.1f deg\n",
						t.X, t.Y, t.Z, RAD_TO_DEGF(COMBAT_STAR->Peek_Human_Phys()->Get_Heading()), RAD_TO_DEGF(COMBAT_CAMERA->Get_Heading()));
				}
				WeaponClass * w = Star_Weapon();
				_RoundsAtStart = (w != NULL) ? w->Get_Total_Rounds_Fired() : 0;
				Log_Weapon("before firing");
				Next_Phase();
			}
			break;

		case PHASE_FIRE:
			right.Trigger = (fmodf(_PhaseTime, 0.2f) < 0.1f) ? 1.0f : 0.0f;
			Shot("04_fire", 0.55f);
			if (_PhaseTime > 1.5f) {
				WeaponClass * w = Star_Weapon();
				Sim_Log("fired %d rounds with the trigger\n", (w != NULL) ? w->Get_Total_Rounds_Fired() - _RoundsAtStart : -1);
				_SawEmpty = false;
				Next_Phase();
			}
			break;

		case PHASE_EMPTY:
		{
			WeaponClass * w = Star_Weapon();
			right.Trigger = (fmodf(_PhaseTime, 0.2f) < 0.1f) ? 1.0f : 0.0f;
			if (_PhaseTime >= _NextLog) {
				_NextLog += 1.0f;
				Log_Weapon("emptying");
			}
			if (w != NULL && w->Get_Clip_Rounds() == 0 && !_SawEmpty) {
				_SawEmpty = true;
				_PhaseTime = 20.0f;		// now hold for 2.5 s and check nothing reloads by itself
				_NextLog = 21.0f;
				Log_Weapon("clip empty");
			}
			if (_SawEmpty && _PhaseTime > 22.5f) {
				Log_Weapon("2.5 s later, still pulling the trigger (manual reload: clip must still be 0)");
				VRManagerClass::Request_Screenshot("05_empty");
				Next_Phase();
			} else if (!_SawEmpty && _PhaseTime > 19.0f) {
				Log_Weapon("gave up emptying");
				Next_Phase();
			}
			break;
		}

		case PHASE_BELT:
            Look_Pose(&left.Grip,LEFT_BELT_POS,Direction(0,-30));
            left.Squeeze=_PhaseTime>0.5f ? 1.0f : 0.0f;
            if(_PhaseTime<0.35f) {
                Vector3 feed,action;
                if(WeaponViewClass::Get_VR_Reload_Points(&feed,&action)) {
                    Vector3 tracking;Matrix3D::Inverse_Transform_Vector(VRManagerClass::Get_Tracking_To_World(),feed,&tracking);
                    Look_Pose(&left.Grip,tracking,Direction(0,0));left.Squeeze=_PhaseTime<0.2f ? 1.0f : 0.0f;
                }
            }
			if (_PhaseTime > 0.8f) {
				Log_Weapon("hand at the belt, grip squeezed");
				Next_Phase();
			}
			break;

		case PHASE_INSERT:
		{
			Vector3 target = RIGHT_HAND_POS + Vector3(-0.06f, 0.02f, 0.0f);
            Vector3 feed,action;if(WeaponViewClass::Get_VR_Reload_Points(&feed,&action))
                Matrix3D::Inverse_Transform_Vector(VRManagerClass::Get_Tracking_To_World(),feed,&target);

			Look_Pose(&left.Grip, Lerp3(LEFT_BELT_POS, target, _PhaseTime / 0.6f), Direction(0.0f, 0.0f));
			left.Squeeze = 1.0f;
			Shot("06_reload", 0.7f);
			if (_PhaseTime > 0.9f) {
				Log_Weapon("magazine brought to the gun");
				Next_Phase();
			}
			break;
		}

		case PHASE_AFTER:
            Shot("06b_manual_committed",1.5f);
            if(_PhaseTime>0.2f && _PhaseTime<1.3f) {
                Vector3 feed,action;Matrix3D muzzle;
                if(WeaponViewClass::Get_VR_Reload_Points(&feed,&action) && WeaponViewClass::Get_VR_Muzzle(&muzzle)) {
                    float pull=WWMath::Clamp((_PhaseTime-0.45f)*0.25f,0.0f,0.12f);
                    Vector3 tracking;Matrix3D::Inverse_Transform_Vector(VRManagerClass::Get_Tracking_To_World(),action-muzzle.Get_X_Vector()*pull,&tracking);
                    Look_Pose(&left.Grip,tracking,Direction(0,0));left.Squeeze=1;
                }
            }
			if (_PhaseTime > 5.0f) {
				Log_Weapon("3 s after the reload");
				_HeadingAtStart = COMBAT_CAMERA->Get_Heading();
				Next_Phase();
			}
			break;

		case PHASE_TURN:
			right.StickX = (_PhaseTime < 0.15f) ? 1.0f : 0.0f;
			if (_PhaseTime > 0.5f) {
				Sim_Log("snap turn right: camera heading %.1f -> %.1f deg\n", RAD_TO_DEGF(_HeadingAtStart), RAD_TO_DEGF(COMBAT_CAMERA->Get_Heading()));
				COMBAT_STAR->Get_Position(&_PositionAtStart);
				Matrix3D base = VRManagerClass::Get_Tracking_To_World();
				_WalkDirection = base.Rotate_Vector(Direction(30.0f, 0.0f));
				_WalkDirection.Z = 0.0f;
				_WalkDirection.Normalize();
				Next_Phase();
			}
			break;

		case PHASE_WALK:
			head_forward = Direction(30.0f, 0.0f);		// look 30 degrees left and walk
			left.StickY = 1.0f;
			if (_PhaseTime > 2.0f) {
				Vector3 pos;
				COMBAT_STAR->Get_Position(&pos);
				Vector3 moved = pos - _PositionAtStart;
				moved.Z = 0.0f;
				float dist = moved.Length();
				float agree = (dist > 0.01f) ? Vector3::Dot_Product(moved * (1.0f / dist), _WalkDirection) : 0.0f;
				Sim_Log("walk forward looking 30 deg left: moved %.2f m, direction match %.2f (1 = exactly where the head looks)\n", dist, agree);
				Next_Phase();
			}
			break;

		case PHASE_END:
			Shot("07_end", 0.5f);
			if (_PhaseTime > 1.5f) {
				Sim_Log("tour finished, quitting\n");
				Next_Phase();
				Stop_Main_Loop(0);
			}
			break;

		default:
			break;
	}

	/*
	** Head and eyes
	*/
	Look_Pose(&frame->Head, Vector3(0.0f, 0.0f, 0.0f), head_forward);
    if(strstr(::GetCommandLineA(),"VRBODYVIEW=1")) Look_Pose(&frame->Head,Vector3(0,0,-2),Vector3(0,-0.4f,1));
	right.Aim = right.Grip;
	left.Aim = left.Grip;
	Matrix3D head = Build_Matrix3D(Quaternion(frame->Head.Orientation[0], frame->Head.Orientation[1], frame->Head.Orientation[2], frame->Head.Orientation[3]));
	for (int eye = 0; eye < RVR_EYE_COUNT; eye++) {
		frame->Eye[eye] = frame->Head;
		Vector3 offset = head.Rotate_Vector(Vector3(eye == 0 ? -0.032f : 0.032f, 0.0f, 0.0f));
		frame->Eye[eye].Position[0] += offset.X;
		frame->Eye[eye].Position[1] += offset.Y;
		frame->Eye[eye].Position[2] += offset.Z;
		frame->Fov[eye].TanLeft = -1.0f;
		frame->Fov[eye].TanRight = 1.0f;
		frame->Fov[eye].TanUp = 1.0f;
		frame->Fov[eye].TanDown = -1.0f;
	}
}
