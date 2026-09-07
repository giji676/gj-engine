#pragma once

#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/engine.h"
#include "engine/frustrum.h"
#include "engine/renderer/debug_renderer.h"
#include "engine/renderer/renderer.h"
#include "engine/scene.h"

struct RenderSystem {
    std::vector<RenderCommand> commands;
    size_t lastSize = 0;

    void update(Scene& w, const Frustum& frustum) {
        commands.clear();
        commands.reserve(lastSize);

        Engine& engine = ENGINE();

        for (uint32_t idx : w.entitiesWith<Object>()) {
            if (!w.slots[idx].alive)
                continue;

            const Entity e = {idx, w.slots[idx].gen};
            if (!w.has<Transform>(e))
                continue;

            const Object& object = w.get<Object>(e);
            const glm::mat4& world = w.get<Transform>(e).worldMatrix;

            if (object.model) {
                glm::vec3 wMin, wMax;
                transformAABB(object.model->getBounds(), world, wMin, wMax);
                if (!frustum.intersectsAABB(wMin, wMax))
                    continue;
            }

            if (object.debug) {
                DebugRenderer& debug = engine.debugRenderer;
                debug.axis(world, 2.5f);
                if (object.model) {
                    const Bounds& bounds = object.model->getBounds();
                    debug.box(
                        world * glm::translate(glm::mat4(1.f), bounds.center),
                        bounds.size,
                        {1.f, 1.f, 1.f});
                }
            }

            if (!object.model)
                continue;

            for (const auto& part : object.model->getParts()) {
                RenderCommand cmd;
                cmd.mesh = &part.mesh;
                cmd.material = part.material;
                cmd.model = world;
                cmd.sortKey =
                    (uint64_t)(cmd.material->usesTransparency() ? 1 : 0) << 63 |
                    (uint64_t)cmd.material->shader->ID << 48 |
                    (uint64_t)cmd.material->id << 32 |
                    (uint64_t)cmd.mesh->id << 16;
                cmd.allocation = &engine.meshRegistry.getAllocation(&part.mesh);
                commands.push_back(cmd);
            }
        }

        lastSize = commands.size();
    }
};
