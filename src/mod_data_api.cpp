/**
 * @file mod_data_api.cpp
 * @brief The collections a mod keeps between calls: recompdata.h's functions.
 *
 * The other N64 recompilations give mods hashmaps, hashsets and slotmaps
 * held on the host side and reached through handles (their mod templates'
 * recompdata.h), and a mod written against that header imports these
 * thirty-five functions by name; without them the loader refuses it. This
 * file provides them, with the same names and the same meanings, so such a
 * mod loads here. It is a port of Zelda64Recomp's src/game/recomp_data_api.cpp
 * (GPLv3, the Zelda64Recomp contributors; NOTICE.md), with three faults of
 * that file put right: a slotmap read, write or erase of a missing key fell
 * through to touch a null element after saying it would return zero, and a
 * memory slotmap's erase freed the host pointer's own address rather than
 * the guest block it held.
 *
 * Every container carries its own mutex, as upstream's do: a mod may call
 * these from any game thread. The elements of the memory forms live in the
 * game's own RAM (recomp::alloc), zeroed on creation and freed with the
 * element or the container. An invalid handle is a fatal error in the mod:
 * the port stops with a message box naming the call, which is what the
 * other recompilations do and what a mod author would want to see.
 */
#include <cassert>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#include "ultramodern/error_handling.hpp"

#include "slot_map.h"

#include "mod_api.h"

namespace {

template <typename KeyType, typename ValueType>
class LockedMap {
public:
    bool get(const KeyType& key, ValueType& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }
        out = it->second;
        return true;
    }
    // Assigns whether or not the key was there; true when it was new.
    bool insert(const KeyType& key, ValueType value) {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.insert_or_assign(key, value).second;
    }
    bool erase(const KeyType& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.erase(key) != 0;
    }
    // Takes any one element out; false when the map is empty.
    bool erase_first(ValueType& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.begin();
        if (it == map_.end()) {
            return false;
        }
        out = it->second;
        map_.erase(it);
        return true;
    }
    bool contains(const KeyType& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.contains(key);
    }
    size_t size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.size();
    }

private:
    std::mutex mutex_;
    std::unordered_map<KeyType, ValueType> map_;
};

template <typename KeyType>
class LockedSet {
public:
    bool contains(const KeyType& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return set_.contains(key);
    }
    bool insert(const KeyType& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return set_.insert(key).second;
    }
    bool erase(const KeyType& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return set_.erase(key) != 0;
    }
    size_t size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return set_.size();
    }

private:
    std::mutex mutex_;
    std::unordered_set<KeyType> set_;
};

template <typename ValueType>
class LockedSlotmap {
public:
    using key_t = typename dod::slot_map32<ValueType>::key;

    // The element's address, valid until the next create or erase on this
    // map; null when the key is not in it.
    bool get(uint32_t key, ValueType** out) {
        std::lock_guard<std::mutex> lock(mutex_);
        ValueType* found = map_.get(key_t{key});
        *out = found;
        return found != nullptr;
    }
    uint32_t create() {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.emplace().raw;
    }
    bool erase(uint32_t key) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!map_.has_key(key_t{key})) {
            return false;
        }
        map_.erase(key_t{key});
        return true;
    }
    bool erase_first(ValueType& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.items().begin();
        if (it == map_.items().end()) {
            return false;
        }
        out = it->second;
        map_.erase(it->first);
        return true;
    }
    size_t size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.size();
    }

private:
    std::mutex mutex_;
    dod::slot_map32<ValueType> map_;
};

using U32ValueMap = LockedMap<uint32_t, uint32_t>;
using U32MemoryMap = std::pair<LockedMap<uint32_t, PTR(void)>, uint32_t>;   // .second: the element size
using U32HashSet = LockedSet<uint32_t>;
using U32Slotmap = LockedSlotmap<uint32_t>;
using MemorySlotmap = std::pair<LockedSlotmap<PTR(void)>, uint32_t>;        // .second: the element size

LockedSlotmap<U32ValueMap> u32_value_hashmaps;
LockedSlotmap<U32MemoryMap> u32_memory_hashmaps;
LockedSlotmap<U32HashSet> u32_hashsets;
LockedSlotmap<U32Slotmap> u32_slotmaps;
LockedSlotmap<MemorySlotmap> memory_slotmaps;

