#include "editor/inspector_panel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "editor/panel.h"
#include "engine/asset_manager/widgets.h"
#include "engine/engine.h"
#include "engine/ibehaviour.h"
#include "engine/inspectable.h"
#include "engine/scene.h"
#include "engine/ui.h"
#include "engine/ui/style.h"
#include "engine/utils/geometry.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace {

constexpr float kHeaderFontSize = 24.f;
constexpr float kRowFontSize = 20.f;
constexpr float kAxisFontSize = 18.f;
constexpr float kInfoLabelH = kRowFontSize + 10.f;
constexpr float kAxisRowH = kAxisFontSize + 12.f;
constexpr float kTransformGap = 4.f;
constexpr float kSpaceRowH = 28.f;
constexpr float kTransformViewH =
    kSpaceRowH + kTransformGap +
    3.f * (kInfoLabelH + kAxisRowH) + 5.f * kTransformGap;
constexpr float kListGap = 6.f;
constexpr float kCardPad = 8.f;
constexpr float kCardHeaderH = kRowFontSize + 12.f;
constexpr float kFieldRowH = kAxisRowH;
constexpr float kFieldGap = 4.f;
constexpr float kCheckboxSize = 20.f;
constexpr float kClearBtnW = 22.f;

std::string formatFloat(float v) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%.3f", v);
    return std::string(buffer);
}

void styleInfoLabel(UIElement& element, float fontSize) {
    element.style.position = PositionMode::Relative;
    element.style.height = Length::px(fontSize + 10.f);
    element.style.width = Length::percent(100.f);
    element.transform.fontSize = fontSize;
}

std::string nicifyName(const char* name) {
    std::string out;
    if (!name || !*name)
        return out;

    bool capNext = true;
    unsigned char prev = 0;
    for (const char* p = name; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '_') {
            if (!out.empty() && out.back() != ' ')
                out.push_back(' ');
            capNext = true;
            prev = c;
            continue;
        }
        if (!out.empty() && std::islower(prev) && std::isupper(c))
            out.push_back(' ');
        if (capNext) {
            out.push_back(static_cast<char>(std::toupper(c)));
            capNext = false;
        } else {
            out.push_back(static_cast<char>(c));
        }
        prev = c;
    }
    return out;
}

void styleInspectorInput(InputField* field) {
    if (!field)
        return;
    field->normal.bgColor = {0.13f, 0.13f, 0.15f, 1.f};
    field->normal.textColor = {0.9f, 0.9f, 0.95f, 1.f};
    field->normal.borderColor = {0.28f, 0.28f, 0.34f, 1.f};
    field->normal.borderWidth = 1.f;
    field->hoveredStyle.borderColor = {0.42f, 0.42f, 0.52f, 1.f};
    field->focusedStyle.borderColor = {0.35f, 0.55f, 0.9f, 1.f};
    field->focusedStyle.borderWidth = 2.f;
    field->disabledStyle.bgColor = {0.09f, 0.09f, 0.11f, 1.f};
    field->disabledStyle.textColor = {0.62f, 0.62f, 0.68f, 1.f};
    field->disabledStyle.borderColor = {0.2f, 0.2f, 0.24f, 1.f};
    field->disabledStyle.borderWidth = 1.f;
    field->cornerRadii = {4.f, 4.f, 4.f, 4.f};
    field->padding = {8.f, 6.f};
}

bool tryParseFloat(const std::string& text, float& out) {
    char tail = '\0';
    return std::sscanf(text.c_str(), " %f %c", &out, &tail) == 1;
}

std::string trimCopy(const std::string& text) {
    const size_t start = text.find_first_not_of(" \t");
    if (start == std::string::npos)
        return {};
    const size_t end = text.find_last_not_of(" \t");
    return text.substr(start, end - start + 1);
}

bool parseInt(const std::string& text, int& out) {
    char tail = '\0';
    return std::sscanf(text.c_str(), " %d %c", &out, &tail) == 1;
}

bool isRefType(InspectType type) {
    return type == InspectType::Object ||
           type == InspectType::Component ||
           type == InspectType::Script;
}

const char* refTypeLabel(InspectType type) {
    switch (type) {
        case InspectType::Component: return "Component";
        case InspectType::Script: return "Script";
        default: return "Object";
    }
}

bool acceptsRef(Scene& scene, Entity e, const InspectField& field) {
    if (!scene.isValid(e))
        return false;
    if (field.type == InspectType::Object)
        return true;
    if (!field.requiredType)
        return false;
    if (!scene.has<Behaviours>(e))
        return false;
    const Behaviours& ib = scene.get<Behaviours>(e);
    if (field.type == InspectType::Component) {
        for (const auto& component : ib.components) {
            if (component && typeid(*component) == *field.requiredType)
                return true;
        }
        return false;
    }
    if (field.type == InspectType::Script) {
        for (const auto& script : ib.scripts) {
            if (script && typeid(*script) == *field.requiredType)
                return true;
        }
        return false;
    }
    return false;
}

std::string entityDisplayName(const Scene& scene, Entity e) {
    if (scene.has<Object>(e)) {
        const Object& obj = scene.get<Object>(e);
        if (!obj.name.empty())
            return obj.name;
    }
    return "Entity " + std::to_string(e.idx);
}

