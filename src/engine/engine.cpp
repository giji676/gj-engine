#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_mouse.h>
#include <glad/glad.h>
#include <chrono>
#include <iostream>

#include "engine/engine.h"
#include "engine/input.h"
#include "engine/profilers/profile_scope.h"
#include "engine/profilers/profiler.h"
#include "engine/renderer/ui_renderer.h"
#include "game/game.h"
#include "glm/ext/matrix_clip_space.hpp"

Engine& Engine::instance() {
    static Engine instance;
    return instance;
}

void Engine::init(Game *g) {
    app = App();
    app.initialize();

    setupKeyBindings();
    loadAssets();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    
    game = g;
    scene.init();
    game->init();
    editor.init();
    renderer.init(&meshRegistry);
    uiRenderer.init();
    debugRenderer.init();
}

void Engine::run() {
    SDL_Event event;
    app.initDeltaTime();

    while (app.running) {
        Profiler::instance().beginFrame();
        {
            PROFILE_SCOPE("Engine::run");
            beginFrame();

            editorUi.setInteractable(editor.isOpen());
            getInput(event);
            {
                PROFILE_SCOPE("Engine::editor.update");
                editor.update();
            }

            // Editor chrome lives in window space.
            const float winW = static_cast<float>(app.width());
            const float winH = static_cast<float>(app.height());
            editorUi.setSurface({0.f, 0.f}, {winW, winH}, {winW, winH});
            editorUi.setInteractable(editor.isOpen());
            editorUi.update();

            const glm::vec4 viewport = gameViewport();
            const glm::vec2 vpSize = {viewport.z, viewport.w};

            // Layout in viewport space (1:1). Fixed px sizes stay fixed; % sizes
            // resolve against the viewport and scale when it is resized.
            gameUi.setSurface({viewport.x, viewport.y}, vpSize, vpSize);

            game->update();
            scene.update(app.deltaTime, !paused);

            if (editor.isOpen() && editor.isEditing())
                editor.updateEditorCamera();

            gameUi.update();

            auto gameUiCommands = gameUi.buildRenderList();
            auto editorUiCommands = editorUi.buildRenderList();
            callRenderer(gameUiCommands, editorUiCommands, viewport);

            {
                // TODO: CPU idle time goes up when BVH raycast makes a lot of calls?
                GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                PROFILE_SCOPE("CPU idle");
                while (glClientWaitSync(fence, 0, 0) == GL_TIMEOUT_EXPIRED) {
                }
                glDeleteSync(fence);
            }

            endFrame();
        }
        Profiler::instance().endFrame();
    }

    app.cleanup();
}

glm::vec4 Engine::gameViewport() const {
    return editor.gameViewportRect();
}

void Engine::callRenderer(
        std::vector<UIRenderCommand>& gameUiQue,
        std::vector<UIRenderCommand>& editorUiQue,
        const glm::vec4& viewport)
{
    const GLint vx = static_cast<GLint>(viewport.x);
    const GLint vy = static_cast<GLint>(viewport.y);
    const GLsizei vw = static_cast<GLsizei>(viewport.z);
    const GLsizei vh = static_cast<GLsizei>(viewport.w);

    // Game pass: confined to the viewport, drawn in game space.
    glViewport(vx, vy, vw, vh);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx, vy, vw, vh);

    // Reset settings for the game pass.
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    Camera* camera = getActiveCamera();
    glm::mat4 view = camera ? camera->view() : glm::mat4(1.f);

    glm::mat4 projection = glm::perspective(
        glm::radians(45.f),
        static_cast<float>(vw) / static_cast<float>(vh),
        0.1f,
        100.0f
    );

    glm::mat4 vp = projection * view;
    Frustum frustum = Frustum::fromMatrix(vp);

    renderCommands.clear();
    scene.collectRenderCommands(frustum, renderCommands);

    game->render(view, projection);
    renderer.render(renderCommands, view, projection);
    debugRenderer.render(view, projection);

    const float vpW = viewport.z;
    const float vpH = viewport.w;

    glm::mat4 gameOrtho = glm::ortho(0.f, vpW, 0.f, vpH);
    uiRenderer.render(
            gameUiQue,
            gameOrtho,
            assets.getShader("glyph"),
            assets.getShader("rect"),
            {viewport.x, viewport.y},
            viewport);

    glDisable(GL_SCISSOR_TEST);

    // Editor pass: whole window.
    glViewport(0, 0, app.width(), app.height());

    glm::mat4 windowOrtho = glm::ortho(
            0.0f, static_cast<float>(app.width()),
            0.0f, static_cast<float>(app.height()));

    uiRenderer.render(
            editorUiQue,
            windowOrtho,
            assets.getShader("glyph"),
            assets.getShader("rect"));
}

void Engine::beginFrame() {
    app.getDeltaTime();
    fpsTimer += app.deltaTime;
    fpsFrames++;
    if (fpsTimer >= 0.5f) {
        fps = fpsFrames / fpsTimer;
        fpsFrames = 0;
        fpsTimer = 0.0f;
    }

    // Backdrop behind the game viewport; the game pass clears its own rect.
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


    char title[128];
    snprintf(title, sizeof(title), "Game | FPS: %.1f", fps);
    SDL_SetWindowTitle(app.window, title);
}

