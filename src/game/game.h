#pragma once

#include <vector>

#include "engine/engine.h"
#include "game/world.h"

#include "engine/defines.h"

typedef struct {
    glm::vec3 pos;
    glm::vec3 color;
} Light;

class Game {
public:
    Game(Engine& engine);

    void init();
    void update();
    void render(const glm::mat4& view, const glm::mat4& projection);

private:
    Engine& engine;
    World_ world;
    Light light;

    UIElementID fpsLabelId = INVALID_UI_ELEMENT;
    UIElementID pauseMenuRootId = INVALID_UI_ELEMENT;
    UIElementID pausePanelId = INVALID_UI_ELEMENT;
    UIElementID pauseButtonId = INVALID_UI_ELEMENT;
    UIElementID pauseInputId = INVALID_UI_ELEMENT;
    UIElementID blockLabelId = INVALID_UI_ELEMENT;
    UIElementID toolbarId = INVALID_UI_ELEMENT;
    UIElementID flexRowDemoId = INVALID_UI_ELEMENT;
    UIElementID flexColDemoId = INVALID_UI_ELEMENT;
    UIElementID absoluteDemoId = INVALID_UI_ELEMENT;
    UIElementID flexGrowDemoId = INVALID_UI_ELEMENT;
    UIElementID flexGrowRatioDemoId = INVALID_UI_ELEMENT;

    bool playToggled = false;

    unsigned int planeVBO, planeVAO, planeEBO;

    void setupTerrain();
    void setupPlayer();
};