void styleObjectSlot(Button* btn, bool dropHover, bool dropValid, bool missing) {
    if (!btn)
        return;
    btn->centerText = false;
    btn->padding = {8.f, 4.f};
    btn->cornerRadii = {4.f, 4.f, 4.f, 4.f};
    btn->normal.textColor = missing
        ? glm::vec4{0.85f, 0.45f, 0.45f, 1.f}
        : glm::vec4{0.9f, 0.9f, 0.95f, 1.f};
    btn->hoveredStyle.textColor = btn->normal.textColor;
    btn->pressedStyle.textColor = btn->normal.textColor;
    btn->disabledStyle.textColor = {0.62f, 0.62f, 0.68f, 1.f};
    btn->normal.bgColor = {0.13f, 0.13f, 0.15f, 1.f};
    btn->hoveredStyle.bgColor = {0.16f, 0.16f, 0.19f, 1.f};
    btn->pressedStyle.bgColor = {0.1f, 0.1f, 0.12f, 1.f};
    btn->disabledStyle.bgColor = {0.09f, 0.09f, 0.11f, 1.f};
    btn->normal.borderWidth = 1.f;
    btn->hoveredStyle.borderWidth = 1.f;
    btn->pressedStyle.borderWidth = 1.f;
    btn->disabledStyle.borderWidth = 1.f;
    btn->normal.borderColor = {0.28f, 0.28f, 0.34f, 1.f};
    btn->hoveredStyle.borderColor = {0.42f, 0.42f, 0.52f, 1.f};
    btn->pressedStyle.borderColor = {0.28f, 0.28f, 0.34f, 1.f};
    btn->disabledStyle.borderColor = {0.2f, 0.2f, 0.24f, 1.f};
    if (dropHover) {
        const glm::vec4 bg = dropValid
            ? glm::vec4{0.16f, 0.28f, 0.22f, 1.f}
            : glm::vec4{0.28f, 0.16f, 0.16f, 1.f};
        const glm::vec4 border = dropValid
            ? glm::vec4{0.45f, 0.9f, 0.6f, 1.f}
            : glm::vec4{0.9f, 0.4f, 0.4f, 1.f};
        btn->normal.bgColor = bg;
        btn->hoveredStyle.bgColor = bg;
        btn->pressedStyle.bgColor = bg;
        btn->normal.borderColor = border;
        btn->hoveredStyle.borderColor = border;
        btn->pressedStyle.borderColor = border;
        btn->normal.borderWidth = 2.f;
        btn->hoveredStyle.borderWidth = 2.f;
        btn->pressedStyle.borderWidth = 2.f;
    }
}

void styleClearButton(Button* btn) {
    if (!btn)
        return;
    btn->text = "x";
    btn->centerText = true;
    btn->padding = {0.f, 0.f};
    btn->cornerRadii = {4.f, 4.f, 4.f, 4.f};
    btn->normal.bgColor = {0.22f, 0.16f, 0.16f, 1.f};
    btn->hoveredStyle.bgColor = {0.42f, 0.22f, 0.22f, 1.f};
    btn->pressedStyle.bgColor = {0.32f, 0.14f, 0.14f, 1.f};
    btn->normal.textColor = {0.9f, 0.75f, 0.75f, 1.f};
    btn->hoveredStyle.textColor = {1.f, 0.9f, 0.9f, 1.f};
    btn->disabledStyle.bgColor = {0.12f, 0.12f, 0.14f, 1.f};
    btn->disabledStyle.textColor = {0.45f, 0.45f, 0.5f, 1.f};
}

void styleSpaceButton(UIElement& el, bool active) {
    auto* btn = dynamic_cast<Button*>(el.widget.get());
    if (!btn)
        return;
    btn->centerText = true;
    btn->padding = {8.f, 4.f};
    btn->cornerRadii = {4.f, 4.f, 4.f, 4.f};
    btn->normal.bgColor = active
        ? glm::vec4{0.16f, 0.38f, 0.72f, 1.f}
        : glm::vec4{0.2f, 0.22f, 0.28f, 1.f};
    btn->normal.textColor = {0.92f, 0.92f, 0.96f, 1.f};
    btn->normal.borderColor = active
        ? glm::vec4{0.35f, 0.55f, 0.9f, 1.f}
        : glm::vec4{0.34f, 0.36f, 0.42f, 1.f};
    btn->normal.borderWidth = 1.f;
    btn->hoveredStyle.bgColor = {0.28f, 0.3f, 0.36f, 1.f};
    btn->pressedStyle.bgColor = {0.16f, 0.38f, 0.72f, 1.f};
}

glm::quat eulerYXZToQuat(const glm::vec3& eulerDeg) {
    const glm::quat qY =
        glm::angleAxis(glm::radians(eulerDeg.y), glm::vec3(0.f, 1.f, 0.f));
    const glm::quat qX =
        glm::angleAxis(glm::radians(eulerDeg.x), glm::vec3(1.f, 0.f, 0.f));
    const glm::quat qZ =
        glm::angleAxis(glm::radians(eulerDeg.z), glm::vec3(0.f, 0.f, 1.f));
    return qY * qX * qZ;
}

glm::vec3 quatToEulerYXZ(const glm::quat& q) {
    const glm::mat4 m = glm::mat4_cast(q);
    float yaw = 0.f;
    float pitch = 0.f;
    float roll = 0.f;
    glm::extractEulerAngleYXZ(m, yaw, pitch, roll);
    return glm::degrees(glm::vec3(pitch, yaw, roll));
}

glm::quat rotationQuatFromWorldMatrix(const glm::mat4& worldMatrix) {
    const glm::vec3 x = glm::vec3(worldMatrix[0]);
    const glm::vec3 y = glm::vec3(worldMatrix[1]);
    const glm::vec3 z = glm::vec3(worldMatrix[2]);
    const glm::mat3 rot(
        glm::normalize(x),
        glm::normalize(y),
        glm::normalize(z));
    return glm::quat_cast(glm::mat4(rot));
}

glm::vec3 worldPosition(const Transform& t) {
    return glm::vec3(t.worldMatrix[3]);
}

glm::vec3 worldScale(const Transform& t) {
    const glm::mat4& w = t.worldMatrix;
    return {
        glm::length(glm::vec3(w[0])),
        glm::length(glm::vec3(w[1])),
        glm::length(glm::vec3(w[2]))};
}

glm::vec3 worldRotationEuler(const Transform& t) {
    return quatToEulerYXZ(rotationQuatFromWorldMatrix(t.worldMatrix));
}

} // namespace

