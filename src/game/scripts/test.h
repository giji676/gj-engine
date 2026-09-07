#pragma once

#include "engine/iscript.h"
#include "engine/engine.h"
#include "engine/object_ref.h"
#include "engine/scene.h"

class Test : public IScript {
public:
    EntityRef target;

    Test() {
        INSPECT(target);
    }

    void init() override {}
    const char* typeName() const override { return "Test"; }

    void update() override {
        Scene& scene = ENGINE().scene;
        Entity e = target.id;
        if (!scene.isValid(e))
            e = entity;
        if (!scene.isValid(e) || !scene.has<Transform>(e))
            return;

        Transform& t = scene.get<Transform>(e);
        t.rotation.y += 45.f * DT();
    }
};
