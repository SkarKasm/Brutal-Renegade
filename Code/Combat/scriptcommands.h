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
 *                     $Archive:: /Commando/Code/Combat/scriptcommands.h                      $* 
 *                                                                                             * 
 *                      $Author:: Patrick                                                     $* 
 *                                                                                             * 
 *                     $Modtime:: 1/09/02 12:09p                                              $* 
 *                                                                                             * 
 *                    $Revision:: 211                                                         $* 
 *                                                                                             * 
 *---------------------------------------------------------------------------------------------* 
 * Functions:                                                                                  * 
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

// PORT: members that had default arguments (allowed by VC6 on function pointers, not by
// modern C++) are wrapper structs holding the same single function pointer; see
// Port/gen_script_commands.py. Layout and the scripts.dll interface are unchanged.
#ifndef	SCRIPTCOMMANDS_H
#define	SCRIPTCOMMANDS_H

#ifndef	VECTOR3_H
	#include "vector3.h"
#endif

#ifndef	COMBATSOUND_H
	#include "combatsound.h"
#endif

#ifndef	ACTIONPARAMS_H
	#include "actionparams.h"
#endif


/*
** DLL import/export macros
*/
#ifdef	BUILDING_DLL
	#define	SCRIPT_DLL_COMMAND	_declspec(dllimport)
#else
	#define	SCRIPT_DLL_COMMAND	extern "C" _declspec(dllexport)
#endif

/*
** Types
*/
class		ScriptableGameObj;
typedef	ScriptableGameObj	GameObject;
class		AudibleSoundClass;
typedef	AudibleSoundClass	Sound2D;
class		Sound3DClass;
typedef	Sound3DClass		Sound3D;
class		ScriptClass;
class		ScriptSaver;
class		ScriptLoader;


/*
** Script Commands
*/


/*
** MISC Script ENUMS
*/
enum {
		OBJECTIVE_TYPE_PRIMARY 				= 1,
		OBJECTIVE_TYPE_SECONDARY,
		OBJECTIVE_TYPE_TERTIARY,

		OBJECTIVE_STATUS_PENDING			= 0,
		OBJECTIVE_STATUS_ACCOMPLISHED,
		OBJECTIVE_STATUS_FAILED,
		OBJECTIVE_STATUS_HIDDEN,

		RADAR_BLIP_SHAPE_NONE				= 0,
		RADAR_BLIP_SHAPE_HUMAN,
		RADAR_BLIP_SHAPE_VEHICLE,
		RADAR_BLIP_SHAPE_STATIONARY,
		RADAR_BLIP_SHAPE_OBJECTIVE,

		RADAR_BLIP_COLOR_NOD					= 0,
		RADAR_BLIP_COLOR_GDI,
		RADAR_BLIP_COLOR_NEUTRAL,
		RADAR_BLIP_COLOR_MUTANT,
		RADAR_BLIP_COLOR_RENEGADE,
		RADAR_BLIP_COLOR_PRIMARY_OBJECTIVE,
		RADAR_BLIP_COLOR_SECONDARY_OBJECTIVE,
		RADAR_BLIP_COLOR_TERTIARY_OBJECTIVE,

		SCRIPT_PLAYERTYPE_SPECTATOR				= -4,		// -4
		SCRIPT_PLAYERTYPE_MUTANT,							// -3
		SCRIPT_PLAYERTYPE_NEUTRAL,							// -2
		SCRIPT_PLAYERTYPE_RENEGADE,							// -1
		SCRIPT_PLAYERTYPE_NOD,								//  0
		SCRIPT_PLAYERTYPE_GDI,								//  1
};

/*
** Script Commands List
*/

#define SCRIPT_COMMANDS_VERSION 174

