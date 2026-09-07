#include "editor/editor.h"

#include <algorithm>
#include <cmath>
#include <cfloat>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include "engine/asset_manager/widgets.h"
#include "engine/camera.h"
#include "engine/engine.h"
#include "engine/input.h"
#include "engine/renderer/debug_renderer.h"
#include "engine/ui.h"
#include "engine/utils/geometry.h"
#include "engine/scene.h"

namespace {

constexpr float kDefaultViewportScale = 0.6f;
constexpr float kMinViewportW = 192.f;
constexpr float kMinViewportH = 108.f;
constexpr float kResizeHandle = 10.f;
constexpr float kToolbarHeight = 36.f;

constexpr float kGizmoScreenFactor = 0.12f;
constexpr float kGizmoPlanePadStart = 0.2f;
constexpr float kGizmoPlanePadEnd = 0.45f;
constexpr float kGizmoAxisPickRadius = 0.1f;
constexpr int kGizmoRingSegments = 32;
constexpr float kGizmoCenterScaleRadius = 0.12f;
constexpr float kGizmoScaleDragFactor = 0.2f;
constexpr float kGizmoMinScale = 1e-7f;

constexpr glm::vec3 kGizmoColorX{1.f, 0.2f, 0.2f};
constexpr glm::vec3 kGizmoColorY{0.2f, 1.f, 0.2f};
constexpr glm::vec3 kGizmoColorZ{0.25f, 0.45f, 1.f};
constexpr glm::vec3 kGizmoColorHighlight{1.f, 1.f, 0.25f};

glm::vec3 gizmoAxisVector(GizmoHandle handle) {
    switch (handle) {
    case GizmoHandle::AxisX: return {1.f, 0.f, 0.f};
    case GizmoHandle::AxisY: return {0.f, 1.f, 0.f};
    case GizmoHandle::AxisZ: return {0.f, 0.f, 1.f};
    default: return {0.f, 0.f, 0.f};
    }
}

glm::vec3 gizmoLocalAxisVector(GizmoHandle handle) {
    return gizmoAxisVector(handle);
}

glm::vec3 gizmoWorldAxis(const GizmoAxes& axes, GizmoHandle handle) {
    switch (handle) {
    case GizmoHandle::AxisX: return axes.x;
    case GizmoHandle::AxisY: return axes.y;
    case GizmoHandle::AxisZ: return axes.z;
    default: return {0.f, 0.f, 0.f};
    }
}

GizmoAxes gizmoAxesFromTransform(const Transform& transform) {
    const glm::mat4& world = transform.worldMatrix;
    GizmoAxes axes;
    const glm::vec3 rawX = glm::vec3(world[0]);
    const glm::vec3 rawY = glm::vec3(world[1]);
    const glm::vec3 rawZ = glm::vec3(world[2]);
    if (glm::dot(rawX, rawX) > 1e-12f)
        axes.x = glm::normalize(rawX);
    if (glm::dot(rawY, rawY) > 1e-12f)
        axes.y = glm::normalize(rawY);
    if (glm::dot(rawZ, rawZ) > 1e-12f)
        axes.z = glm::normalize(rawZ);
    return axes;
}

GizmoAxes gizmoAxesForSpace(const Transform& transform, GizmoSpace space) {
    if (space == GizmoSpace::Local)
        return gizmoAxesFromTransform(transform);
    GizmoAxes axes;
    axes.x = {1.f, 0.f, 0.f};
    axes.y = {0.f, 1.f, 0.f};
    axes.z = {0.f, 0.f, 1.f};
    return axes;
}

const Transform* parentTransform(Scene& scene, Entity entity) {
    static const Transform kIdentity;
    if (!scene.has<Hierarchy>(entity))
        return &kIdentity;
    const Entity parent = scene.get<Hierarchy>(entity).parent;
    if (!scene.isValid(parent) || !scene.has<Transform>(parent))
        return &kIdentity;
    return &scene.get<Transform>(parent);
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

glm::vec3 gizmoPlaneNormal(GizmoHandle handle, const GizmoAxes& axes) {
    switch (handle) {
    case GizmoHandle::PlaneXY: return axes.z;
    case GizmoHandle::PlaneXZ: return axes.y;
    case GizmoHandle::PlaneYZ: return axes.x;
    default: return axes.z;
    }
}

void gizmoPlaneAxes(
        GizmoHandle handle,
        const GizmoAxes& axes,
        glm::vec3& u,
        glm::vec3& v)
{
    switch (handle) {
    case GizmoHandle::PlaneXY:
        u = axes.x;
        v = axes.y;
        break;
    case GizmoHandle::PlaneXZ:
        u = axes.x;
        v = axes.z;
        break;
    case GizmoHandle::PlaneYZ:
        u = axes.y;
        v = axes.z;
        break;
    default:
        u = axes.x;
        v = axes.y;
        break;
    }
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

bool isAxisHandle(GizmoHandle handle) {
    return handle == GizmoHandle::AxisX ||
           handle == GizmoHandle::AxisY ||
           handle == GizmoHandle::AxisZ;
}

bool isPlaneHandle(GizmoHandle handle) {
    return handle == GizmoHandle::PlaneXY ||
           handle == GizmoHandle::PlaneXZ ||
           handle == GizmoHandle::PlaneYZ;
}

bool isCenterHandle(GizmoHandle handle) {
    return handle == GizmoHandle::Center;
}

void drawRing(
        DebugRenderer& debug,
        const glm::vec3& origin,
        const glm::vec3& u,
        const glm::vec3& v,
        float radius,
        const glm::vec3& color,
        bool depthTest)
{
    const int segments = static_cast<int>(kGizmoRingSegments);
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.f * PI * static_cast<float>(i) / segments;
        const float a1 = 2.f * PI * static_cast<float>(i + 1) / segments;
        const glm::vec3 p0 =
            origin + (u * std::cos(a0) + v * std::sin(a0)) * radius;
        const glm::vec3 p1 =
            origin + (u * std::cos(a1) + v * std::sin(a1)) * radius;
        debug.line(p0, p1, color, depthTest);
    }
}

float pickRing(
        const Ray& ray,
        const glm::vec3& origin,
        const glm::vec3& u,
        const glm::vec3& v,
        float radius,
        float pickThickness)
{
    const glm::vec3 normal = glm::normalize(glm::cross(u, v));
    glm::vec3 hit;
    if (!Raycasting::testPlaneIntersection(ray, origin, normal, hit))
        return FLT_MAX;

    const float radial = glm::length(hit - origin);
    const float distToRing = std::abs(radial - radius);
    if (distToRing > pickThickness)
        return FLT_MAX;

    return glm::length(hit - ray.origin);
}

} // namespace

Editor::Editor(Engine& engine)
    : engine_(engine)
{}

void Editor::init() {
    buildShell();
    syncVisibility();
}

void Editor::shutdown() {
    open_ = false;
}

void Editor::update() {
    Input& input = engine_.input;

    if (input.pressed(Action::ToggleEditor)) {
        // When editor is open but gameplay control is active, F3 should return
        // to editor control instead of closing the editor entirely.
        if (open_ && playState_ == EditorPlayState::Play) {
            setPlayState(EditorPlayState::Edit);
        } else {
            toggleOpen();
        }
    }

    if (input.pressed(Action::TogglePlay) && open_) {
        setPlayState(isEditing() ? EditorPlayState::Play : EditorPlayState::Edit);
    }

    if (!open_)
        return;

    if (isEditing() && !engine_.editorUi.hasFocus()) {
        if (input.pressed(SDL_SCANCODE_L))
            setGizmoSpace(GizmoSpace::Local);
        if (input.pressed(Action::GizmoMove) && input.shiftDown())
            setGizmoSpace(GizmoSpace::World);
        else if (input.pressed(Action::GizmoMove))
            setGizmoMode(GizmoMode::Move);
        if (input.pressed(Action::GizmoRotate))
            setGizmoMode(GizmoMode::Rotate);
        if (input.pressed(Action::GizmoScale))
            setGizmoMode(GizmoMode::Scale);
        if (input.pressed(Action::FrameSelection))
            frameSelection();
    }

    const float winW = static_cast<float>(engine_.app.width());
    const float winH = static_cast<float>(engine_.app.height());

    // Reflow the dock tree whenever the OS window size changes.
    if (winW != lastWindowSize_.x || winH != lastWindowSize_.y) {
        lastWindowSize_ = {winW, winH};
        syncAnchoredPanels(winW, winH);
        layout_.relayout(dockableBounds(winW, winH));
        applyLayoutRects();
    }

    toolbarView_.update(*this);
    toolbarPanel_.update(input, {winW, winH});

    for (EditorPanel* panel : panels_) {
        if (isEditing())
            panel->update(input, {winW, winH});
        else
            panel->clearDrag();
    }

    if (open_) {
        hierarchyView_.update(engine_.scene, isEditing());
        updateSceneInteraction();
        inspectorView_.setSelectedId(hierarchyView_.selectedId());
        inspectorView_.setEditable(isEditing());
        inspectorView_.setGizmoSpace(gizmoSpace_);
        inspectorView_.setObjectDrag(
            hierarchyView_.draggingId(),
            hierarchyView_.releasedDragId());
        inspectorView_.update(engine_.scene);

        if (isEditing()) {
            const Entity selectedId = hierarchyView_.selectedId();
            if (selectedId != Entity::invalid() &&
                selectedId != engine_.scene.root &&
                engine_.scene.has<Transform>(selectedId)) {
                const Transform& transform =
                    engine_.scene.get<Transform>(selectedId);
                const glm::vec3 origin = glm::vec3(transform.worldMatrix[3]);
                const float size = gizmoWorldSize(origin);
                const GizmoAxes axes = gizmoAxesForSpace(transform, gizmoSpace_);
                drawGizmo(gizmoMode_, origin, size, axes);
            }
        }
    }

    if (isEditing()) {
        syncResize();
        syncDocking();
    }

    viewportRect_ = viewportPanel_.rect();
}

void Editor::updateEditorCamera() {
    if (!open_ || !isEditing())
        return;

    Input& input = engine_.input;

    if (activeDragPanelId_ != INVALID_UI_ELEMENT ||
        activeResizePanelId_ != INVALID_UI_ELEMENT ||
        hierarchyView_.isDragging())
        return;

    glm::vec3 selectionPivot{0.f};
    Entity selectionId = Entity::invalid();
    const Entity selectedId = hierarchyView_.selectedId();
    if (selectedId != Entity::invalid() &&
        selectedId != engine_.scene.root &&
        engine_.scene.has<Transform>(selectedId)) {
        selectionId = selectedId;
        selectionPivot = glm::vec3(
            engine_.scene.get<Transform>(selectedId).worldMatrix[3]);
    }

    editorCamera_.update(
        input,
        gameViewportRect(),
        true,
        selectionId,
        selectionPivot);
}

void Editor::setOpen(bool open) {
    if (open_ == open)
        return;

    open_ = open;

    if (open_) {
        wasPausedBeforeOpen_ = engine_.isPaused();
        playState_ = EditorPlayState::Edit;
        engine_.setPaused(true);
        if (Camera* gameCam = engine_.getSceneCamera())
            editorCamera_.syncFrom(*gameCam);
        else
            editorCamera_.focusOn({0.f, 2.f, 0.f});
        if (!viewportInitialized_) {
            resetViewportDefault();
            viewportInitialized_ = true;
        } else {
            clampViewportToWindow();
        }
    } else {
        viewportDrag_ = ViewportDrag::None;
        clearGizmoDrag();
        engine_.setPaused(wasPausedBeforeOpen_);
        if (!wasPausedBeforeOpen_)
            engine_.gameUi.onUnpause();
        samplePanelB_.setVisible(false);
        inspectorPanel_.setVisible(false);
        hierarchyPanel_.setVisible(false);
        toolbarPanel_.setVisible(false);
    }

    syncVisibility();
}

void Editor::toggleOpen() {
    setOpen(!open_);
}

void Editor::setPlayState(EditorPlayState state) {
    if (!open_) {
        playState_ = state;
        return;
    }

    if (playState_ == state)
        return;

    playState_ = state;

    if (state == EditorPlayState::Edit) {
        engine_.setPaused(true);
        engine_.editorUi.onUnpause();
    } else {
        clearGizmoDrag();
        engine_.setPaused(false);
    }
}

void Editor::setGizmoMode(GizmoMode mode) {
    if (gizmoMode_ == mode)
        return;
    gizmoMode_ = mode;
    clearGizmoDrag();
}

void Editor::setGizmoSpace(GizmoSpace space) {
    if (gizmoSpace_ == space)
        return;
    gizmoSpace_ = space;
    clearGizmoDrag();
}

void Editor::exitEditMode() {
    if (!open_ || playState_ != EditorPlayState::Edit)
        return;

    playState_ = EditorPlayState::Play;
    clearGizmoDrag();
    engine_.editorUi.onUnpause();
}

void Editor::buildShell() {
    UI& ui = engine_.editorUi;

    rootId_ = ui.createElement();
    UIElement& root = ui.get(rootId_);
    root.visible = false;
    root.style.inset.left = Length::px(0.f);
    root.style.inset.right = Length::px(0.f);
    root.style.inset.top = Length::px(0.f);
    root.style.inset.bottom = Length::px(0.f);
    root.style.display = Display::Block;

    // Panel shells. Future Hierarchy / Inspector panels will reuse EditorPanel.
    const float winW = std::max(1.f, static_cast<float>(engine_.app.width()));
    const float winH = std::max(1.f, static_cast<float>(engine_.app.height()));
    const float dockH = std::max(1.f, winH - kToolbarHeight);

    toolbarPanel_.build(
            ui,
            rootId_,
            "Toolbar",
            {0.f, winH - kToolbarHeight, winW, kToolbarHeight});
    toolbarPanel_.setAnchored(true);
    toolbarPanel_.setShowTitleBar(false);
    if (UIElement& toolbarRoot = ui.get(toolbarPanel_.rootId()); toolbarRoot.widget) {
        auto* bg = static_cast<Rect*>(toolbarRoot.widget.get());
        bg->color = {0.14f, 0.15f, 0.18f, 1.f};
        bg->borderWidth = 0.f;
    }
    UIElement& toolbarContent = ui.get(toolbarPanel_.contentId());
    toolbarContent.style.padding.left = Length::px(0.f);
    toolbarContent.style.padding.right = Length::px(0.f);
    toolbarContent.style.padding.top = Length::px(0.f);
    toolbarContent.style.padding.bottom = Length::px(0.f);
    toolbarContent.style.overflow = Overflow::Hidden;
    toolbarView_.bind(ui, toolbarPanel_, *this);

    viewportPanel_.build(
            ui,
            rootId_,
            "Viewport",
            {
                std::floor(winW/4),
                std::floor(dockH/3),
                std::floor(2*winW/4),
                std::floor(2*dockH/3),
            });
    viewportPanel_.setDockedMode(true);
    if (UIElement& viewportRoot = ui.get(viewportPanel_.rootId()); viewportRoot.widget) {
        auto* bg = static_cast<Rect*>(viewportRoot.widget.get());
        bg->color = {0.f, 0.f, 0.f, 0.f};
        bg->borderWidth = 2.f;
        bg->borderColor = {0.35f, 0.45f, 0.7f, 1.f};
    }

    hierarchyPanel_.build(
            ui,
            rootId_,
            "Hierarchy",
            {
                0.f,
                0.f,
                std::floor(winW/4),
                dockH,
            });
    hierarchyPanel_.setDockedMode(true);
    hierarchyView_.bind(ui, hierarchyPanel_);

    samplePanelB_.build(
            ui,
            rootId_,
            "Panel B",
            {
                std::floor(winW/4),
                0.f,
                std::floor(2*winW/4),
                std::floor(dockH/3),
            });
    samplePanelB_.setDockedMode(true);

    inspectorPanel_.build(
            ui,
            rootId_,
            "Inspector",
            {
                std::floor(3*winW/4),
                0.f,
                std::floor(winW/4),
                dockH
            });
    inspectorPanel_.setDockedMode(true);
    inspectorView_.bind(ui, inspectorPanel_);
    inspectorView_.setGizmoSpaceCallback([this](GizmoSpace space) {
        setGizmoSpace(space);
    });

    panels_ = {&viewportPanel_, &hierarchyPanel_, &samplePanelB_, &inspectorPanel_};

    dockPreviewIndicatorId_ = ui.createElement();
    ui.reparent(dockPreviewIndicatorId_, rootId_);
    UIElement& indicator = ui.get(dockPreviewIndicatorId_);
    indicator.visible = false;
    indicator.style.position = PositionMode::Absolute;
    indicator.style.display = Display::Block;
    auto& indicatorRect = indicator.addWidget<Rect>();
    indicatorRect.color = {0.3f, 0.55f, 1.f, 0.35f};
    indicatorRect.borderWidth = 2.f;
    indicatorRect.borderColor = {0.55f, 0.75f, 1.f, 0.95f};

    layout_.initialize(
        {viewportPanel_.rootId(), hierarchyPanel_.rootId(),
         samplePanelB_.rootId(), inspectorPanel_.rootId()},
        {viewportPanel_.rect(), hierarchyPanel_.rect(),
         samplePanelB_.rect(), inspectorPanel_.rect()},
        dockableBounds(winW, winH));
    applyLayoutRects();
    syncAnchoredPanels(winW, winH);
    viewportRect_ = viewportPanel_.rect();
    lastWindowSize_ = {winW, winH};
}

void Editor::syncVisibility() {
    if (rootId_ != INVALID_UI_ELEMENT)
        engine_.editorUi.get(rootId_).visible = open_;
    toolbarPanel_.setVisible(open_);
    viewportPanel_.setVisible(open_);
    hierarchyPanel_.setVisible(open_);
    samplePanelB_.setVisible(open_);
    inspectorPanel_.setVisible(open_);
}

void Editor::resetViewportDefault() {
    const float winW = std::max(1.f, static_cast<float>(engine_.app.width()));
    const float winH = std::max(1.f, static_cast<float>(engine_.app.height()));

    const float vw = std::max(kMinViewportW, std::floor(winW * kDefaultViewportScale));
    const float vh = std::max(kMinViewportH, std::floor(winH * kDefaultViewportScale));
    viewportRect_ = {
        std::floor((winW - vw) * 0.5f),
        std::floor((winH - vh) * 0.5f),
        vw,
        vh,
    };
}

void Editor::clampViewportToWindow() {
    const float winW = std::max(1.f, static_cast<float>(engine_.app.width()));
    const float winH = std::max(1.f, static_cast<float>(engine_.app.height()));

    float x = viewportRect_.x;
    float y = viewportRect_.y;
    float w = viewportRect_.z;
    float h = viewportRect_.w;

    // Clip overflow on each edge instead.
    if (x < 0.f) {
        w += x;
        x = 0.f;
    }
    if (y < 0.f) {
        h += y;
        y = 0.f;
    }
    if (x + w > winW)
        w = winW - x;
    if (y + h > winH)
        h = winH - y;

    w = std::max(kMinViewportW, std::min(w, winW));
    h = std::max(kMinViewportH, std::min(h, winH));

    if (x + w > winW)
        x = winW - w;
    if (y + h > winH)
        y = winH - h;

    x = clampf(x, 0.f, std::max(0.f, winW - w));
    y = clampf(y, 0.f, std::max(0.f, winH - h));

    viewportRect_ = {x, y, w, h};
}

glm::vec4 Editor::dockableBounds(float winW, float winH) const {
    const float dockH = std::max(1.f, winH - kToolbarHeight);
    return {0.f, 0.f, winW, dockH};
}

void Editor::syncAnchoredPanels(float winW, float winH) {
    if (!toolbarPanel_.isBuilt())
        return;
    toolbarPanel_.setRect({0.f, winH - kToolbarHeight, winW, kToolbarHeight});
}

void Editor::applyLayoutRects() {
    for (EditorPanel* panel : panels_) {
        panel->setRect(layout_.panelRect(panel->rootId()));
    }
}

void Editor::syncResize() {
    Input& input = engine_.input;
    const glm::vec2 mouse = input.mousePosition();
    const float winW = static_cast<float>(engine_.app.width());
    const float winH = static_cast<float>(engine_.app.height());

    EditorPanel* resizingPanel = nullptr;
    for (EditorPanel* panel : panels_) {
        if (panel->isResizing()) {
            resizingPanel = panel;
            break;
        }
    }

    if (!resizingPanel) {
        activeResizePanelId_ = INVALID_UI_ELEMENT;
        activeResizeDrag_ = PanelDrag::None;
        resizeHandleX_ = {};
        resizeHandleY_ = {};
        return;
    }

    const PanelDrag drag = resizingPanel->dragMode();
    if (activeResizePanelId_ != resizingPanel->rootId() || activeResizeDrag_ != drag) {
        activeResizePanelId_ = resizingPanel->rootId();
        activeResizeDrag_ = drag;
        resizeStartMouse_ = mouse;
        resizeHandleX_ = {};
        resizeHandleY_ = {};

        switch (drag) {
            case PanelDrag::Left:
            case PanelDrag::Right:
                resizeHandleX_ = layout_.beginResize(activeResizePanelId_, drag);
                break;
            case PanelDrag::Top:
            case PanelDrag::Bottom:
                resizeHandleY_ = layout_.beginResize(activeResizePanelId_, drag);
                break;
            case PanelDrag::BottomLeft:
                resizeHandleX_ = layout_.beginResize(activeResizePanelId_, PanelDrag::Left);
                resizeHandleY_ = layout_.beginResize(activeResizePanelId_, PanelDrag::Bottom);
                break;
            case PanelDrag::BottomRight:
                resizeHandleX_ = layout_.beginResize(activeResizePanelId_, PanelDrag::Right);
                resizeHandleY_ = layout_.beginResize(activeResizePanelId_, PanelDrag::Bottom);
                break;
            case PanelDrag::TopLeft:
                resizeHandleX_ = layout_.beginResize(activeResizePanelId_, PanelDrag::Left);
                resizeHandleY_ = layout_.beginResize(activeResizePanelId_, PanelDrag::Top);
                break;
            case PanelDrag::TopRight:
                resizeHandleX_ = layout_.beginResize(activeResizePanelId_, PanelDrag::Right);
                resizeHandleY_ = layout_.beginResize(activeResizePanelId_, PanelDrag::Top);
                break;
            default:
                break;
        }
    }

    bool changed = false;
    if (resizeHandleX_.valid) {
        changed |= layout_.updateResize(resizeHandleX_, mouse.x - resizeStartMouse_.x);
    }
    if (resizeHandleY_.valid) {
        changed |= layout_.updateResize(resizeHandleY_, mouse.y - resizeStartMouse_.y);
    }

    if (changed) {
        layout_.relayout(dockableBounds(winW, winH));
        applyLayoutRects();
    }
}

void Editor::syncDocking() {
    if (dockPreviewIndicatorId_ == INVALID_UI_ELEMENT)
        return;

    UI& ui = engine_.editorUi;
    UIElement& indicator = ui.get(dockPreviewIndicatorId_);
    Input& input = engine_.input;
    const glm::vec2 mouse = input.mousePosition();

    // Docking/relocation is title-bar move only.
    EditorPanel* draggingPanel = nullptr;
    for (EditorPanel* panel : panels_) {
        if (panel->isMoving()) {
            draggingPanel = panel;
            break;
        }
    }

    if (!draggingPanel) {
        if (input.released(MouseAction::Left) &&
            activeDragPanelId_ != INVALID_UI_ELEMENT &&
            activeDockPreview_.valid) {
            if (layout_.dockPanel(activeDragPanelId_, activeDockPreview_)) {
                const float winW = static_cast<float>(engine_.app.width());
                const float winH = static_cast<float>(engine_.app.height());
                layout_.relayout(dockableBounds(winW, winH));
                applyLayoutRects();
            }
        }

        indicator.visible = false;
        activeDragPanelId_ = INVALID_UI_ELEMENT;
        activeDockPreview_ = {};
        return;
    }

    activeDragPanelId_ = draggingPanel->rootId();
    activeDockPreview_ = layout_.findDockPreview(*draggingPanel, mouse);
    if (activeDockPreview_.valid) {
        indicator.visible = true;
        indicator.style.inset.left = Length::px(activeDockPreview_.previewRect.x);
        indicator.style.inset.bottom = Length::px(activeDockPreview_.previewRect.y);
        indicator.style.width = Length::px(activeDockPreview_.previewRect.z);
        indicator.style.height = Length::px(activeDockPreview_.previewRect.w);
    } else {
        indicator.visible = false;
    }

    if (input.released(MouseAction::Left) && activeDragPanelId_ != INVALID_UI_ELEMENT) {
        if (layout_.dockPanel(activeDragPanelId_, activeDockPreview_)) {
            const float winW = static_cast<float>(engine_.app.width());
            const float winH = static_cast<float>(engine_.app.height());
            layout_.relayout(dockableBounds(winW, winH));
            applyLayoutRects();
        }
        activeDragPanelId_ = INVALID_UI_ELEMENT;
        activeDockPreview_ = {};
        indicator.visible = false;
    }
}

ViewportDrag Editor::hitTestViewport(glm::vec2 mouse, glm::vec4 r) {
    const float x = r.x;
    const float y = r.y;
    const float w = r.z;
    const float h = r.w;
    const float hs = kResizeHandle;

    if (!pointInRect(
            mouse,
            {x - hs, y - hs},
            {w + hs * 2.f, h + hs * 2.f}))
        return ViewportDrag::None;

    const bool left   = mouse.x <= x + hs;
    const bool right  = mouse.x >= x + w - hs;
    const bool bottom = mouse.y <= y + hs;
    const bool top    = mouse.y >= y + h - hs;

    if (bottom && left)  return ViewportDrag::BottomLeft;
    if (bottom && right) return ViewportDrag::BottomRight;
    if (top && left)     return ViewportDrag::TopLeft;
    if (top && right)    return ViewportDrag::TopRight;
    if (left)            return ViewportDrag::Left;
    if (right)           return ViewportDrag::Right;
    if (bottom)          return ViewportDrag::Bottom;
    if (top)             return ViewportDrag::Top;

    return ViewportDrag::Move;
}

glm::vec4 Editor::applyViewportDrag(
        ViewportDrag drag,
        glm::vec4 start,
        glm::vec2 delta)
{
    float x = start.x;
    float y = start.y;
    float w = start.z;
    float h = start.w;

    switch (drag) {
        case ViewportDrag::Move:
            x += delta.x;
            y += delta.y;
            break;
        case ViewportDrag::Left:
            x += delta.x;
            w -= delta.x;
            break;
        case ViewportDrag::Right:
            w += delta.x;
            break;
        case ViewportDrag::Bottom:
            y += delta.y;
            h -= delta.y;
            break;
        case ViewportDrag::Top:
            h += delta.y;
            break;
        case ViewportDrag::BottomLeft:
            x += delta.x;
            w -= delta.x;
            y += delta.y;
            h -= delta.y;
            break;
        case ViewportDrag::BottomRight:
            w += delta.x;
            y += delta.y;
            h -= delta.y;
            break;
        case ViewportDrag::TopLeft:
            x += delta.x;
            w -= delta.x;
            h += delta.y;
            break;
        case ViewportDrag::TopRight:
            w += delta.x;
            h += delta.y;
            break;
        default:
            break;
    }

    if (w < kMinViewportW) {
        if (drag == ViewportDrag::Left ||
            drag == ViewportDrag::BottomLeft ||
            drag == ViewportDrag::TopLeft) {
            x -= kMinViewportW - w;
        }
        w = kMinViewportW;
    }

    if (h < kMinViewportH) {
        if (drag == ViewportDrag::Bottom ||
            drag == ViewportDrag::BottomLeft ||
            drag == ViewportDrag::BottomRight) {
            y -= kMinViewportH - h;
        }
        h = kMinViewportH;
    }

    return {x, y, w, h};
}

void Editor::updateViewportInteraction() {
    Input& input = engine_.input;
    const glm::vec2 mouse = input.mousePosition();

    if (input.pressed(MouseAction::Left)) {
        viewportDrag_ = hitTestViewport(mouse, viewportRect_);
        if (viewportDrag_ != ViewportDrag::None) {
            dragStartMouse_ = mouse;
            dragStartRect_ = viewportRect_;
        }
    }

    if (input.down(MouseAction::Left) && viewportDrag_ != ViewportDrag::None) {
        const glm::vec2 delta = mouse - dragStartMouse_;
        viewportRect_ = applyViewportDrag(viewportDrag_, dragStartRect_, delta);
        clampViewportToWindow();
    }

    if (input.released(MouseAction::Left))
        viewportDrag_ = ViewportDrag::None;
}

bool Editor::makeViewportRay(glm::vec2 mouse, Ray& outRay) const {
    const glm::vec4 vp = gameViewportRect();
    if (vp.z <= 1.f || vp.w <= 1.f)
        return false;
    if (!pointInRect(mouse, {vp.x, vp.y}, {vp.z, vp.w}))
        return false;

    const Camera* viewCam = isEditing()
        ? &editorCamera_.camera()
        : engine_.getSceneCamera();
    if (!viewCam)
        return false;

    const float ndcX = ((mouse.x - vp.x) / vp.z) * 2.f - 1.f;
    const float ndcY = ((mouse.y - vp.y) / vp.w) * 2.f - 1.f;

    const glm::mat4 view = viewCam->view();
    const glm::mat4 projection = glm::perspective(
        glm::radians(45.f),
        vp.z / vp.w,
        0.1f,
        100.0f);
    const glm::mat4 invVP = glm::inverse(projection * view);

    glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.f, 1.f);
    if (std::abs(nearH.w) < 1e-8f)
        return false;
    nearH /= nearH.w;

