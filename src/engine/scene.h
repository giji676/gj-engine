#pragma once

#include "engine/asset_manager/model.h"
#include "engine/component_pool.h"
#include "engine/component_registry.h"
#include "engine/entity.h"
#include "engine/frustrum.h"
#include "engine/icomponent.h"
#include "engine/iscript.h"
#include "engine/systems/isystem.h"
#include "engine/renderer/renderer.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

struct EntitySlot {
    bool alive = false;
    uint32_t gen = 0;
    uint64_t comp_mask = 0;
};

struct Transform {
    glm::vec3 position{0.f};
    glm::vec3 rotation{0.f};
    glm::vec3 scale{1.f};

    glm::mat4 worldMatrix{1.f};
    glm::mat4 worldInvMatrix{1.f};
};

struct Object {
    std::string name;
    Model* model = nullptr;
    bool debug = false;
};

struct Hierarchy {
    Entity parent = Entity::invalid();
    std::vector<Entity> children;
};

struct TagRegistry {
    std::unordered_map<std::string, uint32_t> byName_;
    std::vector<std::string> byId;
    std::vector<std::vector<uint32_t>> entitiesByTag_;
    std::vector<std::vector<int32_t>> denseByTag_;

    uint32_t intern(const std::string& name);
    uint32_t byName(const std::string& name) const;
    const std::string& name(uint32_t id) const;
    bool hasName(const std::string& name) const;
    bool isValidId(uint32_t id) const;

    void trackEntity(uint32_t tagId, uint32_t entityIdx);
    void untrackEntity(uint32_t tagId, uint32_t entityIdx);
    const std::vector<uint32_t>& entities(uint32_t tagId) const;
};

struct Tag {
    std::vector<uint32_t> ids;
};

struct Behaviours {
    Entity entity = Entity::invalid();
    std::vector<std::unique_ptr<IScript>> scripts;
    std::vector<std::unique_ptr<IComponent>> components;

    template <typename T, typename... Args>
    T& addScript(Args&&... args) {
        static_assert(std::is_base_of<IScript, T>::value,
                      "T must inherit IScript");
        auto script = std::make_unique<T>(std::forward<Args>(args)...);
        script->entity = entity;
        T& ref = *script;
        scripts.push_back(std::move(script));
        ref.init();
        return ref;
    }

    template <typename T, typename... Args>
    T& addComponent(Args&&... args) {
        static_assert(std::is_base_of<IComponent, T>::value,
                      "T must inherit IComponent");
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        component->entity = entity;
        T& ref = *component;
        components.push_back(std::move(component));
        ref.init();
        return ref;
    }

    template <typename T>
    T* getComponent() {
        static_assert(std::is_base_of<IComponent, T>::value,
                      "T must inherit IComponent");
        for (auto& component : components) {
            if (T* typed = dynamic_cast<T*>(component.get()))
                return typed;
        }
        return nullptr;
    }

    template <typename T>
    const T* getComponent() const {
        static_assert(std::is_base_of<IComponent, T>::value,
                      "T must inherit IComponent");
        for (const auto& component : components) {
            if (const T* typed = dynamic_cast<const T*>(component.get()))
                return typed;
        }
        return nullptr;
    }

    template <typename T>
    T* getScript() {
        static_assert(std::is_base_of<IScript, T>::value,
                      "T must inherit IScript");
        for (auto& script : scripts) {
            if (T* typed = dynamic_cast<T*>(script.get()))
                return typed;
        }
        return nullptr;
    }

    template <typename T>
    const T* getScript() const {
        static_assert(std::is_base_of<IScript, T>::value,
                      "T must inherit IScript");
        for (const auto& script : scripts) {
            if (const T* typed = dynamic_cast<const T*>(script.get()))
                return typed;
        }
        return nullptr;
    }
};

class Scene {
public:
    std::vector<EntitySlot> slots;
    std::vector<uint32_t> freeIndices;
    uint32_t nextIndex = 0;

    bool isValid(const Entity& e) const;
    Entity create();
    Entity create(Entity parent);
    void destroy(Entity& e);

    void reparent(Entity child, Entity newParent, int index = -1);
    bool isDescendant(Entity ancestor, Entity node) const;

    template <typename T>
    T& get(const Entity& e);

    template <typename T>
    const T& get(const Entity& e) const;

    template <typename T>
    bool has(const Entity& e) const;

    template <typename T>
    T& add(const Entity& e);

    template <typename T>
    void registerComponent();

    template <typename T>
    const std::vector<uint32_t>& entitiesWith() const;

