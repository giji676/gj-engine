#include "scene.h"

#include <algorithm>

#include "engine/systems/behaviours_system.h"
#include "engine/systems/render_system.h"
#include "engine/systems/transform_system.h"

uint32_t TagRegistry::byName(const std::string& name) const {
    const auto it = byName_.find(name);
    assert(it != byName_.end());
    return it->second;
}

uint32_t TagRegistry::intern(const std::string& name) {
    const auto it = byName_.find(name);
    if (it != byName_.end())
        return it->second;

    const uint32_t id = static_cast<uint32_t>(byId.size());
    byName_.emplace(name, id);
    byId.push_back(name);
    entitiesByTag_.emplace_back();
    denseByTag_.emplace_back();
    return id;
}

const std::string& TagRegistry::name(uint32_t id) const {
    assert(id < byId.size());
    return byId[id];
}

bool TagRegistry::hasName(const std::string& name) const {
    return byName_.find(name) != byName_.end();
}

bool TagRegistry::isValidId(uint32_t id) const {
    return id < byId.size();
}

void TagRegistry::trackEntity(uint32_t tagId, uint32_t entityIdx) {
    assert(isValidId(tagId));
    auto& dense = denseByTag_[tagId];
    if (entityIdx >= dense.size())
        dense.resize(static_cast<size_t>(entityIdx) + 1, -1);
    if (dense[entityIdx] >= 0)
        return;

    dense[entityIdx] = static_cast<int32_t>(entitiesByTag_[tagId].size());
    entitiesByTag_[tagId].push_back(entityIdx);
}

void TagRegistry::untrackEntity(uint32_t tagId, uint32_t entityIdx) {
    assert(isValidId(tagId));
    auto& dense = denseByTag_[tagId];
    if (entityIdx >= dense.size() || dense[entityIdx] < 0)
        return;

    auto& list = entitiesByTag_[tagId];
    const int32_t pos = dense[entityIdx];
    const uint32_t last = list.back();
    list[static_cast<size_t>(pos)] = last;
    dense[last] = pos;
    list.pop_back();
    dense[entityIdx] = -1;
}

const std::vector<uint32_t>& TagRegistry::entities(uint32_t tagId) const {
    assert(isValidId(tagId));
    return entitiesByTag_[tagId];
}

bool hasTag(const Tag& t, uint32_t id) {
    return std::find(t.ids.begin(), t.ids.end(), id) != t.ids.end();
}

void addTag(Tag& t, uint32_t id) {
    if (!hasTag(t, id))
        t.ids.push_back(id);
}

void removeTag(Tag& t, uint32_t id) {
    auto it = std::find(t.ids.begin(), t.ids.end(), id);
    if (it != t.ids.end())
        t.ids.erase(it);
}

void Scene::addTag(const Entity& e, uint32_t tagId) {
    assert(isValid(e));
    assert(tagRegistry.isValidId(tagId));

    Tag& tags = has<Tag>(e) ? get<Tag>(e) : add<Tag>(e);
    if (hasTag(tags, tagId))
        return;

    tags.ids.push_back(tagId);
    tagRegistry.trackEntity(tagId, e.idx);
}

void Scene::removeTag(const Entity& e, uint32_t tagId) {
    if (!isValid(e) || !has<Tag>(e))
        return;

    Tag& tags = get<Tag>(e);
    if (!hasTag(tags, tagId))
        return;

    ::removeTag(tags, tagId);
    tagRegistry.untrackEntity(tagId, e.idx);
}

void Scene::init() {
    root = create(Entity::invalid());
    Object& rootObj = add<Object>(root);
    rootObj.name = "Scene";
}

void Scene::registerSystem(std::unique_ptr<ISystem> system) {
    systems_.push_back(std::move(system));
}

void Scene::update(float dt, bool runBehaviours) {
    for (auto& system : systems_)
        system->update(*this, dt);

    BehavioursSystem behaviours;
    if (runBehaviours)
        behaviours.update(*this);

    TransformSystem ts;
    ts.update(*this);

    if (runBehaviours)
        behaviours.lateUpdate(*this);
}

void Scene::collectRenderCommands(
    const Frustum& frustum,
    std::vector<RenderCommand>& out)
{
    RenderSystem rs;
    rs.lastSize = lastRenderListSize;
    rs.update(*this, frustum);
    lastRenderListSize = rs.lastSize;
    out.insert(out.end(), rs.commands.begin(), rs.commands.end());
}

void Scene::ensureSlotCapacity(uint32_t idx) {
    const size_t need = static_cast<size_t>(idx) + 1;
    if (slots.size() < need)
        slots.resize(need);

    transforms_.ensure(idx);
}

Entity Scene::create() {
    return create(Entity::invalid());
}

