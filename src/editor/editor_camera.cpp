#include "editor/editor_camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/asset_manager/model.h"
#include "engine/utils/geometry.h"

namespace {

constexpr float kOrbitSensitivity = 0.005f;
constexpr float kPanScale = 0.002f;
constexpr float kOrbitEpsilon = 1e-4f;
constexpr float kMinPitch = -1.55f;
constexpr float kMaxPitch = 1.55f;
constexpr float kDollyStep = 0.15f;
constexpr float kFineControlScale = 0.15f;
constexpr float kPivotFollowEpsilon = 1e-4f;
constexpr float kFramePadding = 1.25f;
constexpr float kMinFrameExtent = 0.25f;

float fineScale(Input& input) {
    return input.shiftDown() ? kFineControlScale : 1.f;
}

glm::vec3 safeNormalize(glm::vec3 v, glm::vec3 fallback) {
    const float len = glm::length(v);
    if (len < 1e-8f)
        return fallback;
    return v / len;
}

} // namespace

void EditorCamera::syncFrom(const Camera& gameCam, float lookDistance) {
    camera_.pos = gameCam.pos;
    camera_.front = safeNormalize(gameCam.front, {0.f, 0.f, -1.f});
    camera_.up = safeNormalize(gameCam.up, {0.f, 1.f, 0.f});

    pivot_ = gameCam.pos + gameCam.front * lookDistance;
    trackedSelectionId_ = Entity::invalid();
    syncOrbitStateFromCamera();
}

void EditorCamera::focusOn(const glm::vec3& worldPoint, float distance) {
    pivot_ = worldPoint;
    trackedSelectionId_ = Entity::invalid();
    distance_ = std::max(distance, kOrbitEpsilon);
    camera_.pos = pivot_ - camera_.front * distance_;
    syncOrbitStateFromCamera();
}

void EditorCamera::frameOn(
    const glm::vec3& worldCenter,
    float worldExtent,
    Entity selectionId)
{
    const float extent = std::max(worldExtent, kMinFrameExtent);
    distance_ = std::max(extent * kFramePadding * 2.f, kDefaultDistance);

    pivot_ = worldCenter;
    trackedSelectionId_ = selectionId;

    const glm::vec3 forward = safeNormalize(camera_.front, {0.f, 0.f, -1.f});
    camera_.pos = pivot_ - forward * distance_;
    syncOrbitStateFromCamera();
}

