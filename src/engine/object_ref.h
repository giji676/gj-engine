#pragma once

#include "engine/entity.h"

class Scene;

// Unity-style serialized references. Resolve through Scene when needed.
struct EntityRef {
    Entity id = Entity::invalid();
};

template <typename T>
struct ComponentRef {
    Entity id = Entity::invalid();
};

template <typename T>
struct ScriptRef {
    Entity id = Entity::invalid();
};

using ObjectRef = EntityRef;
