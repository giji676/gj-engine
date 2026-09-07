#include <glad/glad.h>
#include <iostream>
#include <string>

#include "game/game.h"
#include "engine/defines.h"
#include "engine/profilers/profile_scope.h"
#include "engine/scene.h"
#include "game/perlin.h"
#include "game/scripts/test.h"
#include "game/scripts/player_controller.h"
#include "game/scripts/spin_system.h"
#include "game/scripts/gravity_system.h"
#include "game/components/gravity.h"

#include "engine/engine.h"
#include "engine/asset_manager/widgets.h"
#include "engine/ui.h"

namespace {

void styleDemoButton(Button* btn) {
    btn->normal.bgColor = {0.28f, 0.28f, 0.28f, 1.f};
    btn->hoveredStyle.bgColor = {0.36f, 0.36f, 0.36f, 1.f};
    btn->normal.textColor = {0.95f, 0.95f, 0.95f, 1.f};
    btn->hoveredStyle.textColor = {0.95f, 0.95f, 0.95f, 1.f};
}

UIElementID addFlowButton(
    UI& ui,
    UIElementID parent,
    float fontSize,
    const char* text,
    Length height = Length::percent(100.f))
{
    UIElementID id = ui.button({0.f, 0.f}, fontSize, text, nullptr);
    ui.reparent(id, parent);
    ui.get(id).style.position = PositionMode::Relative;
    if (!height.isAuto())
        ui.get(id).style.height = height;
    styleDemoButton(dynamic_cast<Button*>(ui.get(id).widget.get()));
    return id;
}

void setPanelBackground(UI& ui, UIElementID id, glm::vec4 color,
                        glm::vec4 cornerRadii = {0.f, 0.f, 0.f, 0.f}) {
    auto& bg = ui.get(id).addWidget<Rect>();
    bg.color = color;
    bg.cornerRadii = cornerRadii;
}

} // namespace

Terrain generateTerrain(int width, int height, float scale, float heightScale);

Game::Game(Engine& engine)
    : engine(engine)
{}