    void addTag(const Entity& e, uint32_t tagId);
    void removeTag(const Entity& e, uint32_t tagId);

    void update(float dt, bool runBehaviours = true);
    void init();
    void collectRenderCommands(const Frustum& frustum, std::vector<RenderCommand>& out);

    void registerSystem(std::unique_ptr<ISystem> system);

    size_t lastRenderListSize = 0;

    Entity root = Entity::invalid();
    TagRegistry tagRegistry;

private:
    template <typename T>
    friend struct ComponentTraits;

    ComponentPool<Transform> transforms_;
    ComponentPool<Object> objects_;
    ComponentPool<Hierarchy> hierarchies_;
    ComponentPool<Tag> tags_;
    ComponentPool<Behaviours> behaviours_;

    ComponentRegistry dynamicComponents_;
    std::vector<std::unique_ptr<ISystem>> systems_;

    void ensureSlotCapacity(uint32_t idx);
    void clearEntityComponents(uint32_t idx);

    template <typename T>
    uint64_t maskFor() const;

    template <typename T>
    ComponentPool<T>& poolFor();

    template <typename T>
    const ComponentPool<T>& poolFor() const;
};

template <typename T>
struct ComponentTraits {
    static constexpr uint64_t mask = 0;
    static ComponentPool<T>& pool(Scene& w);
};

#define SCENE_REGISTER_COMPONENT(Type, Member, Mask)                    \
    template <>                                                         \
    struct ComponentTraits<Type> {                                      \
        static constexpr uint64_t mask = Mask;                          \
        static ComponentPool<Type>& pool(Scene& w) { return w.Member; } \
    };

SCENE_REGISTER_COMPONENT(Transform, transforms_, (1ull << 0))
SCENE_REGISTER_COMPONENT(Object, objects_, (1ull << 1))
SCENE_REGISTER_COMPONENT(Hierarchy, hierarchies_, (1ull << 2))
SCENE_REGISTER_COMPONENT(Tag, tags_, (1ull << 3))
SCENE_REGISTER_COMPONENT(Behaviours, behaviours_, (1ull << 4))

template <typename T>
uint64_t Scene::maskFor() const {
    if constexpr (ComponentTraits<T>::mask != 0)
        return ComponentTraits<T>::mask;
    if (!dynamicComponents_.isRegistered<T>())
        return 0;
    return dynamicComponents_.mask<T>();
}

template <typename T>
ComponentPool<T>& Scene::poolFor() {
    if constexpr (ComponentTraits<T>::mask != 0)
        return ComponentTraits<T>::pool(*this);
    else
        return dynamicComponents_.pool<T>();
}

template <typename T>
const ComponentPool<T>& Scene::poolFor() const {
    if constexpr (ComponentTraits<T>::mask != 0)
        return ComponentTraits<T>::pool(const_cast<Scene&>(*this));
    else
        return dynamicComponents_.pool<T>();
}

template <typename T>
void Scene::registerComponent() {
    static_assert(
        ComponentTraits<T>::mask == 0,
        "Type is already a static engine component");
    dynamicComponents_.registerType<T>();
}

template <typename T>
const std::vector<uint32_t>& Scene::entitiesWith() const {
    return poolFor<T>().entities;
}

template <typename T>
T& Scene::get(const Entity& e) {
    assert(isValid(e));
    return poolFor<T>().at(e.idx);
}

template <typename T>
const T& Scene::get(const Entity& e) const {
    assert(isValid(e));
    return poolFor<T>().at(e.idx);
}

template <typename T>
bool Scene::has(const Entity& e) const {
    if (!isValid(e))
        return false;
    const uint64_t mask = maskFor<T>();
    if (mask == 0)
        return false;
    return (slots[e.idx].comp_mask & mask) != 0;
}

template <typename T>
T& Scene::add(const Entity& e) {
    assert(isValid(e));
    if constexpr (ComponentTraits<T>::mask == 0)
        assert(dynamicComponents_.isRegistered<T>() && "Call registerComponent<T>() first");

    const uint64_t mask = maskFor<T>();
    assert(mask != 0);

    const bool already = (slots[e.idx].comp_mask & mask) != 0;
    slots[e.idx].comp_mask |= mask;

    ComponentPool<T>& pool = poolFor<T>();
    pool.ensure(e.idx);
    if (!already)
        pool.track(e.idx);

    if constexpr (std::is_same_v<T, Behaviours>)
        pool.at(e.idx).entity = e;

    return pool.at(e.idx);
}

bool hasTag(const Tag& t, uint32_t id);
void addTag(Tag& t, uint32_t id);
void removeTag(Tag& t, uint32_t id);
