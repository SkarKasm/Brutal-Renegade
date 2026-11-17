// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BR_DEPENDENCY_ABI_H
#define BR_DEPENDENCY_ABI_H
#include "checked_memory.h"

namespace br { namespace platform {
enum class PeMachine : u16 { Unknown = 0, I386 = 0x014c, Amd64 = 0x8664 };
#if defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)
constexpr PeMachine kNativeMachine = PeMachine::Amd64;
#elif defined(_M_IX86) || defined(__i386__)
constexpr PeMachine kNativeMachine = PeMachine::I386;
#else
constexpr PeMachine kNativeMachine = PeMachine::Unknown;
#endif
inline bool CanLoadInProcess(PeMachine machine) noexcept
{ return machine != PeMachine::Unknown && machine == kNativeMachine; }

enum class DependencyKind : u8 { Audio, Graphics, Video, MissionScripts, Bandwidth, OpenXR };
// This is an in-process descriptor, not a serialized record or an exported C ABI.
struct NativeBackendContract {
    u32 interface_version = 1;
    PeMachine machine = kNativeMachine;
    MemorySize pointer_bytes = sizeof(void*);
    MemorySize allocation_size_bytes = sizeof(MemorySize);
    bool CompatibleWithCurrentProcess() const noexcept
    {
        return interface_version == 1 && CanLoadInProcess(machine) &&
               pointer_bytes == sizeof(void*) && allocation_size_bytes == sizeof(MemorySize);
    }
};

// Adapters report actual implemented capabilities. Merely changing this
// structure does not create an OpenAL renderer or widen the legacy DX8 backend.
struct GraphicsMemoryContract {
    NativeBackendContract abi;
    GeometryBufferLimits geometry;
    MemoryBudget texture_bytes;
    MemoryBudget dynamic_vertex_bytes;
};
}} // namespace br::platform
#endif
