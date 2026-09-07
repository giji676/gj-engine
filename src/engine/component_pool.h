#pragma once

#include <cstdint>
#include <vector>

template <typename T>
struct ComponentPool {
    std::vector<T> values;
    std::vector<uint32_t> entities;
    std::vector<int32_t> denseIndex;

    void ensure(uint32_t idx) {
        const size_t need = static_cast<size_t>(idx) + 1;
        if (values.size() < need)
            values.resize(need);
    }

    T& at(uint32_t idx) { return values[idx]; }

    const T& at(uint32_t idx) const { return values[idx]; }

    void reset(uint32_t idx) {
        ensure(idx);
        values[idx] = T{};
    }

    void track(uint32_t idx) {
        if (idx >= denseIndex.size())
            denseIndex.resize(static_cast<size_t>(idx) + 1, -1);
        if (denseIndex[idx] >= 0)
            return;

        denseIndex[idx] = static_cast<int32_t>(entities.size());
        entities.push_back(idx);
    }

    void untrack(uint32_t idx) {
        if (idx >= denseIndex.size() || denseIndex[idx] < 0)
            return;

        const int32_t pos = denseIndex[idx];
        const uint32_t last = entities.back();
        entities[static_cast<size_t>(pos)] = last;
        denseIndex[last] = pos;
        entities.pop_back();
        denseIndex[idx] = -1;
    }
};
