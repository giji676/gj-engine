#pragma once

#include <cstdint>
#include <functional>

struct Entity {
    uint32_t idx = UINT32_MAX;
    uint32_t gen = 0;

    static Entity invalid() { return {UINT32_MAX, 0}; }

    bool operator==(const Entity& other) const {
        return idx == other.idx && gen == other.gen;
    }

    bool operator!=(const Entity& other) const {
        return !(*this == other);
    }
};

namespace std {
template <>
struct hash<Entity> {
    size_t operator()(const Entity& e) const noexcept {
        return (static_cast<size_t>(e.gen) << 32) ^ static_cast<size_t>(e.idx);
    }
};
}