Entity Scene::create(Entity parent) {
    uint32_t idx;

    if (freeIndices.empty())
        idx = nextIndex++;
    else {
        idx = freeIndices.back();
        freeIndices.pop_back();
    }

    ensureSlotCapacity(idx);

    transforms_.reset(idx);

    Entity e = {
        .idx = idx,
        .gen = slots[idx].gen,
    };

    slots[idx].alive = true;
    slots[idx].comp_mask = 0;
    add<Transform>(e);

    Hierarchy& h = add<Hierarchy>(e);
    h.children.clear();

    // Creating the scene root: no parent, do not touch any parent list.
    if (!isValid(parent) && !isValid(root)) {
        h.parent = Entity::invalid();
        return e;
    }

    // Default parent is the scene root.
    if (!isValid(parent))
        parent = root;

    h.parent = parent;

    Hierarchy& parentHierarchy = add<Hierarchy>(parent);
    parentHierarchy.children.push_back(e);

    return e;
}

void Scene::clearEntityComponents(uint32_t idx) {
    if ((slots[idx].comp_mask & ComponentTraits<Tag>::mask) != 0) {
        for (uint32_t tagId : tags_.at(idx).ids)
            tagRegistry.untrackEntity(tagId, idx);
    }

    transforms_.untrack(idx);
    objects_.untrack(idx);
    hierarchies_.untrack(idx);
    tags_.untrack(idx);
    behaviours_.untrack(idx);

    transforms_.reset(idx);
    objects_.reset(idx);
    hierarchies_.reset(idx);
    tags_.reset(idx);
    behaviours_.reset(idx);
    dynamicComponents_.clearEntity(idx);
}

void Scene::destroy(Entity& e) {
    if (!isValid(e))
        return;

    if (isValid(root) && e.idx == root.idx)
        return;

    const uint32_t idx = e.idx;

    if (has<Hierarchy>(e)) {
        Hierarchy& h = get<Hierarchy>(e);
        if (isValid(h.parent) && has<Hierarchy>(h.parent)) {
            Hierarchy& parentH = get<Hierarchy>(h.parent);
            auto it = std::find_if(parentH.children.begin(), parentH.children.end(),
                [&](const Entity& child) { return child.idx == idx; });
            if (it != parentH.children.end())
                parentH.children.erase(it);
        }

        // Copy first: recursive destroy unlinks children from this parent.
        std::vector<Entity> children = h.children;
        h.children.clear();
        for (Entity child : children)
            destroy(child);
    }

    clearEntityComponents(idx);

    slots[idx].alive = false;
    slots[idx].comp_mask = 0;
    slots[idx].gen++;
    freeIndices.push_back(idx);

    e = Entity::invalid();
}

bool Scene::isValid(const Entity& e) const {
    return e.idx != UINT32_MAX &&
        e.idx < slots.size() &&
        e.gen == slots[e.idx].gen &&
        slots[e.idx].alive;
}

bool Scene::isDescendant(Entity ancestor, Entity node) const {
    if (!isValid(ancestor) || !isValid(node))
        return false;

    Entity cur = node;
    const uint32_t hopLimit = static_cast<uint32_t>(slots.size());
    for (uint32_t hops = 0; hops < hopLimit; ++hops) {
        if (!has<Hierarchy>(cur))
            return false;
        const Entity parent = get<Hierarchy>(cur).parent;
        if (!isValid(parent))
            return false;
        if (parent == ancestor)
            return true;
        cur = parent;
    }
    return false;
}

void Scene::reparent(Entity child, Entity newParent, int index) {
    if (!isValid(child) || child == root)
        return;
    if (!isValid(newParent))
        newParent = root;
    if (child == newParent || isDescendant(child, newParent))
        return;

    Hierarchy& childH = has<Hierarchy>(child) ? get<Hierarchy>(child) : add<Hierarchy>(child);
    Hierarchy& newParentH =
        has<Hierarchy>(newParent) ? get<Hierarchy>(newParent) : add<Hierarchy>(newParent);

    Entity oldParent = childH.parent;
    int oldIndex = -1;
    if (isValid(oldParent) && has<Hierarchy>(oldParent)) {
        Hierarchy& oldParentH = get<Hierarchy>(oldParent);
        auto it = std::find_if(
            oldParentH.children.begin(),
            oldParentH.children.end(),
            [&](const Entity& c) { return c.idx == child.idx; });
        if (it != oldParentH.children.end()) {
            oldIndex = static_cast<int>(it - oldParentH.children.begin());
            oldParentH.children.erase(it);
        }
    }

    if (oldParent == newParent && oldIndex >= 0 && oldIndex < index)
        --index;

    childH.parent = newParent;

    if (index < 0 || index > static_cast<int>(newParentH.children.size()))
        index = static_cast<int>(newParentH.children.size());

    newParentH.children.insert(
        newParentH.children.begin() + index,
        child);
}
