#pragma once

#include <cassert>
#include <cstdint>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

#include "engine/component_pool.h"

struct IComponentPool {
    virtual ~IComponentPool() = default;
    virtual void ensure(uint32_t idx) = 0;
    virtual void reset(uint32_t idx) = 0;
    virtual void track(uint32_t idx) = 0;
    virtual void untrack(uint32_t idx) = 0;
    virtual const std::vector<uint32_t>& entities() const = 0;
};

template <typename T>
struct TypedComponentPool : IComponentPool {
    ComponentPool<T> pool;

    void ensure(uint32_t idx) override { pool.ensure(idx); }
    void reset(uint32_t idx) override { pool.reset(idx); }
    void track(uint32_t idx) override { pool.track(idx); }
    void untrack(uint32_t idx) override { pool.untrack(idx); }
    const std::vector<uint32_t>& entities() const override { return pool.entities; }
};

struct DynamicComponentEntry {
    uint64_t mask = 0;
    std::unique_ptr<IComponentPool> storage;
};

struct ComponentRegistry {
    // Bits 0-4 are reserved for static engine components.
    // TODO: Make fully dynamic? or reserve more bits for engine components.
    static constexpr uint32_t kFirstDynamicBit = 5;

    uint32_t nextBit = kFirstDynamicBit;
    std::unordered_map<std::type_index, DynamicComponentEntry> entries;

    template <typename T>
    bool isRegistered() const {
        return entries.find(std::type_index(typeid(T))) != entries.end();
    }

    template <typename T>
    void registerType() {
        const std::type_index key(typeid(T));
        if (entries.find(key) != entries.end())
            return;

        assert(nextBit < 64 && "Component mask is full (max 64 types)");
        DynamicComponentEntry entry;
        entry.mask = 1ull << nextBit++;
        entry.storage = std::make_unique<TypedComponentPool<T>>();
        entries.emplace(key, std::move(entry));
    }

    template <typename T>
    ComponentPool<T>& pool() {
        auto it = entries.find(std::type_index(typeid(T)));
        assert(it != entries.end() && "Component type was not registerComponent'd");
        return static_cast<TypedComponentPool<T>*>(it->second.storage.get())->pool;
    }

    template <typename T>
    const ComponentPool<T>& pool() const {
        auto it = entries.find(std::type_index(typeid(T)));
        assert(it != entries.end() && "Component type was not registerComponent'd");
        return static_cast<const TypedComponentPool<T>*>(it->second.storage.get())->pool;
    }

    template <typename T>
    uint64_t mask() const {
        auto it = entries.find(std::type_index(typeid(T)));
        assert(it != entries.end() && "Component type was not registerComponent'd");
        return it->second.mask;
    }

    void clearEntity(uint32_t idx) {
        for (auto& entry : entries) {
            entry.second.storage->untrack(idx);
            entry.second.storage->reset(idx);
        }
    }
};
