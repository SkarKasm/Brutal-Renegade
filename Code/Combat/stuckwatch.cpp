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
** PORT: stuck detector. Players reported that everyone (player, AI, parachuters) stops moving
** after a while while animations and aiming go on. When the player holds a move key for 3 s
** without moving, this writes what the physics sees to stuck_watch.log next to the exe:
** the player's physics state, what a test move collides with, the objects around, and any
** object in the scene with a broken (non-finite or huge) culling box.
*/

#include "stuckwatch.h"
#include "combat.h"
#include "soldier.h"
#include "humanphys.h"
#include "physcontrol.h"
#include "pscene.h"
#include "physcoltest.h"
#include "physlist.h"
#include "combatphysobserver.h"
#include "physicalgameobj.h"
#include "smartgameobj.h"
#include "gameobjmanager.h"
#include "definition.h"
#include "timemgr.h"
#include "rendobj.h"
#include "castres.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <windows.h>

static FILE *	_Log = NULL;
static float	_SampleTimer = 0.0f;
static Vector3	_LastPos(0, 0, 0);
static float	_StuckTime = 0.0f;
static bool		_Reported = false;

static void Log(const char * format, ...)
{
	if (_Log == NULL) {
		char path[MAX_PATH];
		::GetModuleFileNameA(NULL, path, sizeof(path));
		char * slash = strrchr(path, '\\');
		if (slash != NULL) {
			strcpy(slash + 1, "stuck_watch.log");
			_Log = fopen(path, "a");
		}
		if (_Log == NULL) {
			return;
		}
	}
	va_list args;
	va_start(args, format);
	vfprintf(_Log, format, args);
	va_end(args);
	fflush(_Log);
}

static bool Box_Ok(const AABoxClass & box)
{
	for (int i = 0; i < 3; i++) {
		if (!_finite(box.Center[i]) || !_finite(box.Extent[i]) || box.Extent[i] < 0.0f || box.Extent[i] > 2000.0f) {
			return false;
		}
	}
	return true;
}

static void Describe(const char * prefix, PhysClass * obj)
{
	if (obj == NULL) {
		Log("%s(none)\n", prefix);
		return;
	}
	const char * kind = "Phys";
	if (obj->As_HumanPhysClass())					kind = "Human";
	else if (obj->As_VehiclePhysClass())		kind = "Vehicle";
	else if (obj->As_RenderObjPhysClass())		kind = "RenderObj";
	else if (obj->As_StaticAnimPhysClass())	kind = "StaticAnim";
	else if (obj->As_StaticPhysClass())			kind = "Static";
	else if (obj->As_DecorationPhysClass())	kind = "Decoration";
	else if (obj->As_MoveablePhysClass())		kind = "Moveable";

	const char * def_name = "";
	CombatPhysObserverClass * observer = (CombatPhysObserverClass *)obj->Get_Observer();
	if (observer != NULL && observer->As_PhysicalGameObj() != NULL) {
		def_name = observer->As_PhysicalGameObj()->Get_Definition().Get_Name();
	}
	RenderObjClass * model = obj->Peek_Model();
	const AABoxClass & cb = obj->Get_Cull_Box();
	Log("%s%s id=%d name='%s' def='%s' model='%s' group=%d ignore=%d asleep=%d sim=%d cull c=(%.2f %.2f %.2f) e=(%.2f %.2f %.2f)%s\n",
		prefix, kind, obj->Get_ID(), obj->Get_Name() ? obj->Get_Name() : "", def_name,
		model ? model->Get_Name() : "", obj->Get_Collision_Group(), obj->Is_Ignore_Me(), obj->Is_Asleep(),
		obj->Is_Object_Simulating(),
		cb.Center.X, cb.Center.Y, cb.Center.Z, cb.Extent.X, cb.Extent.Y, cb.Extent.Z,
		Box_Ok(cb) ? "" : "  <-- BAD BOX");
}

static void Test_Move(HumanPhysClass * phys, const char * label, const Vector3 & move)
{
	AABoxClass box;
	phys->Get_Collision_Box(&box);
	CastResultStruct res;
	PhysAABoxCollisionTestClass test(box, move, &res, phys->Get_Collision_Group(), COLLISION_TYPE_PHYSICAL);
	phys->Inc_Ignore_Counter();
	PhysicsSceneClass::Get_Instance()->Cast_AABox(test);
	phys->Dec_Ignore_Counter();
	Log("  move %-5s startbad=%d fraction=%.3f normal=(%.2f %.2f %.2f) ", label, res.StartBad, res.Fraction,
		res.Normal.X, res.Normal.Y, res.Normal.Z);
	Describe("hit ", test.CollidedPhysObj);
}

