// SPDX-License-Identifier: GPL-3.0-or-later
// Compile-only component. No main(), runtime execution or game dependencies.
#include "architecture.h"

#ifdef BR_EXPECT_POINTER_BITS
static_assert(br::platform::kPointerBits == BR_EXPECT_POINTER_BITS,
              "Selected compiler does not match the requested architecture");
#endif
static_assert(br::platform::kNativeMachine != br::platform::PeMachine::Unknown,
              "This migration currently targets x86 and AMD64 only");
static_assert(sizeof(br::platform::PointerValue) == sizeof(void*), "Pointer width changed");
static_assert(sizeof(br::wire::LegacyPointerField32LE) == 4, "Legacy save layout changed");
static_assert(sizeof(br::wire::LegacyChunkHeaderLE) == 8, "Legacy envelope changed");
static_assert(sizeof(br::wire::PersistentObjectField64LE) == 8, "New identity width changed");

// Instantiate the typed remap paths for compile-time checking on each target.
// These symbols are compiled into an object only; no startup code executes them.
template bool br::persistence::LegacyPointerReader::Queue<int>(
    br::platform::LegacyObjectToken32, int**);
template bool br::persistence::LegacyPointerReader::Queue<void>(
    br::platform::LegacyObjectToken32, void**);