[[noreturn]] void fatal(const char* function, const char* what) {
    const std::string message = std::string("Fatal error in a mod: ") + function + " -- " + what;
    ultramodern::error_handling::message_box(message.c_str());
    ULTRAMODERN_QUICK_EXIT();
}

#define SNAP_HANDLE_INVALID() fatal(__func__, "the handle is not a live collection")

// A block of the game's RAM for one element: allocated from the runtime's
// heap, zeroed, and returned as the guest address the mod dereferences.
PTR(void) alloc_element(uint8_t* rdram, uint32_t size) {
    void* mem = recomp::alloc(rdram, size);
    const gpr addr = static_cast<gpr>(reinterpret_cast<uint8_t*>(mem) - rdram) + 0xFFFFFFFF80000000ULL;
    for (uint32_t i = 0; i < size; i++) {
        MEM_B(i, addr) = 0;
    }
    return static_cast<PTR(void)>(addr);
}

// -- u32 -> u32 hashmap ------------------------------------------------------

void recomputil_create_u32_value_hashmap(uint8_t* /*rdram*/, recomp_context* ctx) {
    _return(ctx, u32_value_hashmaps.create());
}

void recomputil_destroy_u32_value_hashmap(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    if (!u32_value_hashmaps.erase(handle)) {
        SNAP_HANDLE_INVALID();
    }
}

void recomputil_u32_value_hashmap_contains(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    U32ValueMap* map;
    if (!u32_value_hashmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, map->contains(key) ? 1 : 0);
}

void recomputil_u32_value_hashmap_insert(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    const uint32_t value = _arg<2, uint32_t>(rdram, ctx);
    U32ValueMap* map;
    if (!u32_value_hashmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, map->insert(key, value) ? 1 : 0);
}