    const glm::vec3 target(nearH);
    const glm::vec3 camPos = viewCam->pos;
    const glm::vec3 dir = target - camPos;
    if (glm::dot(dir, dir) < 1e-12f)
        return false;

    outRay.origin = camPos;
    outRay.direction = glm::normalize(dir);
    outRay.t = FLT_MAX;
    return true;
}

void Editor::clearGizmoDrag() {
    objectDragging_ = false;
    dragObjectId_ = Entity::invalid();
    gizmoActiveHandle_ = GizmoHandle::None;
    dragAxis_ = {0.f, 0.f, 0.f};
    dragLocalAxis_ = {0.f, 0.f, 0.f};
}

float Editor::frameExtentForEntity(
    const Object& obj,
    const Transform& transform) const
{
    if (!obj.model)
        return EditorCamera::kDefaultDistance * 0.5f;

    const Bounds bounds = obj.model->getBounds();
    if (glm::length(bounds.size) < 1e-6f)
        return EditorCamera::kDefaultDistance * 0.5f;

    const glm::vec3 col0 = glm::vec3(transform.worldMatrix[0]);
    const glm::vec3 col1 = glm::vec3(transform.worldMatrix[1]);
    const glm::vec3 col2 = glm::vec3(transform.worldMatrix[2]);
    const glm::vec3 worldSize = {
        glm::length(col0) * bounds.size.x,
        glm::length(col1) * bounds.size.y,
        glm::length(col2) * bounds.size.z,
    };
    return glm::length(worldSize) * 0.5f;
}

