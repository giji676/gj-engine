#pragma once

#include <glm/glm.hpp>
#include <vector>

#include "editor/editor_camera.h"
#include "editor/editor_layout.h"
#include "editor/hierarchy_panel.h"
#include "editor/inspector_panel.h"
#include "editor/panel.h"
#include "editor/toolbar_panel.h"
#include "engine/camera.h"
#include "engine/defines.h"
#include "engine/entity.h"
#include "engine/raycasting.h"

class Engine;
struct Object;
struct Transform;

enum class EditorPlayState {
    Edit,
    Play,
};

enum class ViewportDrag {
    None,
    Move,
    Left,
    Right,
    Bottom,
    Top,
    BottomLeft,
    BottomRight,
    TopLeft,
    TopRight,
};

enum class GizmoMode {
    Move,
    Rotate,
    Scale,
};

// Object-local axes expressed in world space (from worldMatrix basis).
struct GizmoAxes {
    glm::vec3 x{1.f, 0.f, 0.f};
    glm::vec3 y{0.f, 1.f, 0.f};
    glm::vec3 z{0.f, 0.f, 1.f};
};

enum class GizmoHandle {
    None,
    Center,
    AxisX,
    AxisY,
    AxisZ,
    PlaneXY,
    PlaneXZ,
    PlaneYZ,
};

// Edit - scene frozen, editor chrome visible (default when editor is open).
// Play - game sim runs while editor shell stays open (will be added later).
class Editor {
public:
    explicit Editor(Engine& engine);

    void init();
    void shutdown();
    void update();
    // After scene world matrices are updated (input + fresh pivots).
    void updateEditorCamera();

    bool isOpen() const { return open_; }
    EditorPlayState playState() const { return playState_; }

    void setOpen(bool open);
    void toggleOpen();
    void setPlayState(EditorPlayState state);
    // Leave editor-tool control without changing engine pause.
    // Used when ESC opens the in-game pause menu over an open editor.
    void exitEditMode();
    bool isEditing() const {
        return open_ && playState_ == EditorPlayState::Edit;
    }

    GizmoMode gizmoMode() const { return gizmoMode_; }
    void setGizmoMode(GizmoMode mode);
    GizmoSpace gizmoSpace() const { return gizmoSpace_; }
    void setGizmoSpace(GizmoSpace space);

    Camera* editModeCamera() { return &editorCamera_.camera(); }

    // Window-space x, y, w, h the game renders into. While the editor is open
    // this is the floating, resizable sub-window; otherwise it is the whole window.
    glm::vec4 gameViewportRect() const;

private:
    Engine& engine_;

    bool open_ = false;
    bool wasPausedBeforeOpen_ = false;
    EditorPlayState playState_ = EditorPlayState::Edit;

    bool viewportInitialized_ = false;
    glm::vec4 viewportRect_ = {0.f, 0.f, 0.f, 0.f};
    glm::vec2 lastWindowSize_ = {0.f, 0.f};

    ViewportDrag viewportDrag_ = ViewportDrag::None;
    glm::vec2 dragStartMouse_ = {0.f, 0.f};
    glm::vec4 dragStartRect_ = {0.f, 0.f, 0.f, 0.f};

    bool objectDragging_ = false;
    Entity dragObjectId_ = Entity::invalid();
    GizmoHandle gizmoActiveHandle_ = GizmoHandle::None;
    GizmoHandle gizmoHoveredHandle_ = GizmoHandle::None;
    glm::vec3 dragPlanePoint_{0.f};
    glm::vec3 dragPlaneNormal_{0.f, 0.f, 1.f};
    glm::vec3 dragAxis_{0.f};
    glm::vec3 dragLastHit_{0.f};
    glm::vec3 dragStartScale_{1.f};
    float dragGizmoSize_ = 1.f;
    glm::vec3 dragLocalAxis_{0.f};
    GizmoMode gizmoMode_ = GizmoMode::Move;
    GizmoSpace gizmoSpace_ = GizmoSpace::Local;

    UIElementID rootId_ = INVALID_UI_ELEMENT;
    UIElementID dockPreviewIndicatorId_ = INVALID_UI_ELEMENT;

    EditorPanel toolbarPanel_;
    EditorPanel viewportPanel_;
    EditorPanel hierarchyPanel_;
    EditorPanel samplePanelB_;
    EditorPanel inspectorPanel_;
    HierarchyPanel hierarchyView_;
    InspectorPanel inspectorView_;
    ToolbarPanel toolbarView_;
    EditorCamera editorCamera_;
    EditorLayout layout_;
    std::vector<EditorPanel*> panels_;

    UIElementID activeDragPanelId_ = INVALID_UI_ELEMENT;
    EditorLayout::DockPreview activeDockPreview_;

    UIElementID activeResizePanelId_ = INVALID_UI_ELEMENT;
    PanelDrag activeResizeDrag_ = PanelDrag::None;
    glm::vec2 resizeStartMouse_ = {0.f, 0.f};
    EditorLayout::ResizeHandle resizeHandleX_;
    EditorLayout::ResizeHandle resizeHandleY_;

    void buildShell();
    void syncVisibility();
    void resetViewportDefault();
    void clampViewportToWindow();
    void syncDocking();
    void syncResize();
    void applyLayoutRects();
    void syncAnchoredPanels(float winW, float winH);
    glm::vec4 dockableBounds(float winW, float winH) const;
    void updateViewportInteraction();
    void updateSceneInteraction();

    bool makeViewportRay(glm::vec2 mouse, Ray& outRay) const;
    void frameSelection();
    float frameExtentForEntity(const Object& obj, const Transform& transform) const;
    float gizmoWorldSize(const glm::vec3& origin) const;
    GizmoHandle pickGizmoHandle(
        GizmoMode mode,
        const Ray& ray,
        const glm::vec3& origin,
        float size,
        const GizmoAxes& axes) const;
    bool beginGizmoDrag(
        GizmoHandle handle,
        const Ray& ray,
        Entity id,
        const glm::vec3& origin,
        float gizmoSize,
        const GizmoAxes& axes);
    void drawGizmo(
        GizmoMode mode,
        const glm::vec3& origin,
        float size,
        const GizmoAxes& axes);
    void clearGizmoDrag();

    static ViewportDrag hitTestViewport(glm::vec2 mouse, glm::vec4 rect);
    static glm::vec4 applyViewportDrag(
        ViewportDrag drag,
        glm::vec4 start,
        glm::vec2 delta);
};
