#include "editor/hierarchy_panel.h"

#include "editor/panel.h"
#include "engine/asset_manager/widgets.h"
#include "engine/engine.h"
#include "engine/input.h"
#include "engine/ui.h"
#include "engine/ui/style.h"
#include "engine/utils/geometry.h"
#include "engine/scene.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace {

constexpr float kRowFontSize = 28.f;
constexpr float kLabelPadX = 4.f;
constexpr float kLabelPadY = 4.f;
constexpr float kDragThresholdPx = 4.f;
constexpr float kEdgeBandRatio = 0.25f;
constexpr float kDropLineHeight = 2.f;

std::string entityLabel(const Scene& scene, Entity e) {
    if (e == scene.root)
        return "Scene";
    if (scene.has<Object>(e)) {
        const Object& obj = scene.get<Object>(e);
        if (!obj.name.empty())
            return obj.name;
    }
    return "Entity " + std::to_string(e.idx);
}

} // namespace

void HierarchyPanel::bind(UI& ui, EditorPanel& panel) {
    ui_ = &ui;
    contentId_ = panel.contentId();
    built_ = true;
    lastSignature_ = 0;

    UIElement& content = ui_->get(contentId_);
    content.style.display = Display::Block;
    content.style.gap = Length::px(0.f);

    dropLineId_ = ui_->rect(
        {0.f, 0.f},
        {0.f, kDropLineHeight},
        {0.35f, 0.65f, 1.f, 0.95f});
    ui_->reparent(dropLineId_, contentId_);
    UIElement& line = ui_->get(dropLineId_);
    line.visible = false;
    line.style.position = PositionMode::Absolute;
    line.style.display = Display::Block;
    line.style.width = Length::percent(100.f);
    line.style.height = Length::px(kDropLineHeight);
}

void HierarchyPanel::collectEntries(
        Scene& scene,
        Entity id,
        int depth,
        bool isLastSibling,
        const std::vector<bool>& ancestorOpen,
        std::vector<Entry>& out) const
{
    static const std::vector<Entity> kEmptyChildren;
    const std::vector<Entity>& children = scene.has<Hierarchy>(id)
        ? scene.get<Hierarchy>(id).children
        : kEmptyChildren;
    const bool hasChildren = !children.empty();
    const bool isExpanded = hasChildren && collapsedIds_.find(id) == collapsedIds_.end();

    Entry entry;
    entry.id = id;
    entry.depth = depth;
    entry.hasChildren = hasChildren;
    entry.isExpanded = isExpanded;
    entry.isLastSibling = isLastSibling;
    entry.ancestorOpen = ancestorOpen;
    out.push_back(entry);

    if (!isExpanded)
        return;

    std::vector<bool> childAncestors = ancestorOpen;
    if (depth > 0)
        childAncestors.push_back(!isLastSibling);

    for (size_t i = 0; i < children.size(); ++i) {
        const bool childIsLast = (i + 1 == children.size());
        collectEntries(
            scene,
            children[i],
            depth + 1,
            childIsLast,
            childAncestors,
            out);
    }
}

uint64_t HierarchyPanel::sceneSignature(Scene& scene) const {
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](uint64_t value) {
        hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    };
    auto mixEntity = [&](Entity e) {
        mix(static_cast<uint64_t>(e.idx));
        mix(static_cast<uint64_t>(e.gen));
    };

    std::function<void(Entity)> walk = [&](Entity id) {
        mixEntity(id);
        Entity parent = Entity::invalid();
        size_t childCount = 0;
        if (scene.has<Hierarchy>(id)) {
            const Hierarchy& h = scene.get<Hierarchy>(id);
            parent = h.parent;
            childCount = h.children.size();
        }
        mixEntity(parent);
        mix(static_cast<uint64_t>(childCount));
        std::string name;
        if (scene.has<Object>(id))
            name = scene.get<Object>(id).name;
        mix(static_cast<uint64_t>(name.size()));
        for (unsigned char c : name)
            mix(static_cast<uint64_t>(c));
        mix(collapsedIds_.count(id) ? 1ull : 0ull);
        if (scene.has<Hierarchy>(id)) {
            for (Entity child : scene.get<Hierarchy>(id).children)
                walk(child);
        }
    };
    walk(scene.root);
    mixEntity(selectedId_);
    mixEntity(draggingId_);
    mixEntity(activeDrop_.targetId);
    mix(static_cast<uint64_t>(static_cast<int>(activeDrop_.kind)));
    return hash;
}

