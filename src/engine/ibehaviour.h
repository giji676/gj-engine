#pragma once

#include "engine/entity.h"
#include "inspectable.h"

class IBehaviour : public Inspectable {
public:
    Entity entity = Entity::invalid();
    bool enabled = true;

    IBehaviour() {
        INSPECT(enabled);
    }

    virtual ~IBehaviour() = default;

    virtual const char* typeName() const { return "Behaviour"; }

    virtual void init() {}
    virtual void update() {}
    virtual void lateUpdate() {}
};