void Game::init() {
    Scene& scene = engine.scene;
    UI& ui = engine.gameUi;
    setupTerrain();
    setupPlayer();

    light.pos = glm::vec3(0.f, 3.f, 0.f);
    light.color = glm::vec3(1.f);

    Shader& sceneShader = engine.assets.getShader("scene");

    sceneShader.use();
    sceneShader.setVec3("lightPos", light.pos);
    sceneShader.setVec3("lightColor", light.color);

    scene.registerComponent<Gravity>();
    scene.registerSystem(std::make_unique<SpinSystem>());
    scene.registerSystem(std::make_unique<GravitySystem>());

    Entity e = scene.create();
    Transform& t = scene.get<Transform>(e);
    t.position.y = 2.f;
    t.scale = {0.001f, 0.001f, 0.001f};
    Object& o = scene.add<Object>(e);
    o.model = &engine.assets.getModel("car");
    o.name = "ECS ENTITY";
    o.debug = true;
    scene.addTag(e, scene.tagRegistry.intern("spin"));
    Gravity& gravity = scene.add<Gravity>(e);
    gravity.acceleration = {0.f, -0.2f, 0.f};

    Shader& texturedMatShader = engine.assets.getShader("textured_mat");
    texturedMatShader.use();
    texturedMatShader.setVec3("lightPos", light.pos);
    texturedMatShader.setVec3("lightColor", light.color);

    // --- Absolute positioning (always visible) ---
    fpsLabelId = ui.label(
            {0.f, 0.f},
            {0.f, 48.f},
            {1.f, 0.f, 0.f, 1.f},
            "hello");
    ui.get(fpsLabelId).style.inset.left = Length::px(50.f);
    ui.get(fpsLabelId).style.inset.top = Length::px(12.f);

    auto crosshairId = ui.rect(
            {0.f, 0.f},
            {10.f, 10.f},
            {1.f, 1.f, 1.f, 1.f},
            {5.f, 5.f, 5.f, 5.f});
    ui.get(crosshairId).transform.anchor = {0.5f, 0.5f};
    ui.get(crosshairId).style.inset.left = Length::percent(50.f);
    ui.get(crosshairId).style.inset.top = Length::percent(50.f);

    constexpr float demoFont = 18.f;
    constexpr float toolbarH = 40.f;

    // All pause-only chrome hangs off this root so visibility is one toggle.
    pauseMenuRootId = ui.createElement();
    ui.get(pauseMenuRootId).visible = false;
    ui.get(pauseMenuRootId).style.inset.left = Length::px(0.f);
    ui.get(pauseMenuRootId).style.inset.right = Length::px(0.f);
    ui.get(pauseMenuRootId).style.inset.top = Length::px(0.f);
    ui.get(pauseMenuRootId).style.inset.bottom = Length::px(0.f);

    // --- Flex row + justify-content: space-between (toolbar, paused) ---
    toolbarId = ui.createElement();
    ui.reparent(toolbarId, pauseMenuRootId);
    ui.get(toolbarId).style.inset.left = Length::px(0.f);
    ui.get(toolbarId).style.inset.right = Length::px(0.f);
    ui.get(toolbarId).style.inset.top = Length::px(0.f);
    ui.get(toolbarId).style.height = Length::px(toolbarH);
    ui.get(toolbarId).style.display = Display::Flex;
    ui.get(toolbarId).style.flexDirection = FlexDirection::Row;
    ui.get(toolbarId).style.justifyContent = JustifyContent::SpaceBetween;
    ui.get(toolbarId).style.gap = Length::px(4.f);
    ui.get(toolbarId).style.padding.left = Length::px(8.f);
    ui.get(toolbarId).style.padding.right = Length::px(8.f);
    ui.get(toolbarId).style.padding.top = Length::px(4.f);
    ui.get(toolbarId).style.padding.bottom = Length::px(4.f);
    setPanelBackground(ui, toolbarId, {0.18f, 0.18f, 0.18f, 1.f});

    {
        UIElementID id = addFlowButton(ui, toolbarId, demoFont, "New");
        dynamic_cast<Button*>(ui.get(id).widget.get())->onClick = []() {
            std::cout << "Toolbar: New\n";
        };
    }
    {
        UIElementID id = addFlowButton(ui, toolbarId, demoFont, "Play");
        dynamic_cast<Button*>(ui.get(id).widget.get())->onClick = [this]() {
            playToggled = !playToggled;
            std::cout << "Toolbar: Play " << (playToggled ? "on" : "off") << "\n";
        };
    }
    addFlowButton(ui, toolbarId, demoFont, "Save");

    // --- Block layout: vertical stack + gap + padding (paused) ---
    pausePanelId = ui.createElement();
    ui.reparent(pausePanelId, pauseMenuRootId);
    ui.get(pausePanelId).style.inset.left = Length::percent(3.f);
    ui.get(pausePanelId).style.inset.top = Length::px(toolbarH + 8.f);
    ui.get(pausePanelId).style.width = Length::percent(42.f);
    ui.get(pausePanelId).style.height = Length::percent(70.f);
    ui.get(pausePanelId).style.display = Display::Block;
    ui.get(pausePanelId).style.overflow = Overflow::Scroll;
    ui.get(pausePanelId).style.justifyContent = JustifyContent::Start;
    ui.get(pausePanelId).style.gap = Length::px(12.f);
    ui.get(pausePanelId).style.padding.left = Length::px(16.f);
    ui.get(pausePanelId).style.padding.right = Length::px(16.f);
    ui.get(pausePanelId).style.padding.top = Length::px(16.f);
    ui.get(pausePanelId).style.padding.bottom = Length::px(16.f);
    setPanelBackground(ui, pausePanelId, {0.12f, 0.12f, 0.12f, 0.92f},
                       {8.f, 8.f, 8.f, 8.f});

    blockLabelId = ui.label(
            {0.f, 0.f},
            {0.f, demoFont},
            {0.7f, 0.7f, 0.7f, 1.f},
            "Block: scroll + gap");
    ui.reparent(blockLabelId, pausePanelId);
    ui.get(blockLabelId).style.position = PositionMode::Relative;
    ui.get(blockLabelId).style.height = Length::px(24.f);

    pauseButtonId = ui.button(
            {0.f, 0.f},
            64.f,
            "abcdefghijklmnopq ABCDEFGHIJKLMNOPQ",
            nullptr);
    ui.reparent(pauseButtonId, pausePanelId);
    ui.get(pauseButtonId).style.position = PositionMode::Relative;
    ui.get(pauseButtonId).style.width = Length::percent(90.f);
    ui.get(pauseButtonId).style.height = Length::percent(35.f);
    ui.get(pauseButtonId).style.textOverflow = TextOverflow::Wrap;

    auto* btn = dynamic_cast<Button*>(ui.get(pauseButtonId).widget.get());
    btn->normal.bgColor = {1, 1, 1, 1};
    btn->hoveredStyle.bgColor = {0, 1, 0, 0.2};
    btn->normal.borderColor = {1, 0, 0, 1};
    btn->normal.borderWidth = 10.f;
    btn->cornerRadii = {10.f, 10.f, 10.f, 10.f};
    btn->onClick = []() { std::cout << "Button clicked\n"; };

    pauseInputId = ui.createElement();
    ui.get(pauseInputId).transform.fontSize = 32.f;
    ui.get(pauseInputId).style.position = PositionMode::Relative;
    ui.get(pauseInputId).style.width = Length::percent(100.f);
    ui.get(pauseInputId).style.height = Length::px(64.f);
    ui.reparent(pauseInputId, pausePanelId);

    auto& field = ui.get(pauseInputId).addWidget<InputField>();
    field.selfId = pauseInputId;
    field.font = &engine.assets.getFont("InterVariable");
    field.placeholder = "Enter text...";
    field.normal.bgColor = {1, 1, 1, 1};
    field.normal.textColor = {0, 0, 0, 1};
    field.normal.borderColor = {0.7f, 0.7f, 0.7f, 1};
    field.normal.borderWidth = 2.f;
    field.focusedStyle.borderColor = {0.2f, 0.5f, 1.f, 1};
    field.focusedStyle.borderWidth = 2.f;
    field.cornerRadii = {6.f, 6.f, 6.f, 6.f};

    for (int i = 1; i <= 15; ++i) {
        addFlowButton(
            ui,
            pausePanelId,
            demoFont,
            ("Scroll item " + std::to_string(i)).c_str(),
            Length::automatic());
    }

    // --- Flex row + justify-content: center (paused) ---
    flexRowDemoId = ui.createElement();
    ui.reparent(flexRowDemoId, pauseMenuRootId);
    ui.get(flexRowDemoId).style.inset.right = Length::percent(3.f);
    ui.get(flexRowDemoId).style.inset.top = Length::px(toolbarH + 8.f);
    ui.get(flexRowDemoId).style.width = Length::percent(48.f);
    ui.get(flexRowDemoId).style.height = Length::px(80.f);
    ui.get(flexRowDemoId).style.display = Display::Flex;
    ui.get(flexRowDemoId).style.flexDirection = FlexDirection::Row;
    ui.get(flexRowDemoId).style.justifyContent = JustifyContent::Center;
    ui.get(flexRowDemoId).style.alignItems = AlignItems::Center;
    ui.get(flexRowDemoId).style.gap = Length::px(8.f);
    ui.get(flexRowDemoId).style.padding.left = Length::px(12.f);
    ui.get(flexRowDemoId).style.padding.right = Length::px(12.f);
    ui.get(flexRowDemoId).style.padding.top = Length::px(8.f);
    ui.get(flexRowDemoId).style.padding.bottom = Length::px(8.f);
    setPanelBackground(ui, flexRowDemoId, {0.15f, 0.8f, 0.15f, 0.9f});

    {
        UIElementID labelId = ui.label(
                {0.f, 0.f}, {0.f, demoFont}, {0.6f, 0.8f, 0.6f, 1.f},
                "Row: center / center");
        ui.reparent(labelId, flexRowDemoId);
        ui.get(labelId).style.position = PositionMode::Relative;
    }
    addFlowButton(ui, flexRowDemoId, demoFont, "A", Length::automatic());
    addFlowButton(ui, flexRowDemoId, demoFont, "B", Length::automatic());
    addFlowButton(ui, flexRowDemoId, demoFont, "C", Length::automatic());

    // --- Flex column + justify-content: end (paused) ---
    flexColDemoId = ui.createElement();
    ui.reparent(flexColDemoId, pauseMenuRootId);
    ui.get(flexColDemoId).style.inset.right = Length::percent(3.f);
    ui.get(flexColDemoId).style.inset.top = Length::px(toolbarH + 96.f);
    ui.get(flexColDemoId).style.width = Length::percent(30.f);
    ui.get(flexColDemoId).style.height = Length::px(140.f);
    ui.get(flexColDemoId).style.display = Display::Flex;
    ui.get(flexColDemoId).style.flexDirection = FlexDirection::Column;
    ui.get(flexColDemoId).style.justifyContent = JustifyContent::End;
    ui.get(flexColDemoId).style.alignItems = AlignItems::Center;
    ui.get(flexColDemoId).style.gap = Length::px(6.f);
    ui.get(flexColDemoId).style.padding.left = Length::px(12.f);
    ui.get(flexColDemoId).style.padding.right = Length::px(12.f);
    ui.get(flexColDemoId).style.padding.top = Length::px(8.f);
    ui.get(flexColDemoId).style.padding.bottom = Length::px(8.f);
    setPanelBackground(ui, flexColDemoId, {0.2f, 0.15f, 0.25f, 0.9f});

    {
        UIElementID labelId = ui.label(
                {0.f, 0.f}, {0.f, demoFont}, {0.8f, 0.6f, 0.9f, 1.f},
                "Col: end / center");
        ui.reparent(labelId, flexColDemoId);
        ui.get(labelId).style.position = PositionMode::Relative;
        ui.get(labelId).style.height = Length::px(24.f);
    }
    addFlowButton(ui, flexColDemoId, demoFont, "Top");
    addFlowButton(ui, flexColDemoId, demoFont, "Mid");
    addFlowButton(ui, flexColDemoId, demoFont, "Bot");

    // --- Nested absolute positioning inside a flow container (paused) ---
    absoluteDemoId = ui.createElement();
    ui.reparent(absoluteDemoId, pauseMenuRootId);
    ui.get(absoluteDemoId).style.inset.right = Length::percent(3.f);
    ui.get(absoluteDemoId).style.inset.bottom = Length::percent(5.f);
    ui.get(absoluteDemoId).style.width = Length::percent(48.f);
    ui.get(absoluteDemoId).style.height = Length::px(100.f);
    ui.get(absoluteDemoId).style.display = Display::Block;
    ui.get(absoluteDemoId).style.padding.left = Length::px(12.f);
    ui.get(absoluteDemoId).style.padding.right = Length::px(12.f);
    ui.get(absoluteDemoId).style.padding.top = Length::px(12.f);
    ui.get(absoluteDemoId).style.padding.bottom = Length::px(12.f);
    setPanelBackground(ui, absoluteDemoId, {0.14f, 0.14f, 0.22f, 0.9f});

    {
        UIElementID labelId = ui.label(
                {0.f, 0.f}, {0.f, demoFont}, {0.65f, 0.65f, 0.85f, 1.f},
                "Absolute inset + anchor");
        ui.reparent(labelId, absoluteDemoId);
        ui.get(labelId).style.position = PositionMode::Relative;
        ui.get(labelId).style.height = Length::px(24.f);
    }

    UIElementID badgeId = ui.rect(
            {0.f, 0.f}, {24.f, 24.f}, {1.f, 0.4f, 0.2f, 1.f}, {4.f, 4.f, 4.f, 4.f});
    ui.reparent(badgeId, absoluteDemoId);
    ui.get(badgeId).transform.anchor = {1.f, 1.f};
    ui.get(badgeId).style.inset.right = Length::px(0.f);
    ui.get(badgeId).style.inset.bottom = Length::px(0.f);

    // --- Flex row + flex-grow: 1 (paused) ---
    flexGrowDemoId = ui.createElement();
    ui.reparent(flexGrowDemoId, pauseMenuRootId);
    ui.get(flexGrowDemoId).style.inset.left = Length::percent(3.f);
    ui.get(flexGrowDemoId).style.inset.bottom = Length::percent(5.f);
    ui.get(flexGrowDemoId).style.width = Length::percent(42.f);
    ui.get(flexGrowDemoId).style.height = Length::px(48.f);
    ui.get(flexGrowDemoId).style.display = Display::Flex;
    ui.get(flexGrowDemoId).style.flexDirection = FlexDirection::Row;
    ui.get(flexGrowDemoId).style.alignItems = AlignItems::Stretch;
    ui.get(flexGrowDemoId).style.gap = Length::px(8.f);
    ui.get(flexGrowDemoId).style.padding.left = Length::px(12.f);
    ui.get(flexGrowDemoId).style.padding.right = Length::px(12.f);
    ui.get(flexGrowDemoId).style.padding.top = Length::px(8.f);
    ui.get(flexGrowDemoId).style.padding.bottom = Length::px(8.f);
    setPanelBackground(ui, flexGrowDemoId, {0.16f, 0.16f, 0.2f, 0.92f});

    {
        UIElementID labelId = ui.label(
                {0.f, 0.f}, {0.f, demoFont}, {0.75f, 0.75f, 0.8f, 1.f},
                "Filter:");
        ui.reparent(labelId, flexGrowDemoId);
        ui.get(labelId).style.position = PositionMode::Relative;
        ui.get(labelId).style.height = Length::percent(100.f);
    }

    {
        UIElementID inputId = ui.createElement();
        ui.reparent(inputId, flexGrowDemoId);
        ui.get(inputId).transform.fontSize = 20.f;
        ui.get(inputId).style.position = PositionMode::Relative;
        ui.get(inputId).style.flexGrow = 1.f;
        ui.get(inputId).style.flexBasis = Length::px(0.f);
        ui.get(inputId).style.height = Length::percent(100.f);

        auto& filterField = ui.get(inputId).addWidget<InputField>();
        filterField.selfId = inputId;
        filterField.font = &engine.assets.getFont("InterVariable");
        filterField.placeholder = "flex: 1";
        filterField.normal.bgColor = {1, 1, 1, 1};
        filterField.normal.textColor = {0, 0, 0, 1};
        filterField.normal.borderColor = {0.7f, 0.7f, 0.7f, 1};
        filterField.normal.borderWidth = 2.f;
        filterField.cornerRadii = {4.f, 4.f, 4.f, 4.f};
    }

    addFlowButton(ui, flexGrowDemoId, demoFont, "Go");

    // --- Flex row: grow 3 / grow 2 / auto (paused) ---
    flexGrowRatioDemoId = ui.createElement();
    ui.reparent(flexGrowRatioDemoId, pauseMenuRootId);
    ui.get(flexGrowRatioDemoId).style.inset.left = Length::percent(3.f);
    ui.get(flexGrowRatioDemoId).style.inset.bottom = Length::percent(10.f);
    ui.get(flexGrowRatioDemoId).style.width = Length::percent(42.f);
    ui.get(flexGrowRatioDemoId).style.height = Length::px(40.f);
    ui.get(flexGrowRatioDemoId).style.display = Display::Flex;
    ui.get(flexGrowRatioDemoId).style.flexDirection = FlexDirection::Row;
    ui.get(flexGrowRatioDemoId).style.alignItems = AlignItems::Stretch;
    ui.get(flexGrowRatioDemoId).style.gap = Length::px(4.f);
    ui.get(flexGrowRatioDemoId).style.padding.left = Length::px(8.f);
    ui.get(flexGrowRatioDemoId).style.padding.right = Length::px(8.f);
    ui.get(flexGrowRatioDemoId).style.padding.top = Length::px(4.f);
    ui.get(flexGrowRatioDemoId).style.padding.bottom = Length::px(4.f);
    setPanelBackground(ui, flexGrowRatioDemoId, {0.2f, 0.18f, 0.14f, 0.92f});

    {
        UIElementID id = addFlowButton(ui, flexGrowRatioDemoId, demoFont, "grow 3");
        ui.get(id).style.flexGrow = 3.f;
        ui.get(id).style.flexBasis = Length::px(0.f);
        dynamic_cast<Button*>(ui.get(id).widget.get())->normal.bgColor =
                {0.55f, 0.22f, 0.22f, 1.f};
    }
    {
        UIElementID id = addFlowButton(ui, flexGrowRatioDemoId, demoFont, "grow 2");
        ui.get(id).style.flexGrow = 2.f;
        ui.get(id).style.flexBasis = Length::px(0.f);
        dynamic_cast<Button*>(ui.get(id).widget.get())->normal.bgColor =
                {0.22f, 0.45f, 0.22f, 1.f};
    }
    {
        UIElementID id = addFlowButton(ui, flexGrowRatioDemoId, demoFont, "grow 1");
        ui.get(id).style.flexGrow = 1.f;
        ui.get(id).style.flexBasis = Length::px(0.f);
        dynamic_cast<Button*>(ui.get(id).widget.get())->normal.bgColor =
                {0.22f, 0.28f, 0.55f, 1.f};
    }
}