void Editor::frameSelection() {
    const Entity selectedId = hierarchyView_.selectedId();
    if (selectedId == Entity::invalid() || selectedId == engine_.scene.root)
        return;
    if (!engine_.scene.has<Transform>(selectedId))
        return;

    const Transform& transform = engine_.scene.get<Transform>(selectedId);
    glm::vec3 center = glm::vec3(transform.worldMatrix[3]);
    float extent = EditorCamera::kDefaultDistance * 0.5f;

    if (engine_.scene.has<Object>(selectedId)) {
        const Object& obj = engine_.scene.get<Object>(selectedId);
        if (obj.model) {
            const Bounds bounds = obj.model->getBounds();
            if (glm::length(bounds.size) > 1e-6f)
                center = glm::vec3(
                    transform.worldMatrix * glm::vec4(bounds.center, 1.f));
        }
        extent = frameExtentForEntity(obj, transform);
    }

    editorCamera_.frameOn(center, extent, selectedId);
}

float Editor::gizmoWorldSize(const glm::vec3& origin) const {
    const Camera* camera = isEditing()
        ? &editorCamera_.camera()
        : engine_.getSceneCamera();
    if (!camera)
        return 1.f;
    const float dist = glm::length(camera->pos - origin);
    return std::max(0.05f, dist * kGizmoScreenFactor);
}

