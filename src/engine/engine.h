#pragma once

#include <SDL2/SDL_events.h>

#include "camera.h"
#include "scene.h"
#include "raycasting.h"
#include "ui.h"
#include "input.h"
#include "window.h"
#include "asset_manager/asset_manager.h"
#include "asset_manager/mesh_registry.h"
#include "renderer/renderer.h"
#include "renderer/ui_renderer.h"
#include "renderer/debug_renderer.h"
#include "editor/editor.h"

#define ENGINE() \
    Engine::instance()

#define DT() \
    Engine::instance().app.deltaTime

class Game;

class Engine {
public:
    static Engine& instance();

    App app;
    Input input;
    AssetManager assets;
    Renderer renderer;
    UIRenderer uiRenderer;
    DebugRenderer debugRenderer;
    Scene scene;
    // Laid out in game space, drawn inside the game viewport.
    UI gameUi;
    // Laid out in window space, drawn over the whole window.
    UI editorUi;
    Raycasting raycasting;
    MeshRegistry meshRegistry;
    Editor editor;

    Entity activeCameraEntity = Entity::invalid();

    float G = 9.81;
    float fps = 0.0f;

    bool isPaused() const { return paused; }
    void setPaused(bool value);

    void init(Game* g);
    void run();

    Camera* getActiveCamera();
    // In-scene gameplay camera; ignores the editor view camera.
    Camera* getSceneCamera();

private:
    Game* game;
    bool paused = false;

    float fpsTimer = 0.0f;
    int fpsFrames = 0;

    std::vector<RenderCommand> renderCommands;

    // x, y, w, h in window pixels: where the game renders and what it believes
    // its window dimensions are.
    glm::vec4 gameViewport() const;

    void getInput(SDL_Event &event);
    void setupKeyBindings();
    void loadAssets();
    void callRenderer(
        std::vector<UIRenderCommand>& gameUiQue,
        std::vector<UIRenderCommand>& editorUiQue,
        const glm::vec4& viewport);

    void beginFrame();
    void endFrame();

    Engine() : editor(*this) {}
    ~Engine() = default;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
};