void Game::setupTerrain() {
    world.width = 500;
    world.height = 500;
    world.scale = 0.05f;
    world.terrain = generateTerrain(world.width, world.height, world.scale, 0.4f);

    glGenVertexArrays(1, &planeVAO);
    glGenBuffers(1, &planeVBO);
    glGenBuffers(1, &planeEBO);

    glBindVertexArray(planeVAO);

    // vertices
    glBindBuffer(GL_ARRAY_BUFFER, planeVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 world.terrain.vertices.size() * sizeof(float),
                 world.terrain.vertices.data(),
                 GL_DYNAMIC_DRAW);

    // indices
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, planeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 world.terrain.indices.size() * sizeof(unsigned int),
                 world.terrain.indices.data(),
                 GL_STATIC_DRAW);

    // position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void Game::setupPlayer() {
    Scene& scene = engine.scene;
    Entity e = scene.create();
    scene.add<Object>(e).name = "Player";
    scene.addTag(e, scene.tagRegistry.intern("player"));
    Behaviours& ib = scene.add<Behaviours>(e);
    ib.entity = e;
    ib.addScript<PlayerController>(&this->world);
    ib.addComponent<Camera>();
    engine.activeCameraEntity = e;
}

Terrain generateTerrain(int width, int height, float scale, float heightScale) {
    Terrain t;

    float halfW = (width - 1) * scale * 0.5f;
    float halfH = (height - 1) * scale * 0.5f;

    // store heights temporarily for normal calculation
    std::vector<float> heights(width * height);

    auto idx = [&](int x, int z) {
        return z * width + x;
    };

    // 1) generate heightmap first
    for (int z = 0; z < height; z++) {
        for (int x = 0; x < width; x++) {

            float wx = x * scale - halfW;
            float wz = z * scale - halfH;

            heights[idx(x, z)] = noise2D(wx, wz) * heightScale;
        }
    }

    // clamp helper
    auto h = [&](int x, int z) {
        x = std::max(0, std::min(x, width - 1));
        z = std::max(0, std::min(z, height - 1));
        return heights[idx(x, z)];
    };

    // 2) build vertices + normals
    for (int z = 0; z < height; z++) {
        for (int x = 0; x < width; x++) {

            float wx = x * scale - halfW;
            float wz = z * scale - halfH;
            float y  = heights[idx(x, z)];

            // central difference normal
            float hl = h(x - 1, z);
            float hr = h(x + 1, z);
            float hd = h(x, z - 1);
            float hu = h(x, z + 1);

            glm::vec3 n;
            n.x = hl - hr;
            n.y = 2.0f;
            n.z = hd - hu;
            n = glm::normalize(n);

            t.vertices.push_back(wx);
            t.vertices.push_back(y);
            t.vertices.push_back(wz);

            t.vertices.push_back(n.x);
            t.vertices.push_back(n.y);
            t.vertices.push_back(n.z);
        }
    }

    // 3) indices unchanged
    for (int z = 0; z < height - 1; z++) {
        for (int x = 0; x < width - 1; x++) {

            int i0 = z * width + x;
            int i1 = i0 + 1;
            int i2 = (z + 1) * width + x;
            int i3 = i2 + 1;

            t.indices.push_back(i0);
            t.indices.push_back(i2);
            t.indices.push_back(i1);

            t.indices.push_back(i1);
            t.indices.push_back(i2);
            t.indices.push_back(i3);
        }
    }

    return t;
}