std::string HierarchyPanel::treePrefix(const Entry& entry) const {
    if (entry.depth <= 0)
        return {};

    std::string prefix;
    for (size_t i = 0; i < entry.ancestorOpen.size(); ++i)
        prefix += entry.ancestorOpen[i] ? "|  " : "   ";
    prefix += entry.isLastSibling ? "`- " : "|- ";
    return prefix;
}

void HierarchyPanel::applyRowMetrics(Row& row) const {
    UIElement& labelEl = ui_->get(row.labelId);
    labelEl.transform.fontSize = kRowFontSize;

    auto* labelBtn = dynamic_cast<Button*>(labelEl.widget.get());
    if (!labelBtn)
        return;

    labelBtn->padding = {kLabelPadX, kLabelPadY};

    const glm::vec2 measured = labelBtn->measureContent(labelEl, {0.f, 0.f});
    const float rowH = measured.y;

    UIElement& rootEl = ui_->get(row.rootId);
    rootEl.style.height = Length::px(rowH);
    rootEl.transform.fontSize = kRowFontSize;

    UIElement& toggleEl = ui_->get(row.toggleId);
    toggleEl.transform.fontSize = kRowFontSize;
    toggleEl.style.height = Length::px(rowH);
    toggleEl.style.width = Length::px(rowH);
    toggleEl.transform.size = {rowH, rowH};

    labelEl.style.height = Length::automatic();
}

void HierarchyPanel::ensureRowCount(size_t count) {
    while (rows_.size() < count) {
        UIElementID root = ui_->createElement();
        ui_->reparent(root, contentId_);
        UIElement& rootEl = ui_->get(root);
        rootEl.style.position = PositionMode::Relative;
        rootEl.style.display = Display::Flex;
        rootEl.style.flexDirection = FlexDirection::Row;
        rootEl.style.alignItems = AlignItems::Center;
        rootEl.style.width = Length::percent(100.f);
        rootEl.style.height = Length::automatic();

        UIElementID toggle = ui_->button({0.f, 0.f}, kRowFontSize, " ", nullptr);
        ui_->reparent(toggle, root);
        UIElement& toggleEl = ui_->get(toggle);
        toggleEl.style.position = PositionMode::Relative;
        toggleEl.style.width = Length::automatic();
        toggleEl.style.height = Length::automatic();
        toggleEl.style.flexGrow = 0.f;
        toggleEl.transform.fontSize = kRowFontSize;
        if (auto* btn = dynamic_cast<Button*>(toggleEl.widget.get())) {
            btn->padding = {0.f, 0.f};
            btn->cornerRadii = {0.f, 0.f, 0.f, 0.f};
            btn->centerText = true;
        }

        UIElementID label = ui_->button({0.f, 0.f}, kRowFontSize, "", nullptr);
        ui_->reparent(label, root);
        UIElement& labelEl = ui_->get(label);
        labelEl.style.position = PositionMode::Relative;
        labelEl.style.height = Length::automatic();
        labelEl.style.flexGrow = 1.f;
        labelEl.style.flexBasis = Length::px(0.f);
        labelEl.transform.fontSize = kRowFontSize;
        if (auto* btn = dynamic_cast<Button*>(labelEl.widget.get())) {
            btn->padding = {kLabelPadX, kLabelPadY};
            btn->cornerRadii = {0.f, 0.f, 0.f, 0.f};
            btn->centerText = false;
        }

        rows_.push_back({root, toggle, label, Entity::invalid()});
        applyRowMetrics(rows_.back());
    }

    // Keep the drop line above row widgets for visibility.
    if (dropLineId_ != INVALID_UI_ELEMENT)
        ui_->reparent(dropLineId_, contentId_);
}

