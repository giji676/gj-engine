#pragma once

#include <glm/glm.hpp>

#include "engine/camera.h"
#include "engine/entity.h"
#include "engine/input.h"

// Free-fly orbit camera for the editor viewport. Independent of the in-game camera.
class EditorCamera {
public:
    static constexpr float kDefaultDistance = 3.f;

    Camera& camera() { return camera_; }
    const Camera& camera() const { return camera_; }

    void syncFrom(const Camera& gameCam, float lookDistance = kDefaultDistance);
    void focusOn(const glm::vec3& worldPoint, float distance = kDefaultDistance);
    void frameOn(
        const glm::vec3& worldCenter,
        float worldExtent,
        Entity selectionId = Entity::invalid());

    void update(
        Input& input,
        const glm::vec4& viewportRect,
        bool active,
        Entity selectionId,
        const glm::vec3& selectionPivot);

private:
    void syncOrbitStateFromCamera();
    void retargetPivotKeepCamera(const glm::vec3& newPivot);
    void followSelectionPivot(const glm::vec3& newPivot);

    static bool mouseInViewport(glm::vec2 mouse, const glm::vec4& vp);

    Camera camera_;
    glm::vec3 pivot_{0.f, 2.f, 0.f};
    float yaw_ = 0.f;
    float pitch_ = 0.25f;
    float distance_ = kDefaultDistance;

    bool orbiting_ = false;
    bool panning_ = false;
    Entity trackedSelectionId_ = Entity::invalid();
};