GizmoHandle Editor::pickGizmoHandle(
    GizmoMode mode,
    const Ray& ray,
    const glm::vec3& origin,
    float size,
    const GizmoAxes& axes) const
{
    if (mode == GizmoMode::Rotate) {
        const float pickThickness = size * kGizmoAxisPickRadius;
        GizmoHandle best = GizmoHandle::None;
        float bestT = FLT_MAX;

        const auto tryRing = [&](GizmoHandle handle, const glm::vec3& u, const glm::vec3& v) {
            const float t = pickRing(ray, origin, u, v, size, pickThickness);
            if (t < bestT) {
                bestT = t;
                best = handle;
            }
        };

        tryRing(GizmoHandle::AxisX, axes.y, axes.z);
        tryRing(GizmoHandle::AxisY, axes.x, axes.z);
        tryRing(GizmoHandle::AxisZ, axes.x, axes.y);
        return best;
    }

    if (mode == GizmoMode::Scale) {
        const float pickRadius = size * kGizmoAxisPickRadius;
        GizmoHandle bestAxis = GizmoHandle::None;
        float bestAxisDist = pickRadius;
        float bestAxisT = FLT_MAX;

        const GizmoHandle axisHandles[] = {
            GizmoHandle::AxisX,
            GizmoHandle::AxisY,
            GizmoHandle::AxisZ,
        };
        for (GizmoHandle handle : axisHandles) {
            const glm::vec3 axis = gizmoWorldAxis(axes, handle);
            const glm::vec3 tip = origin + axis * size;
            float dist = 0.f;
            float rayT = 0.f;
            if (!Raycasting::testRaySegmentDistance(ray, origin, tip, dist, rayT))
                continue;
            if (dist > pickRadius)
                continue;
            if (dist < bestAxisDist - 1e-6f ||
                (std::abs(dist - bestAxisDist) < 1e-6f && rayT < bestAxisT)) {
                bestAxisDist = dist;
                bestAxisT = rayT;
                bestAxis = handle;
            }
        }

        float centerT = FLT_MAX;
        if (Raycasting::testSphereIntersection(
                ray, origin, size * kGizmoCenterScaleRadius, centerT) &&
            centerT < bestAxisT) {
            return GizmoHandle::Center;
        }
        return bestAxis;
    }

    // Move: planes + axis segments.
    const float pad0 = size * kGizmoPlanePadStart;
    const float pad1 = size * kGizmoPlanePadEnd;

    GizmoHandle bestPlane = GizmoHandle::None;
    float bestPlaneT = FLT_MAX;

    const GizmoHandle planes[] = {
        GizmoHandle::PlaneXY,
        GizmoHandle::PlaneXZ,
        GizmoHandle::PlaneYZ,
    };
    for (GizmoHandle handle : planes) {
        glm::vec3 u, v;
        gizmoPlaneAxes(handle, axes, u, v);
        const glm::vec3 normal = gizmoPlaneNormal(handle, axes);

        glm::vec3 hit;
        if (!Raycasting::testPlaneIntersection(ray, origin, normal, hit))
            continue;

        const glm::vec3 rel = hit - origin;
        const float du = glm::dot(rel, u);
        const float dv = glm::dot(rel, v);
        if (du < pad0 || du > pad1 || dv < pad0 || dv > pad1)
            continue;

        const float t = glm::length(hit - ray.origin);
        if (t < bestPlaneT) {
            bestPlaneT = t;
            bestPlane = handle;
        }
    }
    if (bestPlane != GizmoHandle::None)
        return bestPlane;

    GizmoHandle bestAxis = GizmoHandle::None;
    float bestAxisDist = size * kGizmoAxisPickRadius;
    float bestAxisT = FLT_MAX;
    const float pickRadius = size * kGizmoAxisPickRadius;

    const GizmoHandle axisHandles[] = {
        GizmoHandle::AxisX,
        GizmoHandle::AxisY,
        GizmoHandle::AxisZ,
    };
    for (GizmoHandle handle : axisHandles) {
        const glm::vec3 axis = gizmoWorldAxis(axes, handle);
        const glm::vec3 tip = origin + axis * size;
        float dist = 0.f;
        float rayT = 0.f;
        if (!Raycasting::testRaySegmentDistance(ray, origin, tip, dist, rayT))
            continue;
        if (dist > pickRadius)
            continue;
        if (dist < bestAxisDist - 1e-6f ||
            (std::abs(dist - bestAxisDist) < 1e-6f && rayT < bestAxisT)) {
            bestAxisDist = dist;
            bestAxisT = rayT;
            bestAxis = handle;
        }
    }
    return bestAxis;
}