typedef struct {
	unsigned int Size;
	unsigned int Version;

	// Debug messages
	void (*	Debug_Message )( char *format, ... );

	// Action Commands
	void ( * Action_Reset )( GameObject * obj, float priority );
	void ( * Action_Goto )( GameObject * obj, const ActionParamsStruct & params );
	void ( * Action_Attack )( GameObject * obj, const ActionParamsStruct & params );
	void ( * Action_Play_Animation )( GameObject * obj, const ActionParamsStruct & params );
	void ( * Action_Enter_Exit )( GameObject * obj, const ActionParamsStruct & params );
	void ( * Action_Face_Location )( GameObject * obj, const ActionParamsStruct & params );
	void ( * Action_Dock )( GameObject * obj, const ActionParamsStruct & params );
	void ( * Action_Follow_Input )( GameObject * obj, const ActionParamsStruct & params );

	struct Modify_Action_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( GameObject * obj, int action_id, const ActionParamsStruct & params, bool modify_move, bool modify_attack );
		Ptr fn;
		void operator()( GameObject * obj, int action_id, const ActionParamsStruct & params, bool modify_move = true, bool modify_attack = true ) const { return fn( obj, action_id, params, modify_move, modify_attack ); }
		Modify_Action_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Modify_Action;

	// Action information queries
	int	( * Get_Action_ID )( GameObject * obj );
	bool	( * Get_Action_Params )( GameObject * obj, ActionParamsStruct & params );
	bool	( * Is_Performing_Pathfind_Action )( GameObject * obj );

	// Physical control
	void ( * Set_Position )( GameObject * obj, const Vector3 & position );
	Vector3 ( * Get_Position )( GameObject * obj );
	Vector3 ( * Get_Bone_Position )( GameObject * obj, const char * bone_name );
	float ( * Get_Facing )( GameObject * obj );
	void ( * Set_Facing )( GameObject * obj, float degrees );

	// Collision Control
	void	( * Disable_All_Collisions )( GameObject * obj );
	void	( * Disable_Physical_Collisions )( GameObject * obj );
	void	( * Enable_Collisions )( GameObject * obj );
	
	// Object Management
	void ( * Destroy_Object )( GameObject * obj );
	GameObject * ( * Find_Object )( int obj_id );
	GameObject * ( * Create_Object )( const char * type_name, const Vector3 & position );
	GameObject * ( * Create_Object_At_Bone )( GameObject * host_obj, const char * new_obj_type_name, const char * bone_name );
	int	( * Get_ID )( GameObject * obj );
	int	( * Get_Preset_ID )( GameObject * obj );
	const char * ( * Get_Preset_Name )( GameObject * obj );
	void (*Attach_Script)(GameObject* object, const char* scriptName, const char* scriptParams);
	void (*Add_To_Dirty_Cull_List)(GameObject* object);

	// Timers
	void	( * Start_Timer )( GameObject * obj, ScriptClass * script, float duration, int timer_id );

	// Weapons
	struct Trigger_Weapon_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( GameObject * obj, bool trigger, const Vector3 & target, bool primary );
		Ptr fn;
		void operator()( GameObject * obj, bool trigger, const Vector3 & target, bool primary = true ) const { return fn( obj, trigger, target, primary ); }
		Trigger_Weapon_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Trigger_Weapon;
	void	( * Select_Weapon )( GameObject * obj, const char * weapon_name );

	// Custom Script
	struct Send_Custom_Event_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( GameObject * from, GameObject * to, int type, int param, float delay );
		Ptr fn;
		void operator()( GameObject * from, GameObject * to, int type = 0, int param = 0, float delay = 0 ) const { return fn( from, to, type, param, delay ); }
		Send_Custom_Event_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Send_Custom_Event;
	void	( * Send_Damaged_Event )( GameObject * object, GameObject * damager );

	// Random Numbers
	float	( * Get_Random )( float min, float max );
	int	( * Get_Random_Int )( int min, int max );  // Get a random number between min and max-1, INCLUSIVE 

	//	Random Selection
	GameObject * ( * Find_Random_Simple_Object )( const char *preset_name );

	// Object Display
	void	( * Set_Model )( GameObject * obj, const char * model_name );
	struct Set_Animation_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( GameObject * obj, const char * anim_name, bool looping, const char * sub_obj_name, float start_frame, float end_frame, bool is_blended );
		Ptr fn;
		void operator()( GameObject * obj, const char * anim_name, bool looping, const char * sub_obj_name = NULL, float start_frame = 0.0F, float end_frame = -1.0F, bool is_blended = false ) const { return fn( obj, anim_name, looping, sub_obj_name, start_frame, end_frame, is_blended ); }
		Set_Animation_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Set_Animation;
	void	( * Set_Animation_Frame )( GameObject * obj, const char * anim_name, int frame );

	// Sounds
	// Note: Each sound creation function returns the ID of the new sound (0 on error)
	int	( * Create_Sound )( const char * sound_preset_name, const Vector3 & position, GameObject * creator );
	int	( * Create_2D_Sound )( const char * sound_preset_name );
	int	( * Create_2D_WAV_Sound )( const char * wav_filename );
	int	( * Create_3D_WAV_Sound_At_Bone )( const char * wav_filename, GameObject * obj, const char * bone_name );
	int	( * Create_3D_Sound_At_Bone )( const char * sound_preset_name, GameObject * obj, const char * bone_name );
	int	( * Create_Logical_Sound )( GameObject * creator, int type, const Vector3 & position, float radius );	
	void	( * Start_Sound )( int sound_id );
	struct Stop_Sound_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( int sound_id, bool destroy_sound );
		Ptr fn;
		void operator()( int sound_id, bool destroy_sound = true ) const { return fn( sound_id, destroy_sound ); }
		Stop_Sound_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Stop_Sound;
	void	( * Monitor_Sound )( GameObject * game_obj, int sound_id );
	void	( * Set_Background_Music )( const char * wav_filename );
	void	( * Fade_Background_Music )( const char * wav_filename, int fade_out_time, int fade_in_time );
	void	( * Stop_Background_Music )( void );

	// Object Properties
	float	( * Get_Health )( GameObject * obj );
	float	( * Get_Max_Health )( GameObject * obj );
	void	( * Set_Health )( GameObject * obj, float health );
	float	( * Get_Shield_Strength )( GameObject * obj );
	float	( * Get_Max_Shield_Strength )( GameObject * obj );
	void	( * Set_Shield_Strength )( GameObject * obj, float strength );
	void	( * Set_Shield_Type )( GameObject * obj, const char * name );
	int	( * Get_Player_Type )( GameObject * obj );
	void	( * Set_Player_Type )( GameObject * obj, int type );

	// Math
	float	( * Get_Distance )( const Vector3 & p1, const Vector3 & p2 );

	// Set Camera Host
	void	( * Set_Camera_Host )( GameObject * obj );
	void	( * Force_Camera_Look )( const Vector3 & target );

	// Get the Star
	GameObject * ( * Get_The_Star )( void );
	GameObject * ( * Get_A_Star )( const Vector3 & pos );
	struct Find_Closest_Soldier_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef GameObject * (*Ptr)( const Vector3 & pos, float min_dist, float max_dist, bool only_human );
		Ptr fn;
		GameObject * operator()( const Vector3 & pos, float min_dist, float max_dist, bool only_human = true ) const { return fn( pos, min_dist, max_dist, only_human ); }
		Find_Closest_Soldier_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Find_Closest_Soldier;
	bool		    ( * Is_A_Star )( GameObject * obj );

	// Object Control
	void	( * Control_Enable )( GameObject * obj, bool enable );

	// Hack
	const char * ( * Get_Damage_Bone_Name )( void );
	bool			 ( * Get_Damage_Bone_Direction )( void ); // true means shot in the back

	// Visibility
	bool	( * Is_Object_Visible)( GameObject * looker, GameObject * obj );
	struct Enable_Enemy_Seen_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( GameObject * obj, bool enable );
		Ptr fn;
		void operator()( GameObject * obj, bool enable = true ) const { return fn( obj, enable ); }
		Enable_Enemy_Seen_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Enable_Enemy_Seen;

	// Display Text
	struct Set_Display_Color_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( unsigned char red, unsigned char green, unsigned char blue );
		Ptr fn;
		void operator()( unsigned char red = 255, unsigned char green = 255, unsigned char blue = 255 ) const { return fn( red, green, blue ); }
		Set_Display_Color_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Set_Display_Color;
	void	(*	Display_Text )( int string_id );
	struct Display_Float_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( float value, const char * format );
		Ptr fn;
		void operator()( float value, const char * format = "%f" ) const { return fn( value, format ); }
		Display_Float_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Display_Float;
	struct Display_Int_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( int value, const char * format );
		Ptr fn;
		void operator()( int value, const char * format = "%d" ) const { return fn( value, format ); }
		Display_Int_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Display_Int;

	// SaveLoad
	void	(*	Save_Data )( ScriptSaver & saver, int id, int size, void * data );
	void	(*	Save_Pointer )( ScriptSaver & saver, int id, void * pointer );
	bool	(*	Load_Begin )( ScriptLoader & loader, int * id );
	void	(*	Load_Data )( ScriptLoader & loader, int size, void * data );
	void	(*	Load_Pointer )( ScriptLoader & loader, void ** pointer );
	void	(*	Load_End )( ScriptLoader & loader );

	void (*Begin_Chunk)(ScriptSaver& saver, unsigned int chunkID);
	void (*End_Chunk)(ScriptSaver& saver);
	bool (*Open_Chunk)(ScriptLoader& loader, unsigned int* chunkID);
	void (*Close_Chunk)(ScriptLoader& loader);

	// Radar Effects
	void	(* Clear_Radar_Markers )( void );
	void	(* Clear_Radar_Marker )( int id );
	void	(* Add_Radar_Marker )( int id, const Vector3& position, int shape_type, int color_type );
	void	(* Set_Obj_Radar_Blip_Shape )( GameObject * obj, int shape_type );	// Set to -1 to reset default
	void	(* Set_Obj_Radar_Blip_Color )( GameObject * obj, int color_type );	// Set to -1 to reset default
	void	(* Enable_Radar )( bool enable );

	//
	//	Map support
	//
	void	(* Clear_Map_Cell )( int cell_x, int cell_y );	
	void	(* Clear_Map_Cell_By_Pos )( const Vector3 &world_space_pos );
	void	(* Clear_Map_Cell_By_Pixel_Pos )( int pixel_pos_x, int pixel_pos_y );
	void	(* Clear_Map_Region_By_Pos )( const Vector3 &world_space_pos, int pixel_radius );
	void	(* Reveal_Map )( void );
	void	(* Shroud_Map )( void );
	void	(* Show_Player_Map_Marker )( bool onoff );

	//
	//	Height DB access
	//
	float	(* Get_Safe_Flight_Height )( float x_pos, float y_pos );

	// Explosions
	struct Create_Explosion_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( const char * explosion_def_name, const Vector3 & pos, GameObject * creator );
		Ptr fn;
		void operator()( const char * explosion_def_name, const Vector3 & pos, GameObject * creator = NULL ) const { return fn( explosion_def_name, pos, creator ); }
		Create_Explosion_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Create_Explosion;
	struct Create_Explosion_At_Bone_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( const char * explosion_def_name, GameObject * object, const char * bone_name, GameObject * creator );
		Ptr fn;
		void operator()( const char * explosion_def_name, GameObject * object, const char * bone_name, GameObject * creator = NULL ) const { return fn( explosion_def_name, object, bone_name, creator ); }
		Create_Explosion_At_Bone_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Create_Explosion_At_Bone;

	// HUD
	void	(* Enable_HUD )( bool enable );
	void	(* Mission_Complete )( bool success );

	struct Give_PowerUp_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( GameObject * obj, const char * preset_name, bool display_on_hud );
		Ptr fn;
		void operator()( GameObject * obj, const char * preset_name, bool display_on_hud = false ) const { return fn( obj, preset_name, display_on_hud ); }
		Give_PowerUp_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Give_PowerUp;

	// Administration
	void (*Innate_Disable)(GameObject* object);
	void (*Innate_Enable)(GameObject* object);

	// Innate Soldier AI Enable/Disable (returns old value)
	bool	(* Innate_Soldier_Enable_Enemy_Seen )( GameObject * obj, bool state );
	bool	(* Innate_Soldier_Enable_Gunshot_Heard )( GameObject * obj, bool state );
	bool	(* Innate_Soldier_Enable_Footsteps_Heard )( GameObject * obj, bool state );
	bool	(* Innate_Soldier_Enable_Bullet_Heard )( GameObject * obj, bool state );
	bool	(* Innate_Soldier_Enable_Actions )( GameObject * obj, bool state );
	struct Set_Innate_Soldier_Home_Location_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( GameObject * obj, const Vector3& home_pos, float home_radius );
		Ptr fn;
		void operator()( GameObject * obj, const Vector3& home_pos, float home_radius = 999999 ) const { return fn( obj, home_pos, home_radius ); }
		Set_Innate_Soldier_Home_Location_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Set_Innate_Soldier_Home_Location;
	void	(* Set_Innate_Aggressiveness )( GameObject * obj, float aggressiveness );
	void	(* Set_Innate_Take_Cover_Probability )( GameObject * obj, float probability );
	void	(* Set_Innate_Is_Stationary )( GameObject * obj, bool stationary );

	void	(* Innate_Force_State_Bullet_Heard )( GameObject * obj, const Vector3 & pos );
	void	(* Innate_Force_State_Footsteps_Heard )( GameObject * obj, const Vector3 & pos );
	void	(* Innate_Force_State_Gunshots_Heard )( GameObject * obj, const Vector3 & pos );
	void	(* Innate_Force_State_Enemy_Seen )( GameObject * obj, GameObject * enemy );

	// Control of StaticAnimPhys
	struct Static_Anim_Phys_Goto_Frame_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( int obj_id, float frame, const char * anim_name );
		Ptr fn;
		void operator()( int obj_id, float frame, const char * anim_name = NULL ) const { return fn( obj_id, frame, anim_name ); }
		Static_Anim_Phys_Goto_Frame_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Static_Anim_Phys_Goto_Frame;
	struct Static_Anim_Phys_Goto_Last_Frame_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( int obj_id, const char * anim_name );
		Ptr fn;
		void operator()( int obj_id, const char * anim_name = NULL ) const { return fn( obj_id, anim_name ); }
		Static_Anim_Phys_Goto_Last_Frame_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Static_Anim_Phys_Goto_Last_Frame;

	// Timing
	unsigned int (* Get_Sync_Time)( void );

	// Objectives
	struct Add_Objective_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( int id, int type, int status, int short_description_id, char * description_sound_filename, int long_description_id );
		Ptr fn;
		void operator()( int id, int type, int status, int short_description_id, char * description_sound_filename = NULL, int long_description_id = 0 ) const { return fn( id, type, status, short_description_id, description_sound_filename, long_description_id ); }
		Add_Objective_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Add_Objective;
	void	(* Remove_Objective)( int id );
	void	(* Set_Objective_Status)( int id, int status );
	void	(* Change_Objective_Type)( int id, int type );
	void	(* Set_Objective_Radar_Blip)( int id, const Vector3 & position );
	void	(* Set_Objective_Radar_Blip_Object)( int id, ScriptableGameObj * unit );
	void	(* Set_Objective_HUD_Info)( int id, float priority, const char * texture_name, int message_id );
	void	(* Set_Objective_HUD_Info_Position)( int id, float priority, const char * texture_name, int message_id, const Vector3 & position );

	// Camaera Shakes
	struct Shake_Camera_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( const Vector3 & pos, float radius, float intensity, float duration );
		Ptr fn;
		void operator()( const Vector3 & pos, float radius = 25, float intensity = 0.25f, float duration = 1.5f ) const { return fn( pos, radius, intensity, duration ); }
		Shake_Camera_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Shake_Camera;

	// Spawners
	void	(* Enable_Spawner)( int id, bool enable );
	GameObject * (* Trigger_Spawner)( int id );

	// Vehicles
	void	(* Enable_Engine)( GameObject* object, bool onoff );

	// Difficulty Level
	int	(* Get_Difficulty_Level)( void );

	// Keys
	struct Grant_Key_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( GameObject* object, int key, bool grant );
		Ptr fn;
		void operator()( GameObject* object, int key, bool grant = true ) const { return fn( object, key, grant ); }
		Grant_Key_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Grant_Key;
	bool	(* Has_Key)( GameObject* object, int key );

	// Hibernation
	void	(* Enable_Hibernation)( GameObject * object, bool enable );

	void	(* Attach_To_Object_Bone)( GameObject * object, GameObject * host_object, const char * bone_name );

	// Conversation
	struct Create_Conversation_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef int (*Ptr)( const char *conversation_name, int priority, float max_dist, bool is_interruptable );
		Ptr fn;
		int operator()( const char *conversation_name, int priority = 0, float max_dist = 0, bool is_interruptable = true ) const { return fn( conversation_name, priority, max_dist, is_interruptable ); }
		Create_Conversation_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Create_Conversation;
	struct Join_Conversation_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( GameObject * object, int active_conversation_id, bool allow_move, bool allow_head_turn, bool allow_face );
		Ptr fn;
		void operator()( GameObject * object, int active_conversation_id, bool allow_move = true, bool allow_head_turn = true, bool allow_face = true ) const { return fn( object, active_conversation_id, allow_move, allow_head_turn, allow_face ); }
		Join_Conversation_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Join_Conversation;
	void	(* Join_Conversation_Facing)( GameObject * object, int active_conversation_id, int obj_id_to_face );
	struct Start_Conversation_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( int active_conversation_id, int action_id );
		Ptr fn;
		void operator()( int active_conversation_id, int action_id = 0 ) const { return fn( active_conversation_id, action_id ); }
		Start_Conversation_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Start_Conversation;
	void	(* Monitor_Conversation)( GameObject * object, int active_conversation_id );
	void	(* Start_Random_Conversation)( GameObject * object );
	void	(* Stop_Conversation)( int active_conversation_id );
	void	(* Stop_All_Conversations)( void );

	// Locked facing support
	void	(* Lock_Soldier_Facing)( GameObject * object, GameObject * object_to_face, bool turn_body );
	void	(* Unlock_Soldier_Facing)( GameObject * object );

	// Apply Damage
	struct Apply_Damage_Fn {	// PORT: one function pointer that keeps VC6's default arguments
		typedef void (*Ptr)( GameObject * object, float amount, const char * warhead_name, GameObject * damager );
		Ptr fn;
		void operator()( GameObject * object, float amount, const char * warhead_name, GameObject * damager = NULL ) const { return fn( object, amount, warhead_name, damager ); }
		Apply_Damage_Fn & operator= (Ptr p) { fn = p; return *this; }
		operator Ptr () const { return fn; }
	} Apply_Damage;

	// Soldier
	void	(* Set_Loiters_Allowed)( GameObject * object, bool allowed );

	void	(* Set_Is_Visible)( GameObject * object, bool visible );
	void	(* Set_Is_Rendered)( GameObject * object, bool rendered );

	// Points
	float	(* Get_Points)( GameObject * object );
	void	(* Give_Points)( GameObject * object, float points, bool entire_team );

	// Money (points and money were separated 09/06/01)
	float	(* Get_Money)( GameObject * object );
	void	(* Give_Money)( GameObject * object, float money, bool entire_team );

	// Buildings
	bool	(* Get_Building_Power)( GameObject * object );
	void	(* Set_Building_Power)( GameObject * object, bool onoff );
	void	(* Play_Building_Announcement)( GameObject * object, int text_id );
	GameObject * (* Find_Nearest_Building_To_Pos )( const Vector3 & position, const char * mesh_prefix );
	GameObject * (* Find_Nearest_Building )( GameObject * object, const char * mesh_prefix );

	// Zones
	int	(* Team_Members_In_Zone)( GameObject * object, int player_type );

	// Background
	void  (*Set_Clouds) (float cloudcover, float cloudgloominess, float ramptime);
	void  (*Set_Lightning) (float intensity, float startdistance, float enddistance, float heading, float distribution, float ramptime);
	void  (*Set_War_Blitz) (float intensity, float startdistance, float enddistance, float heading, float distribution, float ramptime);

	// Weather
	void	(*Set_Wind)			(float heading, float speed, float variability, float ramptime);
	void	(*Set_Rain)			(float density, float ramptime, bool prime);
	void	(*Set_Snow)			(float density, float ramptime, bool prime);
	void	(*Set_Ash)			(float density, float ramptime, bool prime);
	void  (*Set_Fog_Enable) (bool enabled);
	void  (*Set_Fog_Range)	(float startdistance, float enddistance, float ramptime);

	// Stealth control
	void	(*Enable_Stealth)	(GameObject * object, bool onoff);

	// Sniper control
	void	(*Cinematic_Sniper_Control)	(bool enabled, float zoom);

	// File Access
	int	(*Text_File_Open)			( const char * filename );
	bool	(*Text_File_Get_String)	( int handle, char * buffer, int size );
	void	(*Text_File_Close)		( int handle );

	// Vehicle Transitions
	void	(*Enable_Vehicle_Transitions)	( GameObject * object, bool enable );

	// Player terminal support
	void	(*Display_GDI_Player_Terminal)		();
	void	(*Display_NOD_Player_Terminal)		();
	void	(*Display_Mutant_Player_Terminal)	();

	// Encyclopedia support
	bool	(*Reveal_Encyclopedia_Character)	( int object_id );
	bool	(*Reveal_Encyclopedia_Weapon)		( int object_id );
	bool	(*Reveal_Encyclopedia_Vehicle)	( int object_id );
	bool	(*Reveal_Encyclopedia_Building)	( int object_id );
	void	(*Display_Encyclopedia_Event_UI) ( void );

	void	(* Scale_AI_Awareness)( float sight_scale, float hearing_scale );

	// Cinematic Freeze
	void	(* Enable_Cinematic_Freeze)( GameObject * object, bool enable );

	void	(* Expire_Powerup )( GameObject * object );

	// Hud stuff
	void	(* Set_HUD_Help_Text )( int string_id, const Vector3 &color );
	void	(* Enable_HUD_Pokable_Indicator )( GameObject * object, bool enable );

	void	(* Enable_Innate_Conversations )( GameObject * object, bool enable );

	void	(* Display_Health_Bar )( GameObject * object, bool display );

	// Shadow control.  In certain cases we need to manually disable shadow casting
	// on an object.  Cinematics with too many characters are an example of this.
	void	(* Enable_Shadow) ( GameObject * object, bool enable );

	void	(* Clear_Weapons) ( GameObject * object );

	void	(* Set_Num_Tertiary_Objectives) ( int count );

	// Letterbox and screen fading controls
	void	(* Enable_Letterbox) ( bool onoff, float seconds );
	void	(* Set_Screen_Fade_Color) ( float r, float g, float b, float seconds );
	void	(* Set_Screen_Fade_Opacity) ( float opacity, float seconds );

} ScriptCommands;


/*
** Build a class to wrap the struct
*/
class ScriptCommandsClass {
public:
	ScriptCommands	*Commands;
};

/*
** Get Script Commands 
** This should only be called in the host application
** and not from the DLL
*/
ScriptCommands	*Get_Script_Commands( void );

#endif	// SCRIPTCOMMANDS_H