void TransformView::bind(
        UI& ui,
        UIElementID parentId,
        std::function<void(GizmoSpace)> onSpaceChange)
{
    ui_ = &ui;
    onSpaceChange_ = std::move(onSpaceChange);
    rootId_ = ui.createElement();
    ui.reparent(rootId_, parentId);

    UIElement& root = ui.get(rootId_);
    root.style.position = PositionMode::Relative;
    root.style.display = Display::Block;
    root.style.gap = Length::px(kTransformGap);
    root.style.width = Length::percent(100.f);
    root.style.height = Length::px(kTransformViewH);
    root.style.padding.left = Length::px(2.f);

    UIElementID spaceRowId = ui.createElement();
    ui.reparent(spaceRowId, rootId_);
    UIElement& spaceRow = ui.get(spaceRowId);
    spaceRow.style.position = PositionMode::Relative;
    spaceRow.style.display = Display::Flex;
    spaceRow.style.flexDirection = FlexDirection::Row;
    spaceRow.style.alignItems = AlignItems::Center;
    spaceRow.style.gap = Length::px(6.f);
    spaceRow.style.width = Length::percent(100.f);
    spaceRow.style.height = Length::px(kSpaceRowH);

    UIElementID spaceLabelId = ui.label(
        {0.f, 0.f},
        {0.f, kRowFontSize},
        {0.72f, 0.76f, 0.84f, 1.f},
        "Space");
    ui.reparent(spaceLabelId, spaceRowId);
    styleInfoLabel(ui.get(spaceLabelId), kRowFontSize);

    Font& font = ENGINE().assets.getFont("InterVariable");
    spaceLocalButtonId_ = ui.button({0.f, 0.f}, kAxisFontSize, "Local (L)", &font);
    spaceWorldButtonId_ = ui.button({0.f, 0.f}, kAxisFontSize, "World (Shift+W)", &font);
    ui.reparent(spaceLocalButtonId_, spaceRowId);
    ui.reparent(spaceWorldButtonId_, spaceRowId);

    UIElement& localBtnEl = ui.get(spaceLocalButtonId_);
    localBtnEl.style.position = PositionMode::Relative;
    localBtnEl.style.height = Length::px(24.f);
    localBtnEl.style.width = Length::px(88.f);
    styleSpaceButton(localBtnEl, true);
    if (auto* localBtn = dynamic_cast<Button*>(localBtnEl.widget.get()))
        localBtn->onClick = [this]() {
            if (onSpaceChange_)
                onSpaceChange_(GizmoSpace::Local);
        };

    UIElement& worldBtnEl = ui.get(spaceWorldButtonId_);
    worldBtnEl.style.position = PositionMode::Relative;
    worldBtnEl.style.height = Length::px(24.f);
    worldBtnEl.style.width = Length::px(120.f);
    styleSpaceButton(worldBtnEl, false);
    if (auto* worldBtn = dynamic_cast<Button*>(worldBtnEl.widget.get()))
        worldBtn->onClick = [this]() {
            if (onSpaceChange_)
                onSpaceChange_(GizmoSpace::World);
        };

    auto createRow = [&](const char* title, AxisFields& fields) {
        UIElementID titleId = ui.label({0.f, 0.f}, {0.f, 0.f}, {0.85f, 0.85f, 0.9f, 1.f}, title);
        ui.reparent(titleId, rootId_);
        styleInfoLabel(ui.get(titleId), kRowFontSize);

        UIElementID rowId = ui.createElement();
        ui.reparent(rowId, rootId_);
        UIElement& rowEl = ui.get(rowId);
        rowEl.style.position = PositionMode::Relative;
        rowEl.style.display = Display::Flex;
        rowEl.style.flexDirection = FlexDirection::Row;
        rowEl.style.gap = Length::px(6.f);
        rowEl.style.width = Length::percent(100.f);
        rowEl.style.height = Length::px(kAxisRowH);

        fields.xId = ui.inputField({0.f, 0.f}, {0.f, 0.f}, kAxisFontSize, "X");
        fields.yId = ui.inputField({0.f, 0.f}, {0.f, 0.f}, kAxisFontSize, "Y");
        fields.zId = ui.inputField({0.f, 0.f}, {0.f, 0.f}, kAxisFontSize, "Z");
        ui.reparent(fields.xId, rowId);
        ui.reparent(fields.yId, rowId);
        ui.reparent(fields.zId, rowId);
        for (UIElementID id : {fields.xId, fields.yId, fields.zId}) {
            UIElement& el = ui.get(id);
            el.style.position = PositionMode::Relative;
            el.style.flexGrow = 1.f;
            el.style.flexBasis = Length::px(0.f);
            el.style.height = Length::percent(100.f);
        }
    };
    createRow("Position", pos_);
    createRow("Rotation", rot_);
    createRow("Scale", scale_);

    auto setupField = [&](UIElementID id, const char* placeholder, std::string* pendingText, bool* dirty) {
        UIElement& el = ui.get(id);
        auto* field = dynamic_cast<InputField*>(el.widget.get());
        if (!field)
            return;
        field->placeholder = placeholder;
        field->normal.bgColor = {0.13f, 0.13f, 0.15f, 1.f};
        field->normal.textColor = {0.9f, 0.9f, 0.95f, 1.f};
        field->normal.borderColor = {0.28f, 0.28f, 0.34f, 1.f};
        field->normal.borderWidth = 1.f;
        field->hoveredStyle.borderColor = {0.42f, 0.42f, 0.52f, 1.f};
        field->focusedStyle.borderColor = {0.35f, 0.55f, 0.9f, 1.f};
        field->focusedStyle.borderWidth = 2.f;
        field->disabledStyle.bgColor = {0.09f, 0.09f, 0.11f, 1.f};
        field->disabledStyle.textColor = {0.62f, 0.62f, 0.68f, 1.f};
        field->disabledStyle.borderColor = {0.2f, 0.2f, 0.24f, 1.f};
        field->disabledStyle.borderWidth = 1.f;
        field->cornerRadii = {4.f, 4.f, 4.f, 4.f};
        field->padding = {8.f, 6.f};
        field->onSubmit = [pendingText, dirty](const std::string& value) {
            *pendingText = value;
            *dirty = true;
        };
    };
    auto setupAxis = [&](AxisFields& f) {
        setupField(f.xId, "X", &f.pendingX, &f.dirtyX);
        setupField(f.yId, "Y", &f.pendingY, &f.dirtyY);
        setupField(f.zId, "Z", &f.pendingZ, &f.dirtyZ);
    };
    setupAxis(pos_);
    setupAxis(rot_);
    setupAxis(scale_);
}