void recomputil_u32_value_hashmap_get(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    const PTR(uint32_t) out = _arg<2, PTR(uint32_t)>(rdram, ctx);
    U32ValueMap* map;
    if (!u32_value_hashmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    uint32_t value;
    if (!map->get(key, value)) {
        _return<uint32_t>(ctx, 0);
        return;
    }
    MEM_W(0, out) = value;
    _return<uint32_t>(ctx, 1);
}

void recomputil_u32_value_hashmap_erase(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    U32ValueMap* map;
    if (!u32_value_hashmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, map->erase(key) ? 1 : 0);
}

void recomputil_u32_value_hashmap_size(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    U32ValueMap* map;
    if (!u32_value_hashmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, static_cast<uint32_t>(map->size()));
}

// -- u32 -> memory hashmap ---------------------------------------------------

void recomputil_create_u32_memory_hashmap(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t element_size = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t handle = u32_memory_hashmaps.create();
    U32MemoryMap* map;
    u32_memory_hashmaps.get(handle, &map);
    map->second = element_size;
    _return(ctx, handle);
}

void recomputil_destroy_u32_memory_hashmap(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    U32MemoryMap* map;
    if (!u32_memory_hashmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    PTR(void) element;
    while (map->first.erase_first(element)) {
        recomp::free(rdram, TO_PTR(void, element));
    }
    u32_memory_hashmaps.erase(handle);
}

void recomputil_u32_memory_hashmap_contains(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    U32MemoryMap* map;
    if (!u32_memory_hashmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, map->first.contains(key) ? 1 : 0);
}

void recomputil_u32_memory_hashmap_create(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    U32MemoryMap* map;
    if (!u32_memory_hashmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    PTR(void) existing;
    if (map->first.get(key, existing)) {
        _return<uint32_t>(ctx, 0);
        return;
    }
    map->first.insert(key, alloc_element(rdram, map->second));
    _return<uint32_t>(ctx, 1);
}

void recomputil_u32_memory_hashmap_get(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    U32MemoryMap* map;
    if (!u32_memory_hashmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    PTR(void) element;
    if (!map->first.get(key, element)) {
        _return<PTR(void)>(ctx, NULLPTR);
        return;
    }
    _return<PTR(void)>(ctx, element);
}

void recomputil_u32_memory_hashmap_erase(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    U32MemoryMap* map;
    if (!u32_memory_hashmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    PTR(void) element;
    if (map->first.get(key, element)) {
        recomp::free(rdram, TO_PTR(void, element));
    }
    _return<uint32_t>(ctx, map->first.erase(key) ? 1 : 0);
}

void recomputil_u32_memory_hashmap_size(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    U32MemoryMap* map;
    if (!u32_memory_hashmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, static_cast<uint32_t>(map->first.size()));
}

// -- u32 hashset -------------------------------------------------------------

void recomputil_create_u32_hashset(uint8_t* /*rdram*/, recomp_context* ctx) {
    _return(ctx, u32_hashsets.create());
}

void recomputil_destroy_u32_hashset(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    if (!u32_hashsets.erase(handle)) {
        SNAP_HANDLE_INVALID();
    }
}

void recomputil_u32_hashset_contains(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    U32HashSet* set;
    if (!u32_hashsets.get(handle, &set)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, set->contains(key) ? 1 : 0);
}

void recomputil_u32_hashset_insert(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    U32HashSet* set;
    if (!u32_hashsets.get(handle, &set)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, set->insert(key) ? 1 : 0);
}

void recomputil_u32_hashset_erase(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    U32HashSet* set;
    if (!u32_hashsets.get(handle, &set)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, set->erase(key) ? 1 : 0);
}

void recomputil_u32_hashset_size(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    U32HashSet* set;
    if (!u32_hashsets.get(handle, &set)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, static_cast<uint32_t>(set->size()));
}

// -- u32 slotmap -------------------------------------------------------------

void recomputil_create_u32_slotmap(uint8_t* /*rdram*/, recomp_context* ctx) {
    _return(ctx, u32_slotmaps.create());
}

void recomputil_destroy_u32_slotmap(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    if (!u32_slotmaps.erase(handle)) {
        SNAP_HANDLE_INVALID();
    }
}

void recomputil_u32_slotmap_contains(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    U32Slotmap* map;
    if (!u32_slotmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    uint32_t* element;
    _return<uint32_t>(ctx, map->get(key, &element) ? 1 : 0);
}

void recomputil_u32_slotmap_create(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    U32Slotmap* map;
    if (!u32_slotmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    _return(ctx, map->create());
}

void recomputil_u32_slotmap_get(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    const PTR(uint32_t) out = _arg<2, PTR(uint32_t)>(rdram, ctx);
    U32Slotmap* map;
    if (!u32_slotmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    uint32_t* element;
    if (!map->get(key, &element)) {
        _return<uint32_t>(ctx, 0);
        return;
    }
    MEM_W(0, out) = *element;
    _return<uint32_t>(ctx, 1);
}

void recomputil_u32_slotmap_set(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    const uint32_t value = _arg<2, uint32_t>(rdram, ctx);
    U32Slotmap* map;
    if (!u32_slotmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    uint32_t* element;
    if (!map->get(key, &element)) {
        _return<uint32_t>(ctx, 0);
        return;
    }
    *element = value;
    _return<uint32_t>(ctx, 1);
}

void recomputil_u32_slotmap_erase(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    U32Slotmap* map;
    if (!u32_slotmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, map->erase(key) ? 1 : 0);
}

void recomputil_u32_slotmap_size(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    U32Slotmap* map;
    if (!u32_slotmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, static_cast<uint32_t>(map->size()));
}

// -- memory slotmap ----------------------------------------------------------

void recomputil_create_memory_slotmap(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t element_size = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t handle = memory_slotmaps.create();
    MemorySlotmap* map;
    memory_slotmaps.get(handle, &map);
    map->second = element_size;
    _return(ctx, handle);
}

void recomputil_destroy_memory_slotmap(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    MemorySlotmap* map;
    if (!memory_slotmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    PTR(void) element;
    while (map->first.erase_first(element)) {
        recomp::free(rdram, TO_PTR(void, element));
    }
    memory_slotmaps.erase(handle);
}

void recomputil_memory_slotmap_contains(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    MemorySlotmap* map;
    if (!memory_slotmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    PTR(void)* element;
    _return<uint32_t>(ctx, map->first.get(key, &element) ? 1 : 0);
}

void recomputil_memory_slotmap_create(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    MemorySlotmap* map;
    if (!memory_slotmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    const uint32_t key = map->first.create();
    PTR(void)* element;
    map->first.get(key, &element);
    *element = alloc_element(rdram, map->second);
    _return(ctx, key);
}

void recomputil_memory_slotmap_get(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    const PTR(uint32_t) out = _arg<2, PTR(uint32_t)>(rdram, ctx);
    MemorySlotmap* map;
    if (!memory_slotmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    // recompdata.h promises 0 for a key that is not there (upstream stops
    // the program instead; the header is the contract mods were written to).
    PTR(void)* element;
    if (!map->first.get(key, &element)) {
        _return<uint32_t>(ctx, 0);
        return;
    }
    MEM_W(0, out) = static_cast<uint32_t>(*element);
    _return<uint32_t>(ctx, 1);
}

void recomputil_memory_slotmap_erase(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t key = _arg<1, uint32_t>(rdram, ctx);
    MemorySlotmap* map;
    if (!memory_slotmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    PTR(void)* element;
    if (map->first.get(key, &element)) {
        recomp::free(rdram, TO_PTR(void, *element));
    }
    _return<uint32_t>(ctx, map->first.erase(key) ? 1 : 0);
}

void recomputil_memory_slotmap_size(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t handle = _arg<0, uint32_t>(rdram, ctx);
    MemorySlotmap* map;
    if (!memory_slotmaps.get(handle, &map)) {
        SNAP_HANDLE_INVALID();
    }
    _return<uint32_t>(ctx, static_cast<uint32_t>(map->first.size()));
}

} // namespace

namespace snap {

#define SNAP_EXPORT(name) recomp::overlays::register_base_export(#name, name)

void register_data_api_exports() {
    SNAP_EXPORT(recomputil_create_u32_value_hashmap);
    SNAP_EXPORT(recomputil_destroy_u32_value_hashmap);
    SNAP_EXPORT(recomputil_u32_value_hashmap_contains);
    SNAP_EXPORT(recomputil_u32_value_hashmap_insert);
    SNAP_EXPORT(recomputil_u32_value_hashmap_get);
    SNAP_EXPORT(recomputil_u32_value_hashmap_erase);
    SNAP_EXPORT(recomputil_u32_value_hashmap_size);

    SNAP_EXPORT(recomputil_create_u32_memory_hashmap);
    SNAP_EXPORT(recomputil_destroy_u32_memory_hashmap);
    SNAP_EXPORT(recomputil_u32_memory_hashmap_contains);
    SNAP_EXPORT(recomputil_u32_memory_hashmap_create);
    SNAP_EXPORT(recomputil_u32_memory_hashmap_get);
    SNAP_EXPORT(recomputil_u32_memory_hashmap_erase);
    SNAP_EXPORT(recomputil_u32_memory_hashmap_size);

    SNAP_EXPORT(recomputil_create_u32_hashset);
    SNAP_EXPORT(recomputil_destroy_u32_hashset);
    SNAP_EXPORT(recomputil_u32_hashset_contains);
    SNAP_EXPORT(recomputil_u32_hashset_insert);
    SNAP_EXPORT(recomputil_u32_hashset_erase);
    SNAP_EXPORT(recomputil_u32_hashset_size);

    SNAP_EXPORT(recomputil_create_u32_slotmap);
    SNAP_EXPORT(recomputil_destroy_u32_slotmap);
    SNAP_EXPORT(recomputil_u32_slotmap_contains);
    SNAP_EXPORT(recomputil_u32_slotmap_create);
    SNAP_EXPORT(recomputil_u32_slotmap_get);
    SNAP_EXPORT(recomputil_u32_slotmap_set);
    SNAP_EXPORT(recomputil_u32_slotmap_erase);
    SNAP_EXPORT(recomputil_u32_slotmap_size);

    SNAP_EXPORT(recomputil_create_memory_slotmap);
    SNAP_EXPORT(recomputil_destroy_memory_slotmap);
    SNAP_EXPORT(recomputil_memory_slotmap_contains);
    SNAP_EXPORT(recomputil_memory_slotmap_create);
    SNAP_EXPORT(recomputil_memory_slotmap_get);
    SNAP_EXPORT(recomputil_memory_slotmap_erase);
    SNAP_EXPORT(recomputil_memory_slotmap_size);
}

} // namespace snap
