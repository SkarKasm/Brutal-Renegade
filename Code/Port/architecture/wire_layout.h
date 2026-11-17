// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BR_WIRE_LAYOUT_H
#define BR_WIRE_LAYOUT_H
#include "platform_types.h"
#include <cstring>

namespace br { namespace wire {
using namespace br::platform;

// Byte arrays provide explicit little-endian wire layout without global packing
// pragmas or unaligned scalar loads. Choose a field width in the schema itself.
template<unsigned Bytes> struct UnsignedLE {
    static_assert(Bytes == 1 || Bytes == 2 || Bytes == 4 || Bytes == 8, "Invalid wire width");
    u8 bytes[Bytes] = {};
    u64 Load() const noexcept
    {
        u64 value = 0;
        for (unsigned i = 0; i < Bytes; ++i) value |= u64(bytes[i]) << (8u * i);
        return value;
    }
    // Failure leaves the existing field intact. No silent wire narrowing.
    bool Store(u64 value) noexcept
    {
        if constexpr (Bytes < 8) {
            if ((value >> (Bytes * 8u)) != 0) return false;
        }
        for (unsigned i = 0; i < Bytes; ++i) bytes[i] = static_cast<u8>(value >> (8u * i));
        return true;
    }
};
using U8LE = UnsignedLE<1>;
using U16LE = UnsignedLE<2>;
using U32LE = UnsignedLE<4>;
using U64LE = UnsignedLE<8>;

struct I32LE {
    U32LE bits;
    i32 Load() const noexcept
    {
        const u32 value = static_cast<u32>(bits.Load());
        if (value <= u32((std::numeric_limits<i32>::max)())) return static_cast<i32>(value);
        return -1 - static_cast<i32>((std::numeric_limits<u32>::max)() - value);
    }
    void Store(i32 value) noexcept { bits.Store(static_cast<u32>(value)); }
};
struct I64LE {
    U64LE bits;
    i64 Load() const noexcept
    {
        const u64 value = bits.Load();
        if (value <= u64((std::numeric_limits<i64>::max)())) return static_cast<i64>(value);
        return -1 - static_cast<i64>((std::numeric_limits<u64>::max)() - value);
    }
    void Store(i64 value) noexcept { bits.Store(static_cast<u64>(value)); }
};

struct Float32LE {
    U32LE bits;
    float Load() const noexcept
    {
        u32 value = static_cast<u32>(bits.Load());
        float result;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }
    void Store(float value) noexcept
    {
        u32 encoded;
        std::memcpy(&encoded, &value, sizeof(encoded));
        bits.Store(encoded);
    }
};
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "Legacy float schema requires IEEE 754 binary32");

// Exact layout of the existing W3D chunk envelopes; size's high bit is the
// native sub-chunk flag, and the remaining 31 bits are its payload length.
struct LegacyChunkHeaderLE { U32LE type; U32LE size_and_flags; };
struct LegacyMicroChunkHeaderLE { U8LE type; U8LE size; };
struct LegacyPointerField32LE {
    U32LE token;
    LegacyObjectToken32 Load() const noexcept
    { return LegacyObjectToken32(static_cast<u32>(token.Load())); }
    void Store(LegacyObjectToken32 value) noexcept { token.Store(value.value); }
};

// This declaration reserves a separate, explicit identity domain. A versioned
// reader/writer must be implemented before any existing save/packet can use it.
struct PersistentObjectField64LE {
    U64LE id;
    PersistentObjectId64 Load() const noexcept { return PersistentObjectId64(id.Load()); }
    void Store(PersistentObjectId64 value) noexcept { id.Store(value.value); }
};

static_assert(sizeof(U8LE) == 1 && sizeof(U16LE) == 2 && sizeof(U32LE) == 4 && sizeof(U64LE) == 8,
              "Wire integer widths changed");
static_assert(sizeof(I32LE) == 4 && sizeof(I64LE) == 8, "Signed wire widths changed");
static_assert(alignof(U64LE) == 1, "Wire values must not add host padding");
static_assert(sizeof(Float32LE) == 4, "Float wire width changed");
static_assert(sizeof(LegacyChunkHeaderLE) == 8, "Legacy chunk header changed");
static_assert(offsetof(LegacyChunkHeaderLE, size_and_flags) == 4, "Legacy field offset changed");
static_assert(sizeof(LegacyMicroChunkHeaderLE) == 2, "Legacy micro-header changed");
static_assert(sizeof(LegacyPointerField32LE) == 4, "Legacy reference width changed");
static_assert(sizeof(PersistentObjectField64LE) == 8, "64-bit reference width changed");
static_assert(std::is_standard_layout<LegacyChunkHeaderLE>::value &&
              std::is_trivially_copyable<LegacyChunkHeaderLE>::value, "Unsafe wire layout");
}} // namespace br::wire
#endif
