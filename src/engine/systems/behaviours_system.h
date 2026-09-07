#pragma once

#include "engine/scene.h"

struct BehavioursSystem {
    void update(Scene& w) {
        for (uint32_t idx : w.entitiesWith<Behaviours>()) {
            if (!w.slots[idx].alive)
                continue;

            const Entity e = {idx, w.slots[idx].gen};
            Behaviours& ib = w.get<Behaviours>(e);

            for (auto& component : ib.components) {
                if (component->enabled)
                    component->update();
            }
            for (auto& script : ib.scripts) {
                if (script->enabled)
                    script->update();
            }
        }
    }

    void lateUpdate(Scene& w) {
        for (uint32_t idx : w.entitiesWith<Behaviours>()) {
            if (!w.slots[idx].alive)
                continue;

            const Entity e = {idx, w.slots[idx].gen};
            Behaviours& ib = w.get<Behaviours>(e);

            for (auto& component : ib.components) {
                if (component->enabled)
                    component->lateUpdate();
            }
            for (auto& script : ib.scripts) {
                if (script->enabled)
                    script->lateUpdate();
            }
        }
    }
};