static void Report(SoldierGameObj * star, HumanPhysClass * phys)
{
	SYSTEMTIME t;
	::GetLocalTime(&t);
	Vector3 pos = phys->Get_Position();
	Vector3 vel;
	phys->Get_Velocity(&vel);
	PhysControllerClass * ctrl = phys->Get_Controller();
	AABoxClass box;
	phys->Get_Collision_Box(&box);

	Log("\n==== STUCK %02d:%02d:%02d  game time %.1f s  frame %.1f ms (real %.1f ms)\n",
		t.wHour, t.wMinute, t.wSecond, TimeManager::Get_Total_Seconds(),
		TimeManager::Get_Frame_Seconds() * 1000.0f, TimeManager::Get_Frame_Real_Seconds() * 1000.0f);
	Log("player pos=(%.3f %.3f %.3f) vel=(%.3f %.3f %.3f) on_ground=%d user_control=%d immovable=%d\n",
		pos.X, pos.Y, pos.Z, vel.X, vel.Y, vel.Z, phys->Is_In_Contact(), phys->Is_User_Control_Enabled(),
		phys->Is_Immovable());
	if (ctrl != NULL) {
		Log("controller move=(%.2f %.2f %.2f) turn=%.2f\n", ctrl->Get_Move_Forward(), ctrl->Get_Move_Left(),
			ctrl->Get_Move_Up(), ctrl->Get_Turn_Left());
	}
	Log("collision box c=(%.3f %.3f %.3f) e=(%.3f %.3f %.3f)\n", box.Center.X, box.Center.Y, box.Center.Z,
		box.Extent.X, box.Extent.Y, box.Extent.Z);
	Describe("self ", phys);

	Test_Move(phys, "+X", Vector3(0.5f, 0, 0));
	Test_Move(phys, "-X", Vector3(-0.5f, 0, 0));
	Test_Move(phys, "+Y", Vector3(0, 0.5f, 0));
	Test_Move(phys, "-Y", Vector3(0, -0.5f, 0));
	Test_Move(phys, "up", Vector3(0, 0, 0.5f));
	Test_Move(phys, "down", Vector3(0, 0, -0.5f));
	Test_Move(phys, "none", Vector3(0, 0, 0));

	// everything within 3 m
	AABoxClass near_box(box.Center, box.Extent + Vector3(3, 3, 3));
	NonRefPhysListClass list;
	PhysicsSceneClass::Get_Instance()->Collect_Objects(near_box, true, true, &list);
	Log("objects within 3 m:\n");
	NonRefPhysListIterator nit(&list);
	for (nit.First(); !nit.Is_Done(); nit.Next()) {
		if (nit.Peek_Obj() != phys) {
			Describe("  ", nit.Peek_Obj());
		}
	}

	// every dynamic object: count, and the broken ones
	int count = 0, bad = 0, ignored = 0, asleep = 0;
	RefPhysListIterator it = PhysicsSceneClass::Get_Instance()->Get_Dynamic_Object_Iterator();
	for (it.First(); !it.Is_Done(); it.Next()) {
		PhysClass * obj = it.Peek_Obj();
		count++;
		if (obj->Is_Ignore_Me()) ignored++;
		if (obj->Is_Asleep()) asleep++;
		if (!Box_Ok(obj->Get_Cull_Box()) || !_finite(obj->Get_Transform()[0][3]) ||
				!_finite(obj->Get_Transform()[1][3]) || !_finite(obj->Get_Transform()[2][3])) {
			bad++;
			Describe("  broken: ", obj);
		}
	}
	Log("dynamic objects %d, broken %d, ignore-me %d, asleep %d\n", count, bad, ignored, asleep);

	// the other soldiers: are they stuck too?
	int soldiers = 0;
	for (SLNode<SmartGameObj> * n = GameObjManager::Get_Smart_Game_Obj_List()->Head(); n != NULL; n = n->Next()) {
		SoldierGameObj * s = n->Data()->As_SoldierGameObj();
		if (s == NULL || s == star || s->Peek_Human_Phys() == NULL || soldiers >= 12) {
			continue;
		}
		soldiers++;
		HumanPhysClass * hp = s->Peek_Human_Phys();
		Vector3 p = hp->Get_Position(), v;
		hp->Get_Velocity(&v);
		Log("  soldier %-28s hib=%d pos=(%.1f %.1f %.1f) vel=(%.2f %.2f %.2f) ground=%d asleep=%d ignore=%d group=%d\n",
			s->Get_Definition().Get_Name(), s->Is_Hibernating(), p.X, p.Y, p.Z, v.X, v.Y, v.Z,
			hp->Is_In_Contact(), hp->Is_Asleep(), hp->Is_Ignore_Me(), hp->Get_Collision_Group());
	}
}

void StuckWatch::Update(void)
{
	SoldierGameObj * star = COMBAT_STAR;
	if (star == NULL || star->Peek_Human_Phys() == NULL || star->Is_Dead()) {
		_StuckTime = 0.0f;
		return;
	}
	float dt = TimeManager::Get_Frame_Seconds();
	_SampleTimer += dt;
	if (_SampleTimer < 0.5f) {
		return;
	}
	float elapsed = _SampleTimer;
	_SampleTimer = 0.0f;

	HumanPhysClass * phys = star->Peek_Human_Phys();
	Vector3 pos = phys->Get_Position();
	PhysControllerClass * ctrl = phys->Get_Controller();
	bool wants_move = ctrl != NULL && (fabs(ctrl->Get_Move_Forward()) + fabs(ctrl->Get_Move_Left()) > 0.2f);
	bool moved = (pos - _LastPos).Length() > 0.02f;
	_LastPos = pos;

	if (moved || !wants_move || star->Get_Vehicle() != NULL) {
		if (_Reported && moved) {
			Log("moving again after %.1f s\n", _StuckTime);
		}
		if (moved || star->Get_Vehicle() != NULL) {
			_StuckTime = 0.0f;
			_Reported = false;
		}
		return;
	}
	_StuckTime += elapsed;
	if (_StuckTime >= 3.0f && !_Reported) {
		_Reported = true;
		Report(star, phys);
	}
}
