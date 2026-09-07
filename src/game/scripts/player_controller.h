#pragma once

#include <cmath>

#include "engine/iscript.h"
#include "engine/engine.h"
#include "engine/scene.h"
#include "game/world.h"

class PlayerController : public IScript {
public:
    explicit PlayerController(World_* terrain)
        : terrain(terrain)
    {
        INSPECT(speed);
        INSPECT(jumpForce);
        INSPECT(lookSensitivity);
        INSPECT(recoilAngle);
    }

    glm::vec3 velocity{0.f};
    bool grounded = false;
    float speed = 1.0f;
    float jumpForce = 3.0f;
    float lookSensitivity = 0.1f;
    float yaw = 0.f;
    float pitch = 0.f;
    float recoilAngle = 5.f;

    void init() override {}
    const char* typeName() const override { return "PlayerController"; }

    void update() override {
        Engine& engine = ENGINE();
        if (!engine.scene.isValid(entity) || !engine.scene.has<Transform>(entity))
            return;

        Input& input = engine.input;
        const float dt = DT();

        yaw -= input.mouseDeltaX * lookSensitivity;
        pitch -= input.mouseDeltaY * lookSensitivity;
        pitch = glm::clamp(pitch, -89.0f, 89.0f);

        const glm::vec3 look = lookDirection();
        const glm::vec3 worldUp(0.f, 1.f, 0.f);
        const glm::vec3 right = glm::normalize(glm::cross(look, worldUp));

        Transform& transform = engine.scene.get<Transform>(entity);
        glm::vec3 pos = transform.position;

        const float moveSpeed = speed * dt;
        if (input.down(Action::MoveForward))
            pos += moveSpeed * look;
        if (input.down(Action::MoveBackward))
            pos -= moveSpeed * look;
        if (input.down(Action::MoveLeft))
            pos -= moveSpeed * right;
        if (input.down(Action::MoveRight))
            pos += moveSpeed * right;

        if (input.pressed(Action::Jump) && grounded)
            velocity.y = jumpForce;

        velocity.y += -engine.G * dt;
        pos.y += velocity.y * dt;

        if (terrain)
            resolveGround(pos);

        transform.position = pos;
        transform.rotation = {pitch, yaw, 0.f};
    }

private:
    World_* terrain = nullptr;

    glm::vec3 lookDirection() const {
        return glm::normalize(glm::vec3(
            -sin(glm::radians(yaw)) * cos(glm::radians(pitch)),
             sin(glm::radians(pitch)),
            -cos(glm::radians(yaw)) * cos(glm::radians(pitch))));
    }

    void resolveGround(glm::vec3& pos) {
        const float halfW = (terrain->width - 1) * terrain->scale * 0.5f;
        const float halfH = (terrain->height - 1) * terrain->scale * 0.5f;

        int x = static_cast<int>(floor((pos.x + halfW) / terrain->scale));
        int z = static_cast<int>(floor((pos.z + halfH) / terrain->scale));
        x = glm::clamp(x, 0, terrain->width - 1);
        z = glm::clamp(z, 0, terrain->height - 1);

        const int idx = z * terrain->width + x;
        const float groundY = terrain->terrain.vertices[idx * 6 + 1];

        if (pos.y <= groundY) {
            velocity.y = 0.f;
            pos.y = groundY;
            grounded = true;
        } else {
            grounded = false;
        }
    }
};
