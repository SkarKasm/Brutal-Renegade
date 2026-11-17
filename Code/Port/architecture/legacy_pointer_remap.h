// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BR_LEGACY_POINTER_REMAP_H
#define BR_LEGACY_POINTER_REMAP_H
#include "platform_types.h"
#include <map>
#include <vector>

namespace br { namespace persistence {
using platform::LegacyObjectToken32;
using platform::u32;
using platform::u64;
using platform::MemorySize;

// Per-save session: generate fixed-width identities without truncating addresses.
// Distinct base/interface subobject addresses intentionally get distinct tokens.
class LegacyPointerWriter {
    std::map<const void*, u32, std::less<const void*> > tokens_;
    u64 next_ = 1;
public:
    bool TokenFor(const void* object, LegacyObjectToken32& result)
    {
        if (!object) { result = LegacyObjectToken32(); return true; }
        auto existing = tokens_.find(object);
        if (existing != tokens_.end()) { result = LegacyObjectToken32(existing->second); return true; }
        if (next_ > (std::numeric_limits<u32>::max)()) return false;
        const u32 token = static_cast<u32>(next_);
        tokens_.emplace(object, token);
        ++next_;
        result = LegacyObjectToken32(token);
        return true;
    }
    void Clear() { tokens_.clear(); next_ = 1; }
};

// Per-load session: old on-disk identity -> current full-width runtime address.
// The caller must bind the SAME base/interface address represented by the token;
// a void* alone cannot infer a multiple-inheritance pointer adjustment.
class LegacyPointerReader {
    struct Fixup {
        LegacyObjectToken32 token;
        void* slot;
        void (*assign)(void*, void*);
    };
    std::map<u32, void*> objects_;
    std::vector<Fixup> fixups_;
    template<class T> static void Assign(void* slot, void* object)
    { *static_cast<T**>(slot) = static_cast<T*>(object); }
public:
    bool Bind(LegacyObjectToken32 token, void* object)
    {
        if (!token.value) return object == nullptr;
        if (!object) return false;
        auto result = objects_.emplace(token.value, object);
        return result.second || result.first->second == object;
    }
    bool Find(LegacyObjectToken32 token, void*& result) const
    {
        if (!token.value) { result = nullptr; return true; }
        auto item = objects_.find(token.value);
        if (item == objects_.end()) return false;
        result = item->second;
        return true;
    }
    template<class T> bool Queue(LegacyObjectToken32 token, T** destination)
    {
        static_assert(std::is_object<T>::value || std::is_void<T>::value,
                      "Only object pointer slots can be remapped");
        if (!destination) return false;
        fixups_.push_back(Fixup{token, destination, &Assign<T>});
        return true;
    }
    // Validate all references first; unresolved IDs leave every slot untouched.
    // Queued destinations must remain alive through successful resolution.
    bool ResolveAll(MemorySize& unresolved)
    {
        unresolved = 0;
        void* address = nullptr;
        for (const auto& fixup : fixups_) if (!Find(fixup.token, address)) ++unresolved;
        if (unresolved) return false;
        for (const auto& fixup : fixups_) {
            Find(fixup.token, address);
            fixup.assign(fixup.slot, address);
        }
        fixups_.clear();
        return true;
    }
    MemorySize PendingCount() const noexcept { return fixups_.size(); }
    void Clear() { fixups_.clear(); objects_.clear(); }
};
}} // namespace br::persistence
#endif
