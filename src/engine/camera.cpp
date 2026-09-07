#include "camera.h"

#include "engine/engine.h"

void Camera::lateUpdate() {
    Engine& engine = ENGINE();
    if (!engine.scene.isValid(entity) || !engine.scene.has<Transform>(entity))
        return;

    const glm::mat4& world = engine.scene.get<Transform>(entity).worldMatrix;
    pos = glm::vec3(world[3]);

    const glm::vec3 worldUp = glm::vec3(world[1]);
    const glm::vec3 worldBack = glm::vec3(world[2]);
    const float upLen = glm::length(worldUp);
    const float backLen = glm::length(worldBack);
    if (upLen > 1e-8f)
        up = worldUp / upLen;
    if (backLen > 1e-8f)
        front = -worldBack / backLen;
}
