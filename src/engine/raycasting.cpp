#include <algorithm>
#include <limits>

#include "raycasting.h"
#include "engine.h"
#include "engine/asset_manager/model.h"
#include "engine/profilers/profile_scope.h"
#include "glm/geometric.hpp"

RaycastHit Raycasting::castRay(const Ray& ray) {
    PROFILE_SCOPE("Raycasting::castRay");
    RaycastHit hit;

    Scene& scene = ENGINE().scene;
    if (scene.isValid(scene.root))
        checkIntersect(scene.root, ray, hit);

    return hit;
}

void Raycasting::checkIntersect(
    Entity e,
    const Ray& ray,
    RaycastHit& bestHit)
{
    Scene& scene = ENGINE().scene;
    if (!scene.isValid(e))
        return;

    if (scene.has<Object>(e) && scene.has<Transform>(e)) {
        const Object& object = scene.get<Object>(e);
        const Transform& transform = scene.get<Transform>(e);
        const glm::mat4& worldMat = transform.worldMatrix;

        if (object.model) {
            const Bounds& bounds = object.model->getBounds();
            glm::vec3 size = bounds.size;
            if (size != glm::vec3(0.0f)) {
                glm::vec3 worldScale(
                    glm::length(glm::vec3(worldMat[0])),
                    glm::length(glm::vec3(worldMat[1])),
                    glm::length(glm::vec3(worldMat[2]))
                );

                float radius =
                    std::max({
                        size.x * worldScale.x,
                        size.y * worldScale.y,
                        size.z * worldScale.z
                    }) * 0.5f;

                glm::vec3 center =
                    glm::vec3(worldMat * glm::vec4(bounds.center, 1.0f));

                // TODO: double check radius calculation
                // backpacks torch thing isn't being hit because of radius sometimes
                float distance;
                if (testSphereIntersection(ray, center, radius * 1.5f, distance)) {
                    Ray localRay;
                    localRay.origin = glm::vec3(
                        transform.worldInvMatrix * glm::vec4(ray.origin, 1.0f));
                    localRay.direction = glm::normalize(glm::vec3(
                        transform.worldInvMatrix * glm::vec4(ray.direction, 0.0f)));

                    object.model->bvh.intersectBVH(
                        localRay,
                        object.model->bvh.rootNodeIdx,
                        worldMat);

                    if (localRay.t < bestHit.distance) {
                        bestHit.distance = localRay.t;
                        bestHit.entity = e;
                    }
                }
            }
        }
    }

    if (!scene.has<Hierarchy>(e))
        return;

    for (const Entity& child : scene.get<Hierarchy>(e).children)
        checkIntersect(child, ray, bestHit);
}

bool Raycasting::testSphereIntersection(
        const Ray& ray,
        const glm::vec3& sphereCenter,
        float radius,
        float& intersectionDistance)
{
    glm::vec3 oc = ray.origin - sphereCenter;

    float a = glm::dot(ray.direction, ray.direction);
    float b = 2.0f * glm::dot(oc, ray.direction);
    float c = glm::dot(oc, oc) - radius * radius;

    float discriminant = b * b - 4.0f * a * c;

    if (discriminant < 0.0f) {
        return false;
    }

    float sqrtD = std::sqrt(discriminant);

    float t1 = (-b - sqrtD) / (2.0f * a);
    float t2 = (-b + sqrtD) / (2.0f * a);

    if (t1 >= 0.0f) {
        intersectionDistance = t1;
        return true;
    }

    if (t2 >= 0.0f) {
        intersectionDistance = t2;
        return true;
    }

    return false;
}

bool Raycasting::testPlaneIntersection(
        const Ray& ray,
        const glm::vec3& planePoint,
        const glm::vec3& planeNormal,
        glm::vec3& outHit)
{
    const float denom = glm::dot(ray.direction, planeNormal);
    if (std::abs(denom) < 1e-6f)
        return false;

    const float t = glm::dot(planePoint - ray.origin, planeNormal) / denom;
    if (t < 0.f)
        return false;

    outHit = ray.origin + ray.direction * t;
    return true;
}

bool Raycasting::testRaySegmentDistance(
        const Ray& ray,
        const glm::vec3& a,
        const glm::vec3& b,
        float& outDistance,
        float& outRayT)
{
    const glm::vec3 d1 = ray.direction;
    const glm::vec3 d2 = b - a;
    const glm::vec3 r = ray.origin - a;

    const float aa = glm::dot(d1, d1);
    const float bb = glm::dot(d1, d2);
    const float cc = glm::dot(d2, d2);
    const float dd = glm::dot(d1, r);
    const float ee = glm::dot(d2, r);

    if (aa < 1e-12f || cc < 1e-12f)
        return false;

    const float denom = aa * cc - bb * bb;
    float rayT;
    float segT;

    if (denom < 1e-8f) {
        segT = 0.f;
        rayT = -dd / aa;
    } else {
        rayT = (bb * ee - cc * dd) / denom;
        segT = (aa * ee - bb * dd) / denom;
    }

    segT = std::clamp(segT, 0.f, 1.f);
    rayT = (bb * segT - dd) / aa;

    if (rayT < 0.f) {
        rayT = 0.f;
        segT = std::clamp(ee / cc, 0.f, 1.f);
    }

    const glm::vec3 closestRay = ray.origin + d1 * rayT;
    const glm::vec3 closestSeg = a + d2 * segT;
    outDistance = glm::length(closestRay - closestSeg);
    outRayT = rayT;
    return true;
}

std::optional<TriangleHit> Raycasting::testTriangleIntersection(
        const Ray& ray,
        const Triangle& triangle)
{
    // From https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
    constexpr float epsilon = std::numeric_limits<float>::epsilon();

    glm::vec3 edge1 = triangle.v1 - triangle.v0;
    glm::vec3 edge2 = triangle.v2 - triangle.v0;

    const glm::vec3 normal = cross(edge1, edge2);
    if (dot(normal, ray.direction) > 0) return {};

    glm::vec3 ray_cross_e2 = cross(ray.direction, edge2);
    float det = dot(edge1, ray_cross_e2);

    if (abs(det) < epsilon) return {};

    float inv_det = 1.0 / det;
    glm::vec3 s = ray.origin - triangle.v0;
    float u = inv_det * dot(s, ray_cross_e2);

    if (u < -epsilon || u - 1 > epsilon) return {};

    glm::vec3 s_cross_e1 = cross(s, edge1);
    float v = inv_det * dot(ray.direction, s_cross_e1);

    if (v < -epsilon || u + v - 1 > epsilon) return {};

    float t = inv_det * dot(edge2, s_cross_e1);

    if (t > epsilon)
        return TriangleHit{t, ray.origin + ray.direction * t, normal};
    else
        return {};
}