bool TransformView::parseFloat(const std::string& text, float& out) const {
    char tail = '\0';
    if (std::sscanf(text.c_str(), " %f %c", &out, &tail) == 1)
        return true;
    return false;
}

void TransformView::applyPendingEdits(
        Transform& transform,
        Entity entity,
        Scene& scene,
        GizmoSpace space)
{
    static const Transform kIdentityTransform;
    const Transform* parentT = &kIdentityTransform;
    if (scene.has<Hierarchy>(entity)) {
        const Entity parent = scene.get<Hierarchy>(entity).parent;
        if (scene.isValid(parent) && scene.has<Transform>(parent))
            parentT = &scene.get<Transform>(parent);
    }

    auto applyPosition = [&](const glm::vec3& v) {
        if (space == GizmoSpace::World) {
            transform.position =
                glm::vec3(parentT->worldInvMatrix * glm::vec4(v, 1.f));
        } else {
            transform.position = v;
        }
    };

    auto applyRotation = [&](const glm::vec3& eulerDeg) {
        if (space == GizmoSpace::World) {
            const glm::quat desiredWorldQ = eulerYXZToQuat(eulerDeg);
            const glm::quat parentQ =
                rotationQuatFromWorldMatrix(parentT->worldMatrix);
            const glm::quat localQ = glm::inverse(parentQ) * desiredWorldQ;
            transform.rotation = quatToEulerYXZ(localQ);
        } else {
            transform.rotation = eulerDeg;
        }
    };

    auto applyScale = [&](const glm::vec3& v) {
        if (space == GizmoSpace::World) {
            const glm::vec3 curWorld = worldScale(transform);
            glm::vec3 ratio{1.f, 1.f, 1.f};
            if (curWorld.x > 1e-8f)
                ratio.x = v.x / curWorld.x;
            if (curWorld.y > 1e-8f)
                ratio.y = v.y / curWorld.y;
            if (curWorld.z > 1e-8f)
                ratio.z = v.z / curWorld.z;
            transform.scale = transform.scale * ratio;
        } else {
            transform.scale = v;
        }
    };

    auto applyAxis = [&](AxisFields& f, glm::vec3 current, auto setFn) {
        float parsed = 0.f;
        bool changed = false;
        if (f.dirtyX) {
            if (parseFloat(f.pendingX, parsed)) { current.x = parsed; changed = true; }
            f.dirtyX = false;
        }
        if (f.dirtyY) {
            if (parseFloat(f.pendingY, parsed)) { current.y = parsed; changed = true; }
            f.dirtyY = false;
        }
        if (f.dirtyZ) {
            if (parseFloat(f.pendingZ, parsed)) { current.z = parsed; changed = true; }
            f.dirtyZ = false;
        }
        if (changed)
            setFn(current);
    };

    if (space == GizmoSpace::World) {
        applyAxis(pos_, worldPosition(transform), applyPosition);
        applyAxis(rot_, worldRotationEuler(transform), applyRotation);
        applyAxis(scale_, worldScale(transform), applyScale);
    } else {
        applyAxis(pos_, transform.position, applyPosition);
        applyAxis(rot_, transform.rotation, applyRotation);
        applyAxis(scale_, transform.scale, applyScale);
    }
}

void TransformView::setEditable(bool editable) {
    editable_ = editable;
    if (!ui_)
        return;
    auto setFieldDisabled = [&](UIElementID id) {
        if (id == INVALID_UI_ELEMENT)
            return;
        auto* field = dynamic_cast<InputField*>(ui_->get(id).widget.get());
        if (!field)
            return;
        field->disabled = !editable_;
    };
    auto setTriplet = [&](const AxisFields& f) {
        setFieldDisabled(f.xId);
        setFieldDisabled(f.yId);
        setFieldDisabled(f.zId);
    };
    setTriplet(pos_);
    setTriplet(rot_);
    setTriplet(scale_);
}

void TransformView::setSpace(GizmoSpace space) {
    space_ = space;
    if (!ui_ || spaceLocalButtonId_ == INVALID_UI_ELEMENT)
        return;
    styleSpaceButton(ui_->get(spaceLocalButtonId_), space == GizmoSpace::Local);
    styleSpaceButton(ui_->get(spaceWorldButtonId_), space == GizmoSpace::World);
}

void TransformView::update(
        Transform& transform,
        Entity entity,
        Scene& scene,
        bool editable,
        GizmoSpace space)
{
    if (!ui_ || rootId_ == INVALID_UI_ELEMENT)
        return;

    setEditable(editable);
    setSpace(space);

    if (editable_)
        applyPendingEdits(transform, entity, scene, space);

    auto syncField = [&](UIElementID id, const std::string& value) {
        auto* field = dynamic_cast<InputField*>(ui_->get(id).widget.get());
        if (!field)
            return;
        if (editable_ && field->focused)
            return;
        field->text = value;
        field->caretPos = std::min(field->caretPos, field->text.size());
    };

    const glm::vec3 pos =
        space == GizmoSpace::World ? worldPosition(transform) : transform.position;
    const glm::vec3 rot =
        space == GizmoSpace::World ? worldRotationEuler(transform) : transform.rotation;
    const glm::vec3 scale =
        space == GizmoSpace::World ? worldScale(transform) : transform.scale;

    syncField(pos_.xId, formatFloat(pos.x));
    syncField(pos_.yId, formatFloat(pos.y));
    syncField(pos_.zId, formatFloat(pos.z));
    syncField(rot_.xId, formatFloat(rot.x));
    syncField(rot_.yId, formatFloat(rot.y));
    syncField(rot_.zId, formatFloat(rot.z));
    syncField(scale_.xId, formatFloat(scale.x));
    syncField(scale_.yId, formatFloat(scale.y));
    syncField(scale_.zId, formatFloat(scale.z));
}