bool Editor::beginGizmoDrag(
    GizmoHandle handle,
    const Ray& ray,
    Entity id,
    const glm::vec3& origin,
    float gizmoSize,
    const GizmoAxes& axes)
{
    const Camera* camera = isEditing()
        ? &editorCamera_.camera()
        : engine_.getSceneCamera();
    if (!camera)
        return false;

    if (!engine_.scene.has<Transform>(id))
        return false;

    Transform& transform = engine_.scene.get<Transform>(id);
    dragObjectId_ = id;
    gizmoActiveHandle_ = handle;
    dragPlanePoint_ = origin;
    dragAxis_ = {0.f, 0.f, 0.f};
    dragLocalAxis_ = {0.f, 0.f, 0.f};
    dragGizmoSize_ = gizmoSize;
    dragStartScale_ = transform.scale;

    if (isCenterHandle(handle)) {
        dragPlaneNormal_ = glm::normalize(camera->front);
    } else if (isAxisHandle(handle)) {
        const glm::vec3 axis = gizmoWorldAxis(axes, handle);
        dragAxis_ = axis;
        dragLocalAxis_ = gizmoLocalAxisVector(handle);
        if (gizmoMode_ == GizmoMode::Rotate) {
            dragPlaneNormal_ = axis;
        } else {
            const glm::vec3 camDir = glm::normalize(camera->front);
            glm::vec3 planeNormal = glm::cross(axis, glm::cross(axis, camDir));
            if (glm::dot(planeNormal, planeNormal) < 1e-8f) {
                const glm::vec3 fallback =
                    (std::abs(axis.y) < 0.9f)
                        ? glm::vec3(0.f, 1.f, 0.f)
                        : glm::vec3(1.f, 0.f, 0.f);
                planeNormal = glm::cross(axis, fallback);
            }
            dragPlaneNormal_ = glm::normalize(planeNormal);
        }
    } else if (isPlaneHandle(handle)) {
        dragPlaneNormal_ = gizmoPlaneNormal(handle, axes);
    } else {
        return false;
    }

    objectDragging_ = Raycasting::testPlaneIntersection(
        ray, dragPlanePoint_, dragPlaneNormal_, dragLastHit_);
    if (!objectDragging_) {
        clearGizmoDrag();
        return false;
    }
    return true;
}

