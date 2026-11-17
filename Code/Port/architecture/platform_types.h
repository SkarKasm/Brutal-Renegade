// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BR_PLATFORM_TYPES_H
#define BR_PLATFORM_TYPES_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

// Runtime addresses and persistent identities have intentionally distinct types.
// Do not redefine Windows DWORD/LONG or the engine's serialized uint32 aliases.
namespace br { namespace platform {
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using PointerValue = std::uintptr_t;
using SignedPointerValue = std::intptr_t;
using MemorySize = std::size_t;
using MemoryDifference = std::ptrdiff_t;

constexpr unsigned kPointerBits = sizeof(void*) * 8u;
constexpr bool kIs64Bit = sizeof(void*) == 8;
static_assert(sizeof(void*) == 4 || sizeof(void*) == 8, "Unsupported pointer width");
static_assert(sizeof(PointerValue) == sizeof(void*), "Pointer integer truncates addresses");
static_assert(sizeof(SignedPointerValue) == sizeof(void*), "Pointer difference truncates addresses");
static_assert(sizeof(MemorySize) >= sizeof(void*), "Allocation size truncates addresses");
static_assert(sizeof(u8) == 1 && sizeof(u16) == 2 && sizeof(u32) == 4 && sizeof(u64) == 8,
              "Fixed-width scalar contract changed");
static_assert(std::numeric_limits<unsigned char>::digits == 8, "Non-octet host unsupported");
#ifdef _WIN32
static_assert(sizeof(long) == 4, "Windows LONG/DWORD must retain their LLP64 ABI");
#endif

inline PointerValue PointerBits(const void* address) noexcept
{
    return reinterpret_cast<PointerValue>(address);
}

template<class T> inline T* PointerFromBits(PointerValue value) noexcept
{
    return reinterpret_cast<T*>(value);
}

// This folds ALL address bytes into a legacy 32-bit hash result. The table must
// still store/compare the complete key; a hash value is never a pointer identity.
inline u32 PointerHash32(const void* address) noexcept
{
    PointerValue value = PointerBits(address);
    u32 hash = 2166136261u;
    for (MemorySize byte = 0; byte < sizeof(value); ++byte) {
        hash = (hash ^ static_cast<u8>(value & 0xffu)) * 16777619u;
        value >>= 8;
    }
    return hash;
}

// Strong identities keep raw addresses out of save/network record fields.
struct LegacyObjectToken32 {
    u32 value;
    explicit constexpr LegacyObjectToken32(u32 id = 0) noexcept : value(id) {}
};
struct PersistentObjectId64 {
    u64 value;
    explicit constexpr PersistentObjectId64(u64 id = 0) noexcept : value(id) {}
};
static_assert(sizeof(LegacyObjectToken32) == 4, "Legacy object token must remain 4 bytes");
static_assert(sizeof(PersistentObjectId64) == 8, "Persistent object ID must remain 8 bytes");
static_assert(!std::is_convertible<PointerValue, LegacyObjectToken32>::value,
              "Native addresses must not implicitly become serialized tokens");
}} // namespace br::platform
#endif