void InspectorPanel::bind(UI& ui, EditorPanel& panel) {
    ui_ = &ui;
    contentId_ = panel.contentId();
    built_ = true;

    UIElement& content = ui.get(contentId_);
    content.style.display = Display::Block;
    content.style.gap = Length::px(6.f);
    content.style.padding.left = Length::px(8.f);
    content.style.padding.right = Length::px(8.f);
    content.style.padding.top = Length::px(8.f);
    content.style.padding.bottom = Length::px(8.f);
    content.style.overflow = Overflow::Scroll;

    headerId_ = ui.label({0.f, 0.f}, {0.f, 0.f}, {0.95f, 0.95f, 0.98f, 1.f}, "Inspector");
    ui.reparent(headerId_, contentId_);
    styleInfoLabel(ui.get(headerId_), kHeaderFontSize);

    nameRowId_ = ui.createElement();
    ui.reparent(nameRowId_, contentId_);
    UIElement& nameRow = ui.get(nameRowId_);
    nameRow.style.position = PositionMode::Relative;
    nameRow.style.display = Display::Flex;
    nameRow.style.flexDirection = FlexDirection::Row;
    nameRow.style.alignItems = AlignItems::Center;
    nameRow.style.gap = Length::px(8.f);
    nameRow.style.width = Length::percent(100.f);
    nameRow.style.height = Length::px(kInfoLabelH);

    UIElementID nameLabelId = ui.label(
        {0.f, 0.f},
        {0.f, 0.f},
        {0.82f, 0.82f, 0.86f, 1.f},
        "Name");
    ui.reparent(nameLabelId, nameRowId_);
    styleInfoLabel(ui.get(nameLabelId), kRowFontSize);

    nameFieldId_ = ui.inputField(
        {0.f, 0.f},
        {0.f, 0.f},
        kRowFontSize,
        "Entity name");
    ui.reparent(nameFieldId_, nameRowId_);
    UIElement& nameFieldEl = ui.get(nameFieldId_);
    nameFieldEl.style.position = PositionMode::Relative;
    nameFieldEl.style.flexGrow = 1.f;
    nameFieldEl.style.flexBasis = Length::px(0.f);
    nameFieldEl.style.height = Length::percent(100.f);
    if (auto* nameField = dynamic_cast<InputField*>(nameFieldEl.widget.get())) {
        styleInspectorInput(nameField);
        nameField->onSubmit = [this](const std::string& value) {
            if (!ui_ || nameFieldId_ == INVALID_UI_ELEMENT)
                return;
            auto* field = dynamic_cast<InputField*>(ui_->get(nameFieldId_).widget.get());
            if (!field)
                return;
            field->text = trimCopy(value);
            field->caretPos = std::min(field->caretPos, field->text.size());
            namePendingApply_ = true;
        };
    }

    idLabelId_ = addInfoLabel("ID: -");
    parentId_ = addInfoLabel("Parent: -");
    childrenId_ = addInfoLabel("Children: 0");
    modelId_ = addInfoLabel("Has Model: no");
    debugId_ = addInfoLabel("Debug: off");

    transformView_.bind(ui, contentId_, [this](GizmoSpace space) {
        if (gizmoSpaceCallback_)
            gizmoSpaceCallback_(space);
    });
    componentsView_.bind(ui, contentId_, "Components");
    scriptsView_.bind(ui, contentId_, "Scripts");
    componentsView_.setVisible(false);
    scriptsView_.setVisible(false);
}

UIElementID InspectorPanel::addInfoLabel(const char* text) {
    UIElementID id = ui_->label({0.f, 0.f}, {0.f, 0.f}, {0.82f, 0.82f, 0.86f, 1.f}, text);
    ui_->reparent(id, contentId_);
    styleInfoLabel(ui_->get(id), kRowFontSize);
    return id;
}

void InspectorPanel::setLabelText(UIElementID id, const char* value) const {
    if (id == INVALID_UI_ELEMENT || !ui_)
        return;
    if (auto* label = dynamic_cast<Label*>(ui_->get(id).widget.get()))
        label->text = value;
}

void InspectorPanel::setLabelText(UIElementID id, const std::string& value) const {
    if (id == INVALID_UI_ELEMENT || !ui_)
        return;
    if (auto* label = dynamic_cast<Label*>(ui_->get(id).widget.get()))
        label->text = value;
}