void HierarchyPanel::styleToggle(UIElementID id, bool hasChildren, bool expanded) const {
    auto* btn = dynamic_cast<Button*>(ui_->get(id).widget.get());
    if (!btn)
        return;

    btn->normal.bgColor = {0.f, 0.f, 0.f, 0.f};
    btn->hoveredStyle.bgColor = hasChildren
        ? glm::vec4{0.28f, 0.28f, 0.32f, 1.f}
        : glm::vec4{0.f, 0.f, 0.f, 0.f};
    btn->pressedStyle.bgColor = {0.22f, 0.22f, 0.26f, 1.f};
    btn->normal.textColor = {0.75f, 0.75f, 0.8f, 1.f};
    btn->hoveredStyle.textColor = {0.95f, 0.95f, 0.97f, 1.f};
    btn->text = hasChildren ? (expanded ? "v" : ">") : " ";
    btn->disabled = !hasChildren;
}

void HierarchyPanel::styleLabel(UIElementID id, bool selected, bool dropHover) const {
    auto* btn = dynamic_cast<Button*>(ui_->get(id).widget.get());
    if (!btn)
        return;

    btn->centerText = false;
    if (dropHover) {
        btn->normal.bgColor = {0.18f, 0.42f, 0.28f, 1.f};
        btn->hoveredStyle.bgColor = {0.22f, 0.5f, 0.34f, 1.f};
        btn->pressedStyle.bgColor = {0.16f, 0.38f, 0.25f, 1.f};
        btn->normal.borderColor = {0.45f, 0.9f, 0.6f, 1.f};
        btn->normal.borderWidth = 2.f;
    } else if (selected) {
        btn->normal.bgColor = {0.22f, 0.38f, 0.62f, 1.f};
        btn->hoveredStyle.bgColor = {0.28f, 0.46f, 0.72f, 1.f};
        btn->pressedStyle.bgColor = {0.18f, 0.32f, 0.55f, 1.f};
        btn->normal.borderWidth = 0.f;
    } else {
        btn->normal.bgColor = {0.f, 0.f, 0.f, 0.f};
        btn->hoveredStyle.bgColor = {0.22f, 0.22f, 0.26f, 1.f};
        btn->pressedStyle.bgColor = {0.18f, 0.18f, 0.22f, 1.f};
        btn->normal.borderWidth = 0.f;
    }
    btn->normal.textColor = {0.88f, 0.88f, 0.9f, 1.f};
    btn->hoveredStyle.textColor = {0.95f, 0.95f, 0.97f, 1.f};
    btn->pressedStyle.textColor = {0.95f, 0.95f, 0.97f, 1.f};
}

void HierarchyPanel::syncObjectDebug(Scene& scene) const {
    std::function<void(Entity)> walk = [&](Entity id) {
        if (scene.has<Object>(id))
            scene.get<Object>(id).debug = (id == selectedId_);
        if (scene.has<Hierarchy>(id)) {
            for (Entity child : scene.get<Hierarchy>(id).children)
                walk(child);
        }
    };
    walk(scene.root);
}

int HierarchyPanel::siblingIndex(Scene& scene, Entity id) const {
    if (!scene.has<Hierarchy>(id))
        return -1;
    const Entity parent = scene.get<Hierarchy>(id).parent;
    if (!scene.isValid(parent) || !scene.has<Hierarchy>(parent))
        return -1;
    const auto& siblings = scene.get<Hierarchy>(parent).children;
    for (size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i] == id)
            return static_cast<int>(i);
    }
    return -1;
}

bool HierarchyPanel::canDropOn(
        Scene& scene,
        Entity draggedId,
        const DropTarget& drop) const
{
    if (!drop.valid || drop.kind == DropKind::None || drop.targetId == Entity::invalid())
        return false;
    if (draggedId == Entity::invalid() || draggedId == scene.root)
        return false;
    if (drop.targetId == draggedId)
        return false;

    if (drop.kind == DropKind::Reparent) {
        if (scene.isDescendant(draggedId, drop.targetId))
            return false;
        return true;
    }

    // Insert before/after is sibling placement under target's parent.
    if (drop.targetId == scene.root)
        return false;

    if (!scene.has<Hierarchy>(drop.targetId))
        return false;
    const Entity newParent = scene.get<Hierarchy>(drop.targetId).parent;
    if (newParent == draggedId || scene.isDescendant(draggedId, newParent))
        return false;
    return true;
}

