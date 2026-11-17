// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BR_CHECKED_MEMORY_H
#define BR_CHECKED_MEMORY_H
#include "platform_types.h"

namespace br { namespace platform {
inline bool TryAdd(MemorySize left, MemorySize right, MemorySize& result) noexcept
{
    if (right > (std::numeric_limits<MemorySize>::max)() - left) return false;
    result = left + right;
    return true;
}
inline bool TryMultiply(MemorySize count, MemorySize stride, MemorySize& result) noexcept
{
    if (stride && count > (std::numeric_limits<MemorySize>::max)() / stride) return false;
    result = count * stride;
    return true;
}
inline bool TryAlignUp(MemorySize bytes, MemorySize alignment, MemorySize& result) noexcept
{
    if (!alignment || (alignment & (alignment - 1))) return false;
    MemorySize padded;
    if (!TryAdd(bytes, alignment - 1, padded)) return false;
    result = padded & ~(alignment - 1);
    return true;
}
inline bool RangeFits(MemorySize offset, MemorySize count, MemorySize capacity) noexcept
{
    return offset <= capacity && count <= capacity - offset;
}
inline bool TryLegacyCount32(MemorySize count, u32& result) noexcept
{
    if (count > (std::numeric_limits<u32>::max)()) return false;
    result = static_cast<u32>(count);
    return true;
}

// Zero budget means no artificial engine cap; allocation can still fail at the
// allocator/OS/backend boundary. This does not commit or reserve any memory.
struct MemoryBudget {
    MemorySize limit_bytes = 0;
    bool Allows(MemorySize committed, MemorySize requested) const noexcept
    {
        MemorySize total;
        return TryAdd(committed, requested, total) && (!limit_bytes || total <= limit_bytes);
    }
};

// A pointer-width migration does not widen DX8 upload counts or GPU indices.
// Backends must report their own capacities; the legacy defaults remain bounded.
struct GeometryBufferLimits {
    MemorySize max_vertex_count = 65535;
    MemorySize max_index_count = 65535;
    unsigned index_value_bits = 16;

    bool Allows(MemorySize vertices, MemorySize polygons, MemorySize passes) const noexcept
    {
        if (!vertices || !passes || vertices > max_vertex_count) return false;
        if (index_value_bits != 16 && index_value_bits != 32) return false;
        const u64 addressable_vertices = u64(1) << index_value_bits;
        if (vertices > addressable_vertices) return false;
        MemorySize indices;
        return TryMultiply(polygons, MemorySize(3), indices) &&
               TryMultiply(indices, passes, indices) && indices <= max_index_count;
    }
};

// Intrusive free-list storage must fit a native next-pointer in EVERY slot.
// The allocator must align the returned block base to required_alignment.
struct PoolBlockLayout {
    MemorySize first_object_offset = 0;
    MemorySize object_stride = 0;
    MemorySize total_bytes = 0;
    MemorySize required_alignment = 0;
};
inline bool TryPoolBlockLayout(MemorySize object_size, MemorySize object_alignment,
    MemorySize object_count, PoolBlockLayout& layout) noexcept
{
    if (!object_size || !object_alignment || (object_alignment & (object_alignment - 1))) return false;
    const MemorySize alignment = object_alignment > alignof(void*) ? object_alignment : alignof(void*);
    const MemorySize slot_size = object_size > sizeof(void*) ? object_size : sizeof(void*);
    MemorySize first, stride, payload, total;
    if (!TryAlignUp(sizeof(void*), alignment, first) ||
        !TryAlignUp(slot_size, alignment, stride) ||
        !TryMultiply(stride, object_count, payload) || !TryAdd(first, payload, total)) return false;
    layout.first_object_offset = first;
    layout.object_stride = stride;
    layout.total_bytes = total;
    layout.required_alignment = alignment;
    return true;
}
}} // namespace br::platform
#endif