void InspectorPanel::update(Scene& scene) {
    if (!built_ || !ui_ || contentId_ == INVALID_UI_ELEMENT)
        return;

    if (selectedId_ == Entity::invalid() || !scene.isValid(selectedId_)) {
        if (nameFieldId_ != INVALID_UI_ELEMENT) {
            auto* nameField = dynamic_cast<InputField*>(ui_->get(nameFieldId_).widget.get());
            if (nameField) {
                nameField->disabled = true;
                nameField->text.clear();
                nameField->placeholder = "Entity name";
                nameField->caretPos = 0;
            }
            nameFieldWasFocused_ = false;
            namePendingApply_ = false;
        }
        setLabelText(idLabelId_, "ID: -");
        setLabelText(parentId_, "Parent: -");
        setLabelText(childrenId_, "Children: 0");
        setLabelText(modelId_, "Has Model: no");
        setLabelText(debugId_, "Debug: off");
        transformView_.setEditable(false);
        componentsView_.setVisible(false);
        scriptsView_.setVisible(false);
        return;
    }

    Object& obj = scene.has<Object>(selectedId_)
        ? scene.get<Object>(selectedId_)
        : scene.add<Object>(selectedId_);
    Transform& transform = scene.get<Transform>(selectedId_);

    Entity parent = Entity::invalid();
    size_t childCount = 0;
    if (scene.has<Hierarchy>(selectedId_)) {
        const Hierarchy& h = scene.get<Hierarchy>(selectedId_);
        parent = h.parent;
        childCount = h.children.size();
    }

    if (nameFieldId_ != INVALID_UI_ELEMENT) {
        auto* nameField = dynamic_cast<InputField*>(ui_->get(nameFieldId_).widget.get());
        if (nameField) {
            nameField->disabled = !editable_;

            const bool applyOnBlur =
                nameFieldWasFocused_ && !nameField->focused && editable_;
            nameFieldWasFocused_ = nameField->focused;

            if (editable_ && namePendingApply_) {
                obj.name = trimCopy(nameField->text);
                namePendingApply_ = false;
            } else if (applyOnBlur) {
                obj.name = trimCopy(nameField->text);
            }

            if (!nameField->focused) {
                nameField->text = obj.name;
                nameField->placeholder = obj.name.empty()
                    ? ("Entity " + std::to_string(selectedId_.idx))
                    : "Entity name";
                nameField->caretPos =
                    std::min(nameField->caretPos, nameField->text.size());
            }
        }
    }

    setLabelText(idLabelId_, "ID: " + std::to_string(selectedId_.idx));
    setLabelText(
        parentId_,
        scene.isValid(parent) ? ("Parent: " + std::to_string(parent.idx)) : "Parent: -");
    setLabelText(childrenId_, "Children: " + std::to_string(childCount));
    setLabelText(modelId_, std::string("Has Model: ") + (obj.model ? "yes" : "no"));
    setLabelText(debugId_, std::string("Debug: ") + (obj.debug ? "on" : "off"));

    transformView_.update(
        transform, selectedId_, scene, editable_, gizmoSpace_);

    std::vector<IBehaviour*> components;
    std::vector<IBehaviour*> scripts;
    if (scene.has<Behaviours>(selectedId_)) {
        Behaviours& ib = scene.get<Behaviours>(selectedId_);
        components.reserve(ib.components.size());
        for (auto& component : ib.components)
            components.push_back(component.get());
        scripts.reserve(ib.scripts.size());
        for (auto& script : ib.scripts)
            scripts.push_back(script.get());
    }
    componentsView_.update(
        scene, components, editable_, draggingObjectId_, droppedObjectId_);
    scriptsView_.update(
        scene, scripts, editable_, draggingObjectId_, droppedObjectId_);
}

void BehaviourListView::bind(UI& ui, UIElementID parentId, const char* title) {
    ui_ = &ui;
    headerHeight_ = kRowFontSize + 10.f;
    emptyHeight_ = kRowFontSize + 8.f;

    rootId_ = ui.createElement();
    ui.reparent(rootId_, parentId);
    UIElement& root = ui.get(rootId_);
    root.style.position = PositionMode::Relative;
    root.style.display = Display::Block;
    root.style.gap = Length::px(kListGap);
    root.style.width = Length::percent(100.f);
    root.style.height = Length::px(headerHeight_ + kListGap + emptyHeight_);

    headerId_ = ui.label({0.f, 0.f}, {0.f, 0.f}, {0.95f, 0.95f, 0.98f, 1.f}, title);
    ui.reparent(headerId_, rootId_);
    styleInfoLabel(ui.get(headerId_), kRowFontSize);

    emptyId_ = ui.label({0.f, 0.f}, {0.f, 0.f}, {0.62f, 0.62f, 0.68f, 1.f}, "None");
    ui.reparent(emptyId_, rootId_);
    UIElement& empty = ui.get(emptyId_);
    empty.style.position = PositionMode::Relative;
    empty.style.height = Length::px(emptyHeight_);
    empty.style.width = Length::percent(100.f);
    empty.transform.fontSize = kRowFontSize - 2.f;
}

void BehaviourListView::setVisible(bool visible) {
    if (!ui_ || rootId_ == INVALID_UI_ELEMENT)
        return;
    setElementInFlow(rootId_, visible, headerHeight_ + kListGap + emptyHeight_);
}

void BehaviourListView::setElementInFlow(
        UIElementID id,
        bool inFlow,
        float heightPx) const
{
    if (!ui_ || id == INVALID_UI_ELEMENT)
        return;
    UIElement& e = ui_->get(id);
    e.visible = inFlow;
    e.style.position = inFlow ? PositionMode::Relative : PositionMode::Absolute;
    e.style.height = Length::px(inFlow ? heightPx : 0.f);
}

float BehaviourListView::fieldsHeight(size_t fieldCount) const {
    if (fieldCount == 0)
        return 0.f;
    float height = static_cast<float>(fieldCount) * kFieldRowH;
    if (fieldCount > 1)
        height += kFieldGap * static_cast<float>(fieldCount - 1);
    return height;
}

float BehaviourListView::cardHeight(size_t fieldCount) const {
    float height = kCardPad * 2.f + kCardHeaderH;
    const float fieldsH = fieldsHeight(fieldCount);
    if (fieldsH > 0.f)
        height += kFieldGap + fieldsH;
    return height;
}

float BehaviourListView::listHeight(const std::vector<IBehaviour*>& items) const {
    float height = headerHeight_;
    if (items.empty()) {
        height += kListGap + emptyHeight_;
        return height;
    }
    for (IBehaviour* item : items) {
        const size_t n = item ? item->inspectFields().size() : 0;
        height += kListGap + cardHeight(n);
    }
    return height;
}