HierarchyPanel::DropTarget HierarchyPanel::hitTestDrop(
        Scene& scene,
        glm::vec2 mouse) const
{
    DropTarget result;
    const Entity rootId = scene.root;

    for (const Row& row : rows_) {
        if (row.objectId == Entity::invalid())
            continue;
        if (!ui_->get(row.rootId).visible)
            continue;

        const UIElement& rowEl = ui_->get(row.rootId);
        const glm::vec2 pos = rowEl.transform.position;
        const glm::vec2 size = rowEl.transform.size;
        if (size.y <= 0.f)
            continue;
        if (!pointInRect(mouse, pos, size))
            continue;

        const float localY = mouse.y - pos.y;
        const float edge = size.y * kEdgeBandRatio;
        DropKind kind = DropKind::Reparent;
        if (row.objectId == rootId) {
            // Root only accepts reparent-into (no sibling insert around scene).
            kind = DropKind::Reparent;
        } else if (localY >= size.y - edge) {
            kind = DropKind::InsertBefore;
        } else if (localY <= edge) {
            kind = DropKind::InsertAfter;
        } else {
            kind = DropKind::Reparent;
        }

        result.kind = kind;
        result.targetId = row.objectId;
        result.valid = true;
        return result;
    }
    return result;
}

void HierarchyPanel::applyDrop(Scene& scene, Entity draggedId, const DropTarget& drop) {
    if (!canDropOn(scene, draggedId, drop))
        return;

    if (drop.kind == DropKind::Reparent) {
        scene.reparent(draggedId, drop.targetId);
        collapsedIds_.erase(drop.targetId);
        return;
    }

    if (!scene.has<Hierarchy>(drop.targetId))
        return;
    const Entity newParent = scene.get<Hierarchy>(drop.targetId).parent;
    int index = siblingIndex(scene, drop.targetId);
    if (index < 0)
        return;
    if (drop.kind == DropKind::InsertAfter)
        ++index;

    scene.reparent(draggedId, newParent, index);
}

void HierarchyPanel::syncDropPreview(Scene& scene) {
    if (dropLineId_ == INVALID_UI_ELEMENT)
        return;

    UIElement& line = ui_->get(dropLineId_);
    line.visible = false;

    if (draggingId_ == Entity::invalid() || !activeDrop_.valid)
        return;

    if (activeDrop_.kind == DropKind::Reparent)
        return;

    for (const Row& row : rows_) {
        if (row.objectId != activeDrop_.targetId)
            continue;

        const UIElement& rowEl = ui_->get(row.rootId);
        const UIElement& content = ui_->get(contentId_);
        float lineY = rowEl.transform.position.y;
        if (activeDrop_.kind == DropKind::InsertBefore)
            lineY = rowEl.transform.position.y + rowEl.transform.size.y - kDropLineHeight;

        line.visible = true;
        line.style.position = PositionMode::Absolute;
        line.style.inset.left = Length::px(0.f);
        line.style.inset.right = Length::px(0.f);
        line.style.inset.bottom = Length::px(
            std::max(0.f, lineY - content.transform.position.y));
        line.style.height = Length::px(kDropLineHeight);
        line.style.width = Length::percent(100.f);
        return;
    }
}

