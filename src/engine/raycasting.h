#pragma once

#include <cfloat>
#include <optional>
#include <glm/glm.hpp>

#include "engine/defines.h"
#include "engine/entity.h"

struct triangle3 {
    glm::vec3 a;
    glm::vec3 b;
    glm::vec3 c;
};

struct RaycastHit {
    Entity entity = Entity::invalid();
    float distance = FLT_MAX;
};

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
    float t = FLT_MAX;
};

struct TriangleHit {
    float t;
    glm::vec3 position;
    glm::vec3 normal;
};

class Raycasting {
public:
    RaycastHit castRay(const Ray& ray);

    void checkIntersect(
        Entity e,
        const Ray& ray,
        RaycastHit& bestHit);

    static bool testSphereIntersection(
        const Ray& ray,
        const glm::vec3& sphereCenter,
        float radius,
        float& intersectionDistance);

    static bool testPlaneIntersection(
        const Ray& ray,
        const glm::vec3& planePoint,
        const glm::vec3& planeNormal,
        glm::vec3& outHit);

    // Closest distance between a ray and a finite segment [a, b].
    // outRayT is the ray parameter of the closest point (must be >= 0).
    static bool testRaySegmentDistance(
        const Ray& ray,
        const glm::vec3& a,
        const glm::vec3& b,
        float& outDistance,
        float& outRayT);

    static std::optional<TriangleHit> testTriangleIntersection(
        const Ray& ray,
        const Triangle& triangle);
};