void BehaviourListView::ensureCardCount(size_t count) {
    while (cards_.size() < count) {
        UIElementID root = ui_->createElement();
        ui_->reparent(root, rootId_);
        UIElement& rootEl = ui_->get(root);
        rootEl.style.position = PositionMode::Relative;
        rootEl.style.display = Display::Block;
        rootEl.style.gap = Length::px(kFieldGap);
        rootEl.style.width = Length::percent(100.f);
        rootEl.style.height = Length::px(cardHeight(0));
        rootEl.style.padding.left = Length::px(kCardPad);
        rootEl.style.padding.right = Length::px(kCardPad);
        rootEl.style.padding.top = Length::px(kCardPad);
        rootEl.style.padding.bottom = Length::px(kCardPad);
        auto& bg = rootEl.addWidget<Rect>();
        bg.color = {0.16f, 0.16f, 0.19f, 1.f};
        bg.cornerRadii = {6.f, 6.f, 6.f, 6.f};
        bg.borderWidth = 1.f;
        bg.borderColor = {0.28f, 0.28f, 0.34f, 1.f};

        UIElementID headerRow = ui_->createElement();
        ui_->reparent(headerRow, root);
        UIElement& headerEl = ui_->get(headerRow);
        headerEl.style.position = PositionMode::Relative;
        headerEl.style.display = Display::Flex;
        headerEl.style.flexDirection = FlexDirection::Row;
        headerEl.style.alignItems = AlignItems::Center;
        headerEl.style.width = Length::percent(100.f);
        headerEl.style.height = Length::px(kCardHeaderH);

        UIElementID nameId = ui_->label(
            {0.f, 0.f}, {0.f, 0.f}, {0.9f, 0.9f, 0.94f, 1.f}, "");
        ui_->reparent(nameId, headerRow);
        UIElement& nameEl = ui_->get(nameId);
        nameEl.style.position = PositionMode::Relative;
        nameEl.style.flexGrow = 1.f;
        nameEl.style.flexBasis = Length::px(0.f);
        nameEl.style.height = Length::percent(100.f);
        nameEl.transform.fontSize = kRowFontSize;

        UIElementID fieldsId = ui_->createElement();
        ui_->reparent(fieldsId, root);
        UIElement& fieldsEl = ui_->get(fieldsId);
        fieldsEl.style.position = PositionMode::Relative;
        fieldsEl.style.display = Display::Block;
        fieldsEl.style.gap = Length::px(kFieldGap);
        fieldsEl.style.width = Length::percent(100.f);
        fieldsEl.style.height = Length::px(0.f);

        cards_.push_back({root, nameId, fieldsId, {}});
    }
}

void BehaviourListView::ensureFieldCount(Card& card, size_t count) {
    while (card.fields.size() < count) {
        UIElementID row = ui_->createElement();
        ui_->reparent(row, card.fieldsId);
        UIElement& rowEl = ui_->get(row);
        rowEl.style.position = PositionMode::Relative;
        rowEl.style.display = Display::Flex;
        rowEl.style.flexDirection = FlexDirection::Row;
        rowEl.style.alignItems = AlignItems::Center;
        rowEl.style.gap = Length::px(8.f);
        rowEl.style.width = Length::percent(100.f);
        rowEl.style.height = Length::px(kFieldRowH);

        UIElementID labelId = ui_->label(
            {0.f, 0.f}, {0.f, 0.f}, {0.78f, 0.78f, 0.84f, 1.f}, "");
        ui_->reparent(labelId, row);
        UIElement& labelEl = ui_->get(labelId);
        labelEl.style.position = PositionMode::Relative;
        labelEl.style.flexGrow = 1.f;
        labelEl.style.flexBasis = Length::px(0.f);
        labelEl.style.height = Length::percent(100.f);
        labelEl.transform.fontSize = kAxisFontSize;

        UIElementID controlId = ui_->createElement();
        ui_->reparent(controlId, row);
        UIElement& controlEl = ui_->get(controlId);
        controlEl.style.position = PositionMode::Relative;
        controlEl.style.height = Length::percent(100.f);

        UIElementID clearId = ui_->button({0.f, 0.f}, kAxisFontSize, "x", nullptr);
        ui_->reparent(clearId, row);
        UIElement& clearEl = ui_->get(clearId);
        clearEl.style.position = PositionMode::Relative;
        clearEl.style.width = Length::px(kClearBtnW);
        clearEl.style.height = Length::px(kClearBtnW);
        clearEl.style.flexGrow = 0.f;
        clearEl.transform.fontSize = kAxisFontSize;
        styleClearButton(dynamic_cast<Button*>(clearEl.widget.get()));

        card.fields.push_back({row, labelId, controlId, clearId});
    }
}