void Editor::drawGizmo(
    GizmoMode mode,
    const glm::vec3& origin,
    float size,
    const GizmoAxes& axes)
{
    DebugRenderer& debug = engine_.debugRenderer;

    const auto colorFor = [this](GizmoHandle handle, const glm::vec3& base) {
        if (handle == gizmoActiveHandle_ || handle == gizmoHoveredHandle_)
            return kGizmoColorHighlight;
        return base;
    };

    constexpr bool kNoDepth = false;

    if (mode == GizmoMode::Rotate) {
        drawRing(
            debug, origin, axes.y, axes.z, size,
            colorFor(GizmoHandle::AxisX, kGizmoColorX), kNoDepth);
        drawRing(
            debug, origin, axes.x, axes.z, size,
            colorFor(GizmoHandle::AxisY, kGizmoColorY), kNoDepth);
        drawRing(
            debug, origin, axes.x, axes.y, size,
            colorFor(GizmoHandle::AxisZ, kGizmoColorZ), kNoDepth);
        return;
    }

    if (mode == GizmoMode::Scale) {
        const float cube = size * 0.08f;
        const glm::vec3 cubeSize{cube, cube, cube};

        debug.arrow(
            origin, axes.x, size,
            colorFor(GizmoHandle::AxisX, kGizmoColorX), kNoDepth);
        debug.arrow(
            origin, axes.y, size,
            colorFor(GizmoHandle::AxisY, kGizmoColorY), kNoDepth);
        debug.arrow(
            origin, axes.z, size,
            colorFor(GizmoHandle::AxisZ, kGizmoColorZ), kNoDepth);

        const auto drawTipCube = [&](GizmoHandle handle, const glm::vec3& axisColor) {
            const glm::vec3 axis = gizmoWorldAxis(axes, handle);
            const glm::vec3 tip = origin + axis * size;
            glm::mat4 world = glm::translate(glm::mat4(1.f), tip);
            debug.box(world, cubeSize, colorFor(handle, axisColor));
        };
        drawTipCube(GizmoHandle::AxisX, kGizmoColorX);
        drawTipCube(GizmoHandle::AxisY, kGizmoColorY);
        drawTipCube(GizmoHandle::AxisZ, kGizmoColorZ);

        const glm::vec3 centerColor =
            colorFor(GizmoHandle::Center, {0.85f, 0.85f, 0.9f});
        glm::mat4 centerWorld = glm::translate(glm::mat4(1.f), origin);
        debug.box(
            centerWorld,
            glm::vec3(size * kGizmoCenterScaleRadius * 2.f),
            centerColor);
        return;
    }

    // Move
    debug.arrow(
        origin, axes.x, size,
        colorFor(GizmoHandle::AxisX, kGizmoColorX), kNoDepth);
    debug.arrow(
        origin, axes.y, size,
        colorFor(GizmoHandle::AxisY, kGizmoColorY), kNoDepth);
    debug.arrow(
        origin, axes.z, size,
        colorFor(GizmoHandle::AxisZ, kGizmoColorZ), kNoDepth);

    const float pad0 = size * kGizmoPlanePadStart;
    const float pad1 = size * kGizmoPlanePadEnd;

    const auto drawPad = [&](GizmoHandle handle, const glm::vec3& baseColor) {
        glm::vec3 u, v;
        gizmoPlaneAxes(handle, axes, u, v);
        const glm::vec3 a = origin + u * pad0 + v * pad0;
        const glm::vec3 b = origin + u * pad1 + v * pad0;
        const glm::vec3 c = origin + u * pad1 + v * pad1;
        const glm::vec3 d = origin + u * pad0 + v * pad1;
        debug.quadOutline(a, b, c, d, colorFor(handle, baseColor), kNoDepth);
    };

    drawPad(GizmoHandle::PlaneXY, glm::mix(kGizmoColorX, kGizmoColorY, 0.5f));
    drawPad(GizmoHandle::PlaneXZ, glm::mix(kGizmoColorX, kGizmoColorZ, 0.5f));
    drawPad(GizmoHandle::PlaneYZ, glm::mix(kGizmoColorY, kGizmoColorZ, 0.5f));
}

