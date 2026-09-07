#pragma once

#include <glm/glm.hpp>

struct Gravity {
    glm::vec3 acceleration{0.f, -9.81f, 0.f};
    glm::vec3 velocity{0.f};
};