void Game::update() {
    PROFILE_SCOPE("Game::update");

    Input& input = engine.input;

    auto setPauseMenuVisible = [&](bool visible) {
        engine.gameUi.get(pauseMenuRootId).visible = visible;
    };

    // -------------------------
    // 1. ALWAYS RUN (control input)
    // -------------------------
    if (input.pressed(Action::Pause)) {
        if (engine.editor.isEditing()) {
            // Leave editor-tool control; stay paused so the game pause UI can show.
            engine.editor.exitEditMode();
        } else {
            engine.setPaused(!engine.isPaused());
        }
    }

    // Editor Edit mode owns the paused session without the in-game pause chrome.
    setPauseMenuVisible(engine.isPaused() && !engine.editor.isEditing());

    if (input.pressed(Action::ToggleScreen))
        engine.app.toggleWindow();

    // -------------------------
    // 3. UI (always runs)
    // -------------------------
    UIElement& e = engine.gameUi.get(fpsLabelId);
    if (auto* lbl = dynamic_cast<Label*>(e.widget.get())) {
        lbl->text = "FPS: " + std::to_string(engine.fps);
    }
}

void Game::render(const glm::mat4& view, const glm::mat4& projection) {
    PROFILE_SCOPE("Game::render");

    glm::mat4 model = glm::mat4(1.0f);

    Shader& sceneShader = engine.assets.getShader("scene");
    sceneShader.use();
    sceneShader.setMat4("model", model);
    sceneShader.setMat4("view", view);
    sceneShader.setMat4("projection", projection);

    glBindVertexArray(planeVAO);
    glDrawElements(GL_TRIANGLES,
                   world.terrain.indices.size(),
                   GL_UNSIGNED_INT,
                   0);
    glBindVertexArray(0);
}