void HierarchyPanel::updateDrag(Scene& scene) {
    Input& input = ENGINE().input;
    const glm::vec2 mouse = input.mousePosition();
    const Entity rootId = scene.root;
    releasedDragId_ = Entity::invalid();

    if (input.pressed(MouseAction::Left)) {
        dragCandidateId_ = Entity::invalid();
        dragMoved_ = false;
        for (const Row& row : rows_) {
            if (row.objectId == Entity::invalid() || row.objectId == rootId)
                continue;
            if (!ui_->get(row.rootId).visible)
                continue;
            const UIElement& labelEl = ui_->get(row.labelId);
            if (pointInRect(mouse, labelEl.transform.position, labelEl.transform.size)) {
                dragCandidateId_ = row.objectId;
                dragStartMouse_ = mouse;
                break;
            }
        }
    }

    if (dragCandidateId_ != Entity::invalid() && input.down(MouseAction::Left)) {
        const glm::vec2 delta = mouse - dragStartMouse_;
        if (!dragMoved_ && glm::length(delta) >= kDragThresholdPx) {
            dragMoved_ = true;
            draggingId_ = dragCandidateId_;
        }
    }

    if (draggingId_ != Entity::invalid() && input.down(MouseAction::Left)) {
        DropTarget drop = hitTestDrop(scene, mouse);
        if (!canDropOn(scene, draggingId_, drop))
            drop = {};
        activeDrop_ = drop;
    }

    if (input.released(MouseAction::Left)) {
        if (draggingId_ != Entity::invalid()) {
            releasedDragId_ = draggingId_;
            const UIElement& content = ui_->get(contentId_);
            const bool overHierarchy = pointInRect(
                mouse, content.transform.position, content.transform.size);
            if (overHierarchy && activeDrop_.valid)
                applyDrop(scene, draggingId_, activeDrop_);
        } else if (dragCandidateId_ != Entity::invalid() && !dragMoved_) {
            selectedId_ = dragCandidateId_;
        }

        draggingId_ = Entity::invalid();
        dragCandidateId_ = Entity::invalid();
        dragMoved_ = false;
        activeDrop_ = {};
    }

    if (!input.down(MouseAction::Left) && draggingId_ != Entity::invalid()) {
        draggingId_ = Entity::invalid();
        dragCandidateId_ = Entity::invalid();
        dragMoved_ = false;
        activeDrop_ = {};
    }
}

void HierarchyPanel::rebuildRows(Scene& scene) {
    std::vector<Entry> entries;
    collectEntries(scene, scene.root, 0, true, {}, entries);
    ensureRowCount(entries.size());

    for (size_t i = 0; i < rows_.size(); ++i) {
        UIElement& rootEl = ui_->get(rows_[i].rootId);
        if (i >= entries.size()) {
            rootEl.visible = false;
            rows_[i].objectId = Entity::invalid();
            continue;
        }

        const Entry& entry = entries[i];
        rootEl.visible = true;
        rows_[i].objectId = entry.id;

        const Entity capturedId = entry.id;
        size_t childCount = 0;
        if (scene.has<Hierarchy>(entry.id))
            childCount = scene.get<Hierarchy>(entry.id).children.size();

        styleToggle(rows_[i].toggleId, entry.hasChildren, entry.isExpanded);
        auto* toggle = dynamic_cast<Button*>(ui_->get(rows_[i].toggleId).widget.get());
        if (toggle) {
            if (entry.hasChildren) {
                toggle->onClick = [this, capturedId]() {
                    if (collapsedIds_.count(capturedId))
                        collapsedIds_.erase(capturedId);
                    else
                        collapsedIds_.insert(capturedId);
                    lastSignature_ = 0;
                };
            } else {
                toggle->onClick = nullptr;
            }
        }

        auto* label = dynamic_cast<Button*>(ui_->get(rows_[i].labelId).widget.get());
        if (label) {
            label->text = treePrefix(entry) + entityLabel(scene, entry.id);
            if (entry.hasChildren)
                label->text += " (" + std::to_string(childCount) + ")";

            // Selection / drag are handled in updateDrag to avoid click conflicts.
            label->onClick = nullptr;
        }

        const bool dropHover =
            draggingId_ != Entity::invalid() &&
            activeDrop_.valid &&
            activeDrop_.kind == DropKind::Reparent &&
            activeDrop_.targetId == entry.id;
        styleLabel(rows_[i].labelId, entry.id == selectedId_, dropHover);
        applyRowMetrics(rows_[i]);
    }

    syncDropPreview(scene);
}

void HierarchyPanel::update(Scene& scene, bool interactive) {
    if (!built_ || !ui_ || contentId_ == INVALID_UI_ELEMENT)
        return;

    if (interactive) {
        updateDrag(scene);
    } else {
        releasedDragId_ = Entity::invalid();
        draggingId_ = Entity::invalid();
        dragCandidateId_ = Entity::invalid();
        dragMoved_ = false;
        activeDrop_ = {};
    }

    const uint64_t signature = sceneSignature(scene);
    if (signature == lastSignature_) {
        syncDropPreview(scene);
        return;
    }

    lastSignature_ = signature;
    rebuildRows(scene);
    syncObjectDebug(scene);
}
