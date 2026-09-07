#pragma once

#include <algorithm>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/scene.h"

struct TransformSystem {
    std::vector<uint32_t> order;
    std::vector<uint32_t> depth;

    void update(Scene& w) {
        rebuildOrder(w);

        for (uint32_t idx : order) {
            const Entity e = {idx, w.slots[idx].gen};
            Transform& transform = w.get<Transform>(e);
            const glm::mat4 local = transformToMatrix(
                transform.position,
                transform.rotation,
                transform.scale);

            glm::mat4 world = local;
            if (w.has<Hierarchy>(e)) {
                const Entity p = w.get<Hierarchy>(e).parent;
                if (w.isValid(p) && w.has<Transform>(p))
                    world = w.get<Transform>(p).worldMatrix * local;
            }

            transform.worldMatrix = world;
            transform.worldInvMatrix = glm::inverse(world);
        }
    }

    void rebuildOrder(Scene& w) {
        order.clear();
        depth.assign(w.slots.size(), 0);

        for (uint32_t idx : w.entitiesWith<Transform>()) {
            if (!w.slots[idx].alive)
                continue;
            const Entity e = {idx, w.slots[idx].gen};
            depth[idx] = depthOf(w, e);
            order.push_back(idx);
        }

        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            if (depth[a] != depth[b])
                return depth[a] < depth[b];
            return a < b;
        });
    }

    uint32_t depthOf(Scene& w, Entity start) const {
        uint32_t d = 0;
        Entity cur = start;
        const uint32_t hopLimit = static_cast<uint32_t>(w.slots.size());

        for (uint32_t hops = 0; hops < hopLimit; ++hops) {
            if (!w.has<Hierarchy>(cur))
                return d;

            const Entity p = w.get<Hierarchy>(cur).parent;
            if (!w.isValid(p) || !w.has<Transform>(p))
                return d;
            if (p.idx == start.idx)
                return d;

            cur = p;
            ++d;
        }
        return d;
    }

    static glm::mat4 transformToMatrix(
        const glm::vec3& pos,
        const glm::vec3& rot,
        const glm::vec3& scale)
    {
        glm::mat4 m(1.0f);

        m = glm::translate(m, pos);

        // YXZ: yaw around Y, then pitch around X, then roll around Z.
        m = glm::rotate(m, glm::radians(rot.y), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(rot.x), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(rot.z), glm::vec3(0, 0, 1));

        m = glm::scale(m, scale);

        return m;
    }
};
