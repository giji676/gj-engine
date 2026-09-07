#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <functional>

#include "engine/defines.h"
#include "engine/entity.h"
#include "engine/inspectable.h"

class EditorPanel;
class IBehaviour;
class UI;
class Scene;
struct Transform;

// Reusable inspector card list for IScript / IComponent (and future IBehaviour types).
// Each card shows the type name plus INSPECT() fields (bool checkbox, numbers as inputs).
class BehaviourListView {
public:
    void bind(UI& ui, UIElementID parentId, const char* title);
    void update(
        Scene& scene,
        const std::vector<IBehaviour*>& items,
        bool editable,
        Entity draggingId,
        Entity droppedId);
    void setVisible(bool visible);

private:
    struct FieldRow {
        UIElementID rootId = INVALID_UI_ELEMENT;
        UIElementID labelId = INVALID_UI_ELEMENT;
        UIElementID controlId = INVALID_UI_ELEMENT;
        UIElementID clearId = INVALID_UI_ELEMENT;
        InspectType type = InspectType::Bool;
        bool wasFocused = false;
    };

    struct Card {
        UIElementID rootId = INVALID_UI_ELEMENT;
        UIElementID nameId = INVALID_UI_ELEMENT;
        UIElementID fieldsId = INVALID_UI_ELEMENT;
        std::vector<FieldRow> fields;
    };

    void ensureCardCount(size_t count);
    void ensureFieldCount(Card& card, size_t count);
    void setElementInFlow(UIElementID id, bool inFlow, float heightPx) const;
    void syncCard(
        Card& card,
        IBehaviour& behaviour,
        Scene& scene,
        bool editable,
        Entity draggingId,
        Entity droppedId);
    void syncFieldRow(
        FieldRow& row,
        const InspectField& field,
        Scene& scene,
        bool editable,
        Entity draggingId,
        Entity droppedId);
    float cardHeight(size_t fieldCount) const;
    float fieldsHeight(size_t fieldCount) const;
    float listHeight(const std::vector<IBehaviour*>& items) const;

    UI* ui_ = nullptr;
    UIElementID rootId_ = INVALID_UI_ELEMENT;
    UIElementID headerId_ = INVALID_UI_ELEMENT;
    UIElementID emptyId_ = INVALID_UI_ELEMENT;
    std::vector<Card> cards_;
    float headerHeight_ = 0.f;
    float emptyHeight_ = 0.f;
};

// Reusable transform display block for inspector-like UIs.
class TransformView {
public:
    void bind(
        UI& ui,
        UIElementID parentId,
        std::function<void(GizmoSpace)> onSpaceChange);
    void update(
        Transform& transform,
        Entity entity,
        Scene& scene,
        bool editable,
        GizmoSpace space);
    void setEditable(bool editable);
    void setSpace(GizmoSpace space);

private:
    bool parseFloat(const std::string& text, float& out) const;
    void applyPendingEdits(
        Transform& transform,
        Entity entity,
        Scene& scene,
        GizmoSpace space);

    struct AxisFields {
        UIElementID xId = INVALID_UI_ELEMENT;
        UIElementID yId = INVALID_UI_ELEMENT;
        UIElementID zId = INVALID_UI_ELEMENT;
        std::string pendingX;
        std::string pendingY;
        std::string pendingZ;
        bool dirtyX = false;
        bool dirtyY = false;
        bool dirtyZ = false;
    };

    UI* ui_ = nullptr;
    UIElementID rootId_ = INVALID_UI_ELEMENT;
    UIElementID spaceLocalButtonId_ = INVALID_UI_ELEMENT;
    UIElementID spaceWorldButtonId_ = INVALID_UI_ELEMENT;
    AxisFields pos_;
    AxisFields rot_;
    AxisFields scale_;
    bool editable_ = true;
    GizmoSpace space_ = GizmoSpace::Local;
    std::function<void(GizmoSpace)> onSpaceChange_;
};

class InspectorPanel {
public:
    void bind(UI& ui, EditorPanel& panel);
    void setSelectedId(Entity id) { selectedId_ = id; }
    void setEditable(bool editable) { editable_ = editable; }
    void setGizmoSpace(GizmoSpace space) { gizmoSpace_ = space; }
    void setGizmoSpaceCallback(std::function<void(GizmoSpace)> callback) {
        gizmoSpaceCallback_ = std::move(callback);
    }
    void setObjectDrag(Entity draggingId, Entity droppedId) {
        draggingObjectId_ = draggingId;
        droppedObjectId_ = droppedId;
    }
    void update(Scene& scene);

private:
    void setLabelText(UIElementID id, const char* value) const;
    void setLabelText(UIElementID id, const std::string& value) const;
    UIElementID addInfoLabel(const char* text);

    UI* ui_ = nullptr;
    UIElementID contentId_ = INVALID_UI_ELEMENT;
    Entity selectedId_ = Entity::invalid();
    Entity draggingObjectId_ = Entity::invalid();
    Entity droppedObjectId_ = Entity::invalid();
    bool editable_ = true;
    GizmoSpace gizmoSpace_ = GizmoSpace::Local;
    std::function<void(GizmoSpace)> gizmoSpaceCallback_;
    bool built_ = false;

    UIElementID headerId_ = INVALID_UI_ELEMENT;
    UIElementID nameRowId_ = INVALID_UI_ELEMENT;
    UIElementID nameFieldId_ = INVALID_UI_ELEMENT;
    bool nameFieldWasFocused_ = false;
    bool namePendingApply_ = false;
    UIElementID idLabelId_ = INVALID_UI_ELEMENT;
    UIElementID parentId_ = INVALID_UI_ELEMENT;
    UIElementID childrenId_ = INVALID_UI_ELEMENT;
    UIElementID modelId_ = INVALID_UI_ELEMENT;
    UIElementID debugId_ = INVALID_UI_ELEMENT;

    TransformView transformView_;
    BehaviourListView componentsView_;
    BehaviourListView scriptsView_;
};
