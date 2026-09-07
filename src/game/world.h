#pragma once

#include <vector>
#include <glm/glm.hpp>

struct Terrain {
    std::vector<float> vertices;   // x y z nx ny nz
    std::vector<unsigned int> indices;
};

struct World_ {
    Terrain terrain;
    int width = 0;
    int height = 0;
    float scale = 1.f;
};