void BehaviourListView::syncFieldRow(
        FieldRow& row,
        const InspectField& field,
        Scene& scene,
        bool editable,
        Entity draggingId,
        Entity droppedId)
{
    setElementInFlow(row.rootId, true, kFieldRowH);

    if (auto* label = dynamic_cast<Label*>(ui_->get(row.labelId).widget.get()))
        label->text = nicifyName(field.name);

    UIElement& controlEl = ui_->get(row.controlId);
    const bool typeChanged = !controlEl.widget || row.type != field.type;
    if (typeChanged)
        row.type = field.type;

    const bool refField = isRefType(field.type);
    setElementInFlow(row.clearId, refField, kClearBtnW);

    if (field.type == InspectType::Bool) {
        if (typeChanged)
            controlEl.addWidget<Checkbox>();
        controlEl.style.width = Length::px(kCheckboxSize);
        controlEl.style.flexGrow = 0.f;
        controlEl.style.flexBasis = Length::px(kCheckboxSize);
        controlEl.style.height = Length::px(kCheckboxSize);

        auto* box = dynamic_cast<Checkbox*>(controlEl.widget.get());
        if (!box)
            return;
        box->disabled = !editable;
        bool* value = static_cast<bool*>(field.ptr);
        box->checked = *value;
        box->onChange = [value](bool checked) {
            if (value)
                *value = checked;
        };
        return;
    }

    if (refField) {
        if (typeChanged) {
            auto& btn = controlEl.addWidget<Button>();
            btn.font = &ENGINE().assets.getFont("InterVariable");
        }
        controlEl.style.width = Length::px(0.f);
        controlEl.style.flexGrow = 1.f;
        controlEl.style.flexBasis = Length::px(0.f);
        controlEl.style.height = Length::percent(100.f);
        controlEl.transform.fontSize = kAxisFontSize;

        auto* slot = dynamic_cast<Button*>(controlEl.widget.get());
        auto* clear = dynamic_cast<Button*>(ui_->get(row.clearId).widget.get());
        Entity* value = static_cast<Entity*>(field.ptr);
        if (!slot || !value)
            return;

        const glm::vec2 mouse = ENGINE().input.mousePosition();
        const bool hovered = pointInRect(
            mouse, controlEl.transform.position, controlEl.transform.size);
        bool dropValid = false;
        if (draggingId != Entity::invalid() && scene.isValid(draggingId))
            dropValid = acceptsRef(scene, draggingId, field);
        const bool dropHover = editable && hovered && draggingId != Entity::invalid();

        if (editable && hovered && droppedId != Entity::invalid() && scene.isValid(droppedId) &&
            acceptsRef(scene, droppedId, field)) {
            *value = droppedId;
        }

        bool missing = false;
        if (*value == Entity::invalid()) {
            slot->text = std::string("None (") + refTypeLabel(field.type) + ")";
        } else if (!scene.isValid(*value)) {
            missing = true;
            slot->text = "Missing";
        } else if (!acceptsRef(scene, *value, field)) {
            missing = true;
            slot->text = "Missing";
        } else {
            slot->text = entityDisplayName(scene, *value);
        }

        slot->disabled = !editable;
        styleObjectSlot(slot, dropHover, dropValid, missing);
        slot->onClick = nullptr;

        styleClearButton(clear);
        if (clear) {
            clear->disabled = !editable || *value == Entity::invalid();
            Entity* clearValue = value;
            clear->onClick = [clearValue, editable]() {
                if (editable && clearValue)
                    *clearValue = Entity::invalid();
            };
        }
        return;
    }

    if (typeChanged) {
        auto& input = controlEl.addWidget<InputField>();
        input.selfId = row.controlId;
        input.font = &ENGINE().assets.getFont("InterVariable");
        styleInspectorInput(&input);
    }
    controlEl.style.width = Length::px(0.f);
    controlEl.style.flexGrow = 1.f;
    controlEl.style.flexBasis = Length::px(0.f);
    controlEl.style.height = Length::percent(100.f);
    controlEl.transform.fontSize = kAxisFontSize;

    auto* input = dynamic_cast<InputField*>(controlEl.widget.get());
    if (!input)
        return;
    input->disabled = !editable;

    const bool applyOnBlur = row.wasFocused && !input->focused && editable;
    row.wasFocused = input->focused;

    if (field.type == InspectType::Float) {
        float* value = static_cast<float*>(field.ptr);
        input->onSubmit = [value](const std::string& text) {
            float parsed = 0.f;
            if (value && tryParseFloat(text, parsed))
                *value = parsed;
        };
        if (applyOnBlur) {
            float parsed = 0.f;
            if (tryParseFloat(input->text, parsed))
                *value = parsed;
        }
        if (editable && input->focused)
            return;
        input->text = formatFloat(*value);
        input->caretPos = std::min(input->caretPos, input->text.size());
        return;
    }

    int* value = static_cast<int*>(field.ptr);
    input->onSubmit = [value](const std::string& text) {
        int parsed = 0;
        if (value && parseInt(text, parsed))
            *value = parsed;
    };
    if (applyOnBlur) {
        int parsed = 0;
        if (parseInt(input->text, parsed))
            *value = parsed;
    }
    if (editable && input->focused)
        return;
    input->text = std::to_string(*value);
    input->caretPos = std::min(input->caretPos, input->text.size());
}

void BehaviourListView::syncCard(
        Card& card,
        IBehaviour& behaviour,
        Scene& scene,
        bool editable,
        Entity draggingId,
        Entity droppedId)
{
    const auto& fields = behaviour.inspectFields();
    setElementInFlow(card.rootId, true, cardHeight(fields.size()));

    if (auto* name = dynamic_cast<Label*>(ui_->get(card.nameId).widget.get()))
        name->text = behaviour.typeName();

    const float fieldsH = fieldsHeight(fields.size());
    setElementInFlow(card.fieldsId, fieldsH > 0.f, fieldsH);
    ensureFieldCount(card, fields.size());

    for (size_t i = 0; i < card.fields.size(); ++i) {
        if (i >= fields.size() || !fields[i].ptr) {
            auto* box = dynamic_cast<Checkbox*>(
                ui_->get(card.fields[i].controlId).widget.get());
            if (box)
                box->onChange = nullptr;
            auto* input = dynamic_cast<InputField*>(
                ui_->get(card.fields[i].controlId).widget.get());
            if (input)
                input->onSubmit = nullptr;
            auto* slot = dynamic_cast<Button*>(
                ui_->get(card.fields[i].controlId).widget.get());
            if (slot)
                slot->onClick = nullptr;
            auto* clear = dynamic_cast<Button*>(
                ui_->get(card.fields[i].clearId).widget.get());
            if (clear)
                clear->onClick = nullptr;
            setElementInFlow(card.fields[i].rootId, false, 0.f);
            continue;
        }
        syncFieldRow(
            card.fields[i], fields[i], scene, editable, draggingId, droppedId);
    }
}

void BehaviourListView::update(
        Scene& scene,
        const std::vector<IBehaviour*>& items,
        bool editable,
        Entity draggingId,
        Entity droppedId)
{
    if (!ui_ || rootId_ == INVALID_UI_ELEMENT)
        return;

    setElementInFlow(rootId_, true, listHeight(items));
    ensureCardCount(items.size());

    const bool empty = items.empty();
    setElementInFlow(emptyId_, empty, emptyHeight_);

    for (size_t i = 0; i < cards_.size(); ++i) {
        if (i >= items.size() || !items[i]) {
            setElementInFlow(cards_[i].rootId, false, 0.f);
            continue;
        }
        syncCard(cards_[i], *items[i], scene, editable, draggingId, droppedId);
    }
}