void Editor::updateSceneInteraction() {
    Input& input = engine_.input;

    if (!isEditing() ||
        hierarchyView_.isDragging() ||
        activeDragPanelId_ != INVALID_UI_ELEMENT ||
        activeResizePanelId_ != INVALID_UI_ELEMENT) {
        clearGizmoDrag();
        gizmoHoveredHandle_ = GizmoHandle::None;
        return;
    }

    const glm::vec2 mouse = input.mousePosition();
    const glm::vec4 vp = gameViewportRect();
    const bool mouseInViewport = pointInRect(mouse, {vp.x, vp.y}, {vp.z, vp.w});

    const Entity selectedId = hierarchyView_.selectedId();
    const bool hasSelection =
        selectedId != Entity::invalid() &&
        selectedId != engine_.scene.root &&
        engine_.scene.has<Transform>(selectedId);

    glm::vec3 gizmoOrigin{0.f};
    float gizmoSize = 1.f;
    GizmoAxes gizmoAxes;
    if (hasSelection) {
        const Transform& selected = engine_.scene.get<Transform>(selectedId);
        gizmoOrigin = glm::vec3(selected.worldMatrix[3]);
        gizmoSize = gizmoWorldSize(gizmoOrigin);
        gizmoAxes = gizmoAxesForSpace(selected, gizmoSpace_);
    }

    if (!objectDragging_) {
        gizmoHoveredHandle_ = GizmoHandle::None;
        if (hasSelection && mouseInViewport) {
            Ray hoverRay;
            if (makeViewportRay(mouse, hoverRay))
                gizmoHoveredHandle_ = pickGizmoHandle(
                    gizmoMode_, hoverRay, gizmoOrigin, gizmoSize, gizmoAxes);
        }
    } else {
        gizmoHoveredHandle_ = gizmoActiveHandle_;
    }

    if (input.pressed(MouseAction::Left) && mouseInViewport) {
        Ray ray;
        if (!makeViewportRay(mouse, ray)) {
            clearGizmoDrag();
            return;
        }

        if (hasSelection) {
            const GizmoHandle handle =
                pickGizmoHandle(gizmoMode_, ray, gizmoOrigin, gizmoSize, gizmoAxes);
            if (handle != GizmoHandle::None) {
                beginGizmoDrag(
                    handle, ray, selectedId, gizmoOrigin, gizmoSize, gizmoAxes);
                return;
            }
        }

        const RaycastHit hit = engine_.raycasting.castRay(ray);
        if (hit.distance < FLT_MAX &&
            hit.entity != Entity::invalid() &&
            hit.entity != engine_.scene.root) {
            hierarchyView_.setSelectedId(hit.entity);
            inspectorView_.setSelectedId(hit.entity);
            clearGizmoDrag();
        } else {
            hierarchyView_.setSelectedId(Entity::invalid());
            inspectorView_.setSelectedId(Entity::invalid());
            clearGizmoDrag();
        }
    }

    if (objectDragging_ &&
        dragObjectId_ != Entity::invalid() &&
        gizmoActiveHandle_ != GizmoHandle::None &&
        input.down(MouseAction::Left)) {
        Ray ray;
        const glm::vec2 clampedMouse = {
            clampf(mouse.x, vp.x, vp.x + vp.z),
            clampf(mouse.y, vp.y, vp.y + vp.w),
        };
        if (!makeViewportRay(clampedMouse, ray))
            return;

        glm::vec3 hitPoint;
        if (!Raycasting::testPlaneIntersection(
                ray, dragPlanePoint_, dragPlaneNormal_, hitPoint))
            return;

        if (!engine_.scene.has<Transform>(dragObjectId_))
            return;

        Transform& transform = engine_.scene.get<Transform>(dragObjectId_);
        const Transform& parent = *parentTransform(engine_.scene, dragObjectId_);

        if (gizmoMode_ == GizmoMode::Rotate && isAxisHandle(gizmoActiveHandle_)) {
            const auto projectOntoPlane = [&](const glm::vec3& v) {
                return v - dragAxis_ * glm::dot(v, dragAxis_);
            };
            glm::vec3 arm = projectOntoPlane(hitPoint - dragPlanePoint_);
            glm::vec3 prevArm = projectOntoPlane(dragLastHit_ - dragPlanePoint_);
            dragLastHit_ = hitPoint;

            const float armLen = glm::length(arm);
            const float prevArmLen = glm::length(prevArm);
            if (armLen < 1e-8f || prevArmLen < 1e-8f)
                return;

            arm /= armLen;
            prevArm /= prevArmLen;

            const float angleRad = std::atan2(
                glm::dot(glm::cross(prevArm, arm), dragAxis_),
                glm::dot(prevArm, arm));
            const float angleDeg = glm::degrees(angleRad);
            if (std::abs(angleDeg) < 1e-4f)
                return;

            const glm::quat currentQ = eulerYXZToQuat(transform.rotation);
            glm::quat newQ;
            if (gizmoSpace_ == GizmoSpace::World) {
                const glm::quat parentQ =
                    rotationQuatFromWorldMatrix(parent.worldMatrix);
                const glm::quat worldQ = parentQ * currentQ;
                newQ = glm::inverse(parentQ) *
                    (glm::angleAxis(angleRad, dragAxis_) * worldQ);
            } else {
                newQ = currentQ * glm::angleAxis(angleRad, dragLocalAxis_);
            }
            transform.rotation = quatToEulerYXZ(newQ);
            return;
        }

        glm::vec3 worldDelta = hitPoint - dragLastHit_;
        dragLastHit_ = hitPoint;

        if (gizmoMode_ == GizmoMode::Scale) {
            const float scaleDelta =
                (glm::dot(worldDelta, isCenterHandle(gizmoActiveHandle_)
                    ? dragPlaneNormal_
                    : dragAxis_) / dragGizmoSize_) * kGizmoScaleDragFactor;

            if (isCenterHandle(gizmoActiveHandle_)) {
                if (std::abs(scaleDelta) < 1e-10f)
                    return;
                const float factor = 1.f + scaleDelta;
                glm::vec3 scaled = dragStartScale_ * factor;
                scaled = glm::max(scaled, glm::vec3(kGizmoMinScale));
                transform.scale = scaled;
                return;
            }

            if (isAxisHandle(gizmoActiveHandle_)) {
                if (std::abs(scaleDelta) < 1e-10f)
                    return;

                glm::vec3 scale = transform.scale;
                const float factor = 1.f + scaleDelta;
                if (gizmoSpace_ == GizmoSpace::World) {
                    glm::vec3 localDir = glm::vec3(
                        parent.worldInvMatrix * glm::vec4(dragAxis_, 0.f));
                    if (glm::dot(localDir, localDir) > 1e-12f)
                        localDir = glm::normalize(localDir);
                    const glm::vec3 weights = glm::abs(localDir);
                    scale.x = std::max(
                        kGizmoMinScale, scale.x * (1.f + scaleDelta * weights.x));
                    scale.y = std::max(
                        kGizmoMinScale, scale.y * (1.f + scaleDelta * weights.y));
                    scale.z = std::max(
                        kGizmoMinScale, scale.z * (1.f + scaleDelta * weights.z));
                } else {
                    if (gizmoActiveHandle_ == GizmoHandle::AxisX)
                        scale.x = std::max(kGizmoMinScale, scale.x * factor);
                    else if (gizmoActiveHandle_ == GizmoHandle::AxisY)
                        scale.y = std::max(kGizmoMinScale, scale.y * factor);
                    else if (gizmoActiveHandle_ == GizmoHandle::AxisZ)
                        scale.z = std::max(kGizmoMinScale, scale.z * factor);
                }
                transform.scale = scale;
                return;
            }
        }

        // Move
        if (isAxisHandle(gizmoActiveHandle_)) {
            const float along = glm::dot(worldDelta, dragAxis_);
            worldDelta = dragAxis_ * along;
        } else if (isPlaneHandle(gizmoActiveHandle_)) {
            // plane drag: worldDelta already constrained by plane
        } else {
            return;
        }

        if (glm::dot(worldDelta, worldDelta) < 1e-12f)
            return;

        const glm::vec3 localDelta = glm::vec3(
            parent.worldInvMatrix * glm::vec4(worldDelta, 0.f));
        transform.position = transform.position + localDelta;
    }

    if (input.released(MouseAction::Left))
        clearGizmoDrag();
}

glm::vec4 Editor::gameViewportRect() const {
    const float winW = std::max(1.f, static_cast<float>(engine_.app.width()));
    const float winH = std::max(1.f, static_cast<float>(engine_.app.height()));

    if (!open_)
        return {0.f, 0.f, winW, winH};

    const UIElement& content = engine_.editorUi.get(viewportPanel_.contentId());
    const glm::vec4 contentRect = {
        content.transform.position.x,
        content.transform.position.y,
        content.transform.size.x,
        content.transform.size.y
    };
    if (contentRect.z > 1.f && contentRect.w > 1.f)
        return contentRect;

    // During a dock-tree mutation frame, layout sizes can be transiently zero.
    if (viewportRect_.z > 1.f && viewportRect_.w > 1.f)
        return viewportRect_;

    return {0.f, 0.f, winW, winH};
}
