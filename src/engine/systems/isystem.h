#pragma once

class Scene;

struct ISystem {
    virtual ~ISystem() = default;
    virtual void update(Scene& w, float dt) = 0;
};
