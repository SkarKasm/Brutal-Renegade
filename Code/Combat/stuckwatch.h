/*
**	Command & Conquer Renegade(tm)
**	Copyright 2025 Electronic Arts Inc.
**	GPL v3 or later; see stuckwatch.cpp.
*/

#ifndef STUCKWATCH_H
#define STUCKWATCH_H

// PORT: logs the physics state when the player can't move (stuck_watch.log next to the exe)
class StuckWatch
{
public:
	static void Update(void);
};

#endif
