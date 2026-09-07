#pragma once

#include "engine/systems/isystem.h"
#include "engine/scene.h"

struct SpinSystem : ISystem {
    void update(Scene& w, float dt) override {
        if (!w.tagRegistry.hasName("spin"))
            return;

        const uint32_t spinTag = w.tagRegistry.byName("spin");
        for (uint32_t idx : w.tagRegistry.entities(spinTag)) {
            if (!w.slots[idx].alive)
                continue;

            const Entity e = {idx, w.slots[idx].gen};
            if (!w.has<Transform>(e))
                continue;

            Transform& transform = w.get<Transform>(e);
            transform.rotation.y += dt * 45.f;
        }
    }
};