void Engine::endFrame() {
    PROFILE_SCOPE("Engine::endFrame");
    SDL_GL_SwapWindow(app.window);
}

void Engine::getInput(SDL_Event &event) {
    input.beginFrame();

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                app.running = false;
                break;

            case SDL_KEYDOWN:
                if (editorUi.hasFocus())
                    editorUi.dispatchKeyInput(event.key.keysym.scancode);
                else
                    gameUi.dispatchKeyInput(event.key.keysym.scancode);
                input.setKey(event.key.keysym.scancode, true);
                break;

            case SDL_KEYUP:
                input.setKey(event.key.keysym.scancode, false);
                break;

            case SDL_TEXTINPUT:
                if (editorUi.hasFocus())
                    editorUi.dispatchTextInput(event.text.text);
                else
                    gameUi.dispatchTextInput(event.text.text);
                break;

            case SDL_MOUSEMOTION:
                input.setMousePosition(
                    event.motion.x,
                    event.motion.y
                );

                input.addMouseDelta(
                    event.motion.xrel,
                    event.motion.yrel
                );
                break;

            case SDL_MOUSEBUTTONDOWN:
                input.setMouseButton(event.button.button, true);
                break;

            case SDL_MOUSEBUTTONUP:
                input.setMouseButton(event.button.button, false);
                break;

            case SDL_MOUSEWHEEL:
                input.addMouseWheel(static_cast<float>(event.wheel.y));
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    app.updateViewport();
                break;
        }
    }
}

void Engine::setupKeyBindings() {
    input.bind(Action::MoveForward, SDL_SCANCODE_W);
    input.bind(Action::MoveBackward, SDL_SCANCODE_S);
    input.bind(Action::MoveLeft, SDL_SCANCODE_A);
    input.bind(Action::MoveRight, SDL_SCANCODE_D);
    input.bind(Action::Jump, SDL_SCANCODE_SPACE);
    input.bind(Action::ToggleScreen, SDL_SCANCODE_F11);
    input.bind(Action::Pause, SDL_SCANCODE_ESCAPE);
    input.bind(Action::ToggleEditor, SDL_SCANCODE_F3);
    input.bind(Action::TogglePlay, SDL_SCANCODE_F5);
    input.bind(Action::GizmoMove, SDL_SCANCODE_W);
    input.bind(Action::GizmoRotate, SDL_SCANCODE_E);
    input.bind(Action::GizmoScale, SDL_SCANCODE_R);
    input.bind(Action::FrameSelection, SDL_SCANCODE_F);

    input.bind(MouseAction::Left, SDL_BUTTON_LEFT);
    input.bind(MouseAction::Middle, SDL_BUTTON_MIDDLE);
    input.bind(MouseAction::Right, SDL_BUTTON_RIGHT);
}

void Engine::loadAssets() {
    assets.init(&meshRegistry);

    assets.loadShaders({
        {"scene", "shaders/scene.v.glsl", "shaders/scene.f.glsl"},
        {"textured_mat", "shaders/textured_mat.v.glsl", "shaders/textured_mat.f.glsl"},
        {"debug", "shaders/debug_shader.v.glsl", "shaders/debug_shader.f.glsl"},
        {"glyph", "shaders/glyph.v.glsl", "shaders/glyph.f.glsl"},
        {"rect", "shaders/rect.v.glsl", "shaders/rect.f.glsl"},
    });

	using Clock = std::chrono::steady_clock;
	using Second = std::chrono::duration<double, std::ratio<1> >;
	std::chrono::time_point<Clock> m_beg = Clock::now();

    assets.loadModels({
        {"backpack", "assets/backpack/backpack.obj", true},
        {"car", "assets/car/car.obj", false},
        {"ar", "assets/ar/AssaultRifle_01.obj", false},
    });
    assets.loadFont("InterVariable", "assets/fonts/Inter-4.1/InterVariable.ttf");
    assets.flushLoads();

    std::cout << "Time elapse: " << 
        std::chrono::duration_cast<Second>(Clock::now() - m_beg).count()
        << std::endl;
    // ~2 seconds
}

Camera* Engine::getSceneCamera() {
    if (!scene.isValid(activeCameraEntity))
        return nullptr;

    Behaviours& ib = scene.get<Behaviours>(activeCameraEntity);
    Camera* camera = ib.getComponent<Camera>();

    if (!camera || !camera->enabled)
        return nullptr;
    return camera;
}

Camera* Engine::getActiveCamera() {
    if (editor.isOpen() && editor.isEditing())
        return editor.editModeCamera();

    return getSceneCamera();
}

void Engine::setPaused(bool value) {
    if (paused == value)
        return;

    paused = value;
    input.mouseDeltaX = 0.f;
    input.mouseDeltaY = 0.f;

    if (paused) {
        app.getCursor();
    } else {
        app.releaseCursor();
        gameUi.onUnpause();
        editorUi.onUnpause();
    }
}
