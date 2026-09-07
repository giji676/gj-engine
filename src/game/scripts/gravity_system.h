#pragma once

#include "engine/systems/isystem.h"
#include "engine/scene.h"
#include "game/components/gravity.h"

struct GravitySystem : ISystem {
    void update(Scene& w, float dt) override {
        for (uint32_t idx : w.entitiesWith<Gravity>()) {
            if (!w.slots[idx].alive)
                continue;

            const Entity e = {idx, w.slots[idx].gen};
            if (!w.has<Transform>(e))
                continue;

            Gravity& gravity = w.get<Gravity>(e);
            Transform& transform = w.get<Transform>(e);

            gravity.velocity += gravity.acceleration * dt;
            transform.position += gravity.velocity * dt;
        }
    }
};