void EditorCamera::update(
    Input& input,
    const glm::vec4& viewportRect,
    bool active,
    Entity selectionId,
    const glm::vec3& selectionPivot)
{
    if (!active) {
        orbiting_ = false;
        panning_ = false;
        return;
    }

    const bool hasSelection = selectionId != Entity::invalid();

    if (hasSelection) {
        if (selectionId != trackedSelectionId_) {
            retargetPivotKeepCamera(selectionPivot);
            trackedSelectionId_ = selectionId;
        } else if (glm::length(selectionPivot - pivot_) > kPivotFollowEpsilon) {
            followSelectionPivot(selectionPivot);
        }
    } else {
        trackedSelectionId_ = Entity::invalid();
    }

    const glm::vec2 mouse = input.mousePosition();
    const bool inViewport = mouseInViewport(mouse, viewportRect);
    const float fine = fineScale(input);

    if (input.pressed(MouseAction::Middle) && inViewport)
        orbiting_ = true;
    if (input.pressed(MouseAction::Right) && inViewport)
        panning_ = true;
    if (input.released(MouseAction::Middle))
        orbiting_ = false;
    if (input.released(MouseAction::Right))
        panning_ = false;

    // Orbit: rotate camera position and orientation around pivot.
    if (orbiting_ && input.down(MouseAction::Middle)) {
        const float yawDelta = -input.mouseDeltaX * kOrbitSensitivity * fine;
        const float pitchDelta = -input.mouseDeltaY * kOrbitSensitivity * fine;

        glm::vec3 offset = camera_.pos - pivot_;
        offset = glm::rotate(glm::mat4(1.f), yawDelta, glm::vec3(0.f, 1.f, 0.f)) * glm::vec4(offset, 1.f);

        const glm::vec3 right = glm::normalize(
            glm::cross(camera_.front, camera_.up));
        offset = glm::rotate(glm::mat4(1.f), pitchDelta, right) * glm::vec4(offset, 1.f);

        camera_.pos = pivot_ + offset;
        camera_.front = glm::rotate(glm::mat4(1.f), yawDelta, glm::vec3(0.f, 1.f, 0.f))
            * glm::vec4(camera_.front, 0.f);
        camera_.front = glm::rotate(glm::mat4(1.f), pitchDelta, right)
            * glm::vec4(camera_.front, 0.f);
        camera_.front = safeNormalize(camera_.front, {0.f, 0.f, -1.f});
        camera_.up = glm::rotate(glm::mat4(1.f), yawDelta, glm::vec3(0.f, 1.f, 0.f))
            * glm::vec4(camera_.up, 0.f);
        camera_.up = glm::rotate(glm::mat4(1.f), pitchDelta, right)
            * glm::vec4(camera_.up, 0.f);
        camera_.up = safeNormalize(camera_.up, {0.f, 1.f, 0.f});

        syncOrbitStateFromCamera();
    }

    if (panning_ && input.down(MouseAction::Right)) {
        const glm::vec3 right =
            glm::normalize(glm::cross(camera_.front, camera_.up));
        const glm::vec3 up =
            glm::normalize(glm::cross(right, camera_.front));
        const float scale = distance_ * kPanScale * fine;

        if (hasSelection) {
            camera_.pos -= right * (input.mouseDeltaX * scale);
            camera_.pos += up * (input.mouseDeltaY * scale);
            followSelectionPivot(selectionPivot);
        } else {
            pivot_ -= right * (input.mouseDeltaX * scale);
            pivot_ += up * (input.mouseDeltaY * scale);
            camera_.pos -= right * (input.mouseDeltaX * scale);
            camera_.pos += up * (input.mouseDeltaY * scale);
            syncOrbitStateFromCamera();
        }
    }

    const float wheel = input.mouseWheelY();
    if (inViewport && std::abs(wheel) > 1e-6f) {
        const glm::vec3 forward = safeNormalize(camera_.front, {0.f, 0.f, -1.f});
        const float stepScale = glm::clamp(distance_, 0.25f, 30.f) * kDollyStep * fine;

        camera_.pos += forward * (wheel * stepScale);

        if (hasSelection)
            followSelectionPivot(selectionPivot);
        else {
            pivot_ += forward * (wheel * stepScale);
            syncOrbitStateFromCamera();
        }
    }
}

void EditorCamera::retargetPivotKeepCamera(const glm::vec3& newPivot) {
    pivot_ = newPivot;
    syncOrbitStateFromCamera();
}

void EditorCamera::followSelectionPivot(const glm::vec3& newPivot) {
    pivot_ = newPivot;
    distance_ = glm::length(pivot_ - camera_.pos);
    distance_ = std::max(distance_, kOrbitEpsilon);
}

void EditorCamera::syncOrbitStateFromCamera() {
    distance_ = glm::length(pivot_ - camera_.pos);
    distance_ = std::max(distance_, kOrbitEpsilon);

    const glm::vec3 dir = safeNormalize(camera_.front, {0.f, 0.f, -1.f});
    pitch_ = std::asin(glm::clamp(dir.y, -1.f, 1.f));
    yaw_ = std::atan2(dir.x, dir.z);
}

bool EditorCamera::mouseInViewport(glm::vec2 mouse, const glm::vec4& vp) {
    if (vp.z <= 1.f || vp.w <= 1.f)
        return false;
    return pointInRect(mouse, {vp.x, vp.y}, {vp.z, vp.w});
}
