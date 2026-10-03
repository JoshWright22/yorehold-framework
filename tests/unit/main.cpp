#include "Checks.h"
#include "ImageTests.h"
#include "DialogueTests.h"

#include <yorehold/framework/animation/SpriteSheet.h>
#include <yorehold/framework/animation/Tween.h>
#include <yorehold/framework/assets/FileSystem.h>
#include <yorehold/framework/assets/Skin.h>
#include <yorehold/framework/audio/Audio.h>
#include <yorehold/framework/debug/Profiler.h>
#include <yorehold/framework/editor/History.h>
#include <yorehold/framework/graphics/Atlas.h>
#include <yorehold/framework/graphics/Lighting.h>
#include <yorehold/framework/input/Input.h>
#include <yorehold/framework/input/ControlScheme.h>
#include <yorehold/framework/map/CameraControls.h>
#include <yorehold/framework/map/FogOfWar.h>
#include <yorehold/framework/map/Navigation.h>
#include <yorehold/framework/map/Objects.h>
#include <yorehold/framework/map/Regions.h>
#include <yorehold/framework/map/Templates.h>
#include <yorehold/framework/map/Tokens.h>
#include <yorehold/framework/net/Session.h>
#include <yorehold/framework/Scenes.h>
#include <yorehold/framework/graphics/Particles.h>
#include <yorehold/framework/rpg/Character.h>
#include <yorehold/framework/rpg/Combat.h>
#include <yorehold/framework/save/SaveFile.h>
#include <yorehold/framework/text/RichText.h>
#include <yorehold/framework/text/Strings.h>
#include <yorehold/framework/ui/Ui.h>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace
{
using regression::near;

void tweens()
{
    for (int i = 0; i < 9; ++i)
    {
        CHECK(near(yh::ease(static_cast<yh::Ease>(i), 0), 0));
        CHECK(near(yh::ease(static_cast<yh::Ease>(i), 1), 1));
    }
    CHECK(yh::ease(yh::Ease::OutBack, 0.7f) > 1);
    CHECK(yh::easeFromName("outElastic") == yh::Ease::OutElastic);
    float value = 0;
    yh::Tweens t;
    t.to(&value, 10, 1, yh::Ease::Linear, 0.5);
    value = 2;
    t.update(0.25);
    CHECK(value == 2);
    t.update(0.5);
    CHECK(near(value, 4)); // only 0.25 seconds remains after the delay
    t.to(&value, 8, 0.5, yh::Ease::Linear);
    CHECK(t.count() == 1);
    t.update(0.5);
    CHECK(value == 8 && !t.running(&value));
    int callbacks = 0;
    t.to(&value, 12, 0, yh::Ease::Linear, 0, [&] { ++callbacks; t.to(&value, 16, 1); });
    t.update(0);
    CHECK(value == 12 && callbacks == 1 && t.running(&value));
    t.cancel(&value);
    t.update(10);
    CHECK(value == 12);
    bool rejected = false;
    try { t.to(nullptr, 0, 1); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}

void particles()
{
    std::string error;
    CHECK(!yh::ParticleEffect::fromJson("{\"life\":[0,1]}", &error) && !error.empty());
    CHECK(!yh::ParticleEffect::fromJson("{\"size\":[9,1]}"));
    CHECK(!yh::ParticleEffect::fromJson("{\"startColor\":[256,0,0,255]}"));
    CHECK(!yh::ParticleEffect::fromJson("{\"blend\":\"bad\"}"));
    const auto loaded = yh::ParticleEffect::fromJson("{\"name\":\"spark\",\"life\":2,\"rate\":10}", &error);
    CHECK(loaded && error.empty() && loaded->life.min == 2);
    yh::ParticleEffect effect;
    effect.rate = 10;
    effect.burst = 0;
    effect.duration = 0.5f;
    effect.life = {10, 10};
    effect.speed = {10, 10};
    effect.angle = {0, 0};
    yh::ParticleSystem a(100), b(100);
    a.start(effect, {}, 123);
    b.start(effect, {}, 123);
    a.update(0.5);
    b.update(0.25); b.update(0.25);
    CHECK(a.liveParticles() == 5 && a.liveEmitters() == 0 && b.liveParticles() == 5);
    for (size_t i = 0; i < 5; ++i) CHECK(near(a.position(i).x, b.position(i).x));
    a.update(11);
    CHECK(a.liveParticles() == 0);
    yh::ParticleSystem capped(3);
    capped.burst(effect, {}, 100);
    CHECK(capped.liveParticles() == 3);
    capped.clear();
    CHECK(capped.liveParticles() == 0 && capped.liveEmitters() == 0);
    effect.rate = 0; effect.burst = 4; effect.duration = -1;
    effect.spawnRadius = 20;
    yh::ParticleSystem same1, same2;
    const auto id = same1.start(effect, {}, 66);
    same2.start(effect, {}, 66);
    same2.start(effect, {100, 100}, 888);
    for (size_t i = 0; i < 4; ++i) CHECK(same1.position(i) == same2.position(i));
    same1.stop(id); same1.update(0.1);
    CHECK(same1.liveEmitters() == 0 && same1.liveParticles() == 4);
    effect.rate = 10; effect.burst = 0; effect.life = {0.3f, 0.8f}; effect.speed = {10, 50};
    yh::ParticleSystem coarse, fine;
    coarse.start(effect, {}, 912); fine.start(effect, {}, 912);
    coarse.update(1); fine.update(0.5); fine.update(0.5);
    CHECK(coarse.liveParticles() == fine.liveParticles());
    std::vector<float> cx, fx;
    for (size_t i = 0; i < coarse.liveParticles(); ++i) cx.push_back(coarse.position(i).x);
    for (size_t i = 0; i < fine.liveParticles(); ++i) fx.push_back(fine.position(i).x);
    std::sort(cx.begin(), cx.end()); std::sort(fx.begin(), fx.end());
    for (size_t i = 0; i < std::min(cx.size(), fx.size()); ++i) CHECK(near(cx[i], fx[i], 0.001f));
}

void visibilityAndFog()
{
    const std::array<yh::Wall, 1> walls{{{{5, -10}, {5, 10}}}};
    CHECK(!yh::lineOfSight({0, 0}, {10, 0}, walls));
    CHECK(yh::lineOfSight({0, 0}, {4, 0}, walls));
    CHECK(yh::lineOfSight({0, 0}, {0, 0}, walls));
    const std::array<yh::Wall, 1> collinear{{{{2, 0}, {4, 0}}}};
    CHECK(!yh::lineOfSight({0, 0}, {10, 0}, collinear));
    yh::Visibility visibility;
    const auto polygon = visibility.compute({0, 0}, 10, walls);
    CHECK(polygon.size() >= 96);
    for (yh::Vec2 p : polygon)
        if (std::abs(p.y) < 9.9f) CHECK(p.x <= 5.001f);
    yh::FogOfWar fog(16, 16, 1);
    const std::array<yh::Vision, 1> observers{{{{2.5f, 2.5f}, 10}}};
    fog.update(0, 0, observers, walls);
    CHECK(fog.state(0, 0, {2, 2}) == yh::FogState::Visible);
    CHECK(fog.state(0, 0, {7, 2}) == yh::FogState::Unexplored);
    CHECK(fog.state(1, 0, {2, 2}) == yh::FogState::Unexplored);
    CHECK(fog.state(0, 1, {2, 2}) == yh::FogState::Unexplored);
    fog.update(0, 0, {}, walls);
    CHECK(fog.state(0, 0, {2, 2}) == yh::FogState::Explored);
    auto restored = yh::FogOfWar::fromJson(fog.toJson());
    CHECK(restored && restored->state(0, 0, {2, 2}) == yh::FogState::Explored);
    fog.reset(0);
    CHECK(fog.state(0, 0, {2, 2}) == yh::FogState::Unexplored);
    CHECK(!yh::FogOfWar::fromJson("{\"width\":0,\"height\":1,\"cellSize\":1}"));
}

void mapsAndPaths()
{
    yh::TileMap map(7000, 7000, 64);
    const int ground = map.addLayer("ground", 0, 1);
    CHECK(map.storedChunks() == 0 && map.tile(ground, 6999, 6999) == 1);
    map.setTile(ground, 6000, 6000, 0);
    CHECK(map.storedChunks() == 1 && map.tile(ground, 6000, 6000) == 0);
    const auto authored = map.toJson();
    CHECK(authored.size() < 500);
    auto restored = yh::TileMap::fromJson(authored);
    CHECK(restored && restored->toJson() == authored);
    map.trackChanges();
    map.setTile(ground, 6000, 6000, 1);
    map.setTile(ground, 0, 0, 2);
    CHECK(map.storedChunks() == 1);
    CHECK(restored->applyChanges(map.changesJson()));
    CHECK(restored->tile(0, 6000, 6000) == 1 && restored->tile(0, 0, 0) == 2);
    CHECK(!restored->applyChanges("{\"width\":7000,\"height\":7000,\"tileSize\":64,\"changes\":[[0,0,0,3],[0,7000,0,3]]}"));
    CHECK(restored->tile(0, 0, 0) == 2); // failed deltas are atomic
    CHECK(!yh::TileMap::fromJson("{\"width\":-1,\"height\":10,\"tileSize\":1}"));
    yh::Grid square(yh::GridType::Square, 10);
    CHECK(square.cellAt({-1, -1}) == yh::Cell{-1, -1});
    CHECK(square.snap({17, 13}) == yh::Vec2{15, 15});
    std::set<std::pair<int, int>> blocked{{1, 0}, {0, 1}};
    auto passable = [&](yh::Cell c) { return c.x >= 0 && c.y >= 0 && c.x < 100 && c.y < 100 && !blocked.contains({c.x, c.y}); };
    CHECK(yh::findPath(square, {0, 0}, {2, 2}, passable).empty());
    blocked.clear();
    for (int y = 0; y < 100; ++y) if (y != 50) blocked.insert({40, y});
    yh::Navigation nav(square, 100, 100, passable, 16);
    const auto path = nav.path({2, 2}, {98, 98});
    CHECK(!path.empty() && path.front() == yh::Cell{2, 2} && path.back() == yh::Cell{98, 98});
    for (yh::Cell c : path) CHECK(passable(c));
    blocked.insert({40, 50}); nav.invalidate({40, 50});
    CHECK(nav.path({2, 2}, {98, 98}).empty());
    blocked.erase({40, 50}); nav.invalidate({40, 50});
    CHECK(!nav.path({2, 2}, {98, 98}).empty());
    yh::Grid hex(yh::GridType::Hex, 10);
    CHECK(hex.cellAt(hex.center({3, 4})) == yh::Cell{3, 4});
}

void objectsAndRegions()
{
    yh::Kit kit;
    kit.name = "Locked door";
    kit.prototype.name = "Door";
    kit.prototype.tags = {"blocksMovement", "blocksSight", "key:red", "link:gate"};
    kit.prototype.door = yh::Door{false, true};
    auto parsed = yh::Kit::fromJson(kit.toJson());
    CHECK(parsed && parsed->prototype.door->locked);
    yh::Objects objects;
    const auto door = objects.place(kit, {10, 10});
    CHECK(!objects.passable({10, 10, 1, 1}, 0) && objects.walls(0).size() == 4);
    CHECK(objects.interact(door) == yh::Interaction::Locked);
    const std::array<std::string, 1> keys{{"key:red"}};
    CHECK(objects.interact(door, keys) == yh::Interaction::Opened);
    CHECK(objects.passable({10, 10, 1, 1}, 0) && objects.walls(0).empty());
    yh::MapObject lever; lever.tags = {"lever", "link:gate"};
    const auto leverId = objects.add(lever);
    CHECK(objects.interact(leverId) == yh::Interaction::Activated && !objects.get(door)->door->open);
    yh::MapObject chest; chest.tags = {"container", "destructible"};
    chest.contents["coin"] = 5; chest.durability = yh::Durability{10, 10};
    const auto chestId = objects.add(chest);
    CHECK(objects.take(chestId, "coin", 3) == 3 && objects.take(chestId, "coin", 10) == 2);
    CHECK(objects.damage(chestId, 11) && objects.get(chestId)->destroyed);
    CHECK(objects.take(chestId, "coin", 1) == 0);
    const auto restored = yh::Objects::fromJson(objects.toJson());
    CHECK(restored && restored->toJson() == objects.toJson());
    yh::MapObject crate; crate.tags = {"throwable", "blocksMovement"};
    const auto crateId = objects.add(crate);
    CHECK(objects.throwTo(crateId, {100, 0}, 1, 40) && !objects.get(crateId)->blocksMovement());
    objects.update(0.5);
    CHECK(near(objects.get(crateId)->area.x, 50));
    auto midFlight = yh::Objects::fromJson(objects.toJson());
    CHECK(midFlight && midFlight->get(crateId)->flight);
    int landings = 0;
    midFlight->update(0.6, [&](yh::ObjectId id) { ++landings; CHECK(id == crateId); midFlight->remove(id); });
    CHECK(landings == 1 && !midFlight->get(crateId));
    yh::Regions regions;
    int loads = 0;
    auto loader = [&](std::string_view id) {
        ++loads;
        auto region = std::make_unique<yh::Region>();
        region->id = id;
        region->map = std::make_unique<yh::TileMap>(10, 10, 32.0f);
        region->map->addLayer("ground", 0, 1);
        return region;
    };
    regions.add("a", loader); regions.add("b", loader);
    regions.add("broken", [](std::string_view) { return std::unique_ptr<yh::Region>{}; });
    CHECK(regions.enter(1, "a") && regions.enter(2, "b") && regions.activeCount() == 2);
    regions.update(2);
    regions.active("a")->variables["gate"] = "opened";
    CHECK(!regions.enter(1, "broken") && regions.playerRegion(1) == "a");
    regions.leave(1);
    CHECK(!regions.active("a") && regions.activeCount() == 1);
    regions.update(5);
    CHECK(regions.enter(1, "a") && loads == 2);
    CHECK(regions.active("a")->simulatedSeconds == 2 && regions.active("a")->variables.at("gate") == "opened");
    CHECK(regions.active("b")->simulatedSeconds == 7);
    const auto save = regions.toJson();
    regions.leave(1); regions.leave(2);
    CHECK(regions.restore(save) && regions.enter(3, "a"));
    CHECK(regions.active("a")->variables.at("gate") == "opened");
    CHECK(!regions.restore(save));
    regions.active("a")->map->setTile(0, 2, 2, 3);
    const auto changes = regions.changesJson();
    regions.leave(3);
    CHECK(regions.enter(3, "a") && regions.changesJson() == changes);
    regions.leave(3);
    CHECK(regions.restoreChanges(changes) && regions.enter(3, "a"));
    CHECK(regions.active("a")->map->tile(0, 2, 2) == 3 && regions.active("a")->map->tile(0, 3, 3) == 1);
    CHECK(regions.active("a")->variables.at("gate") == "opened" && regions.active("a")->simulatedSeconds == 2);
    yh::Regions rewind;
    rewind.add("a", loader); rewind.add("b", loader);
    CHECK(rewind.enter(0, "a"));
    const auto early = rewind.toJson();
    CHECK(rewind.enter(0, "b")); rewind.update(10); rewind.leave(0);
    CHECK(rewind.restore(early) && rewind.enter(0, "b") && rewind.active("b")->simulatedSeconds == 0);
    rewind.leave(0);
    CHECK(!rewind.restore("{\"b\":{\"id\":\"b\",\"objects\":null}}"));
    CHECK(rewind.enter(0, "a") && rewind.active("a")->simulatedSeconds == 0);
    const yh::WorldPosition far{1000000000.25, 1000000000.5};
    CHECK(far.relativeTo({1000000000, 1000000000}) == yh::Vec2{0.25f, 0.5f});
}

void tokensAndParty()
{
    yh::TokenController controller;
    controller.tokens.resize(4);
    controller.tokens[0].position = {95, 5};
    controller.tokens[1].position = {5, 5};
    controller.tokens[2].position = {5, 25};
    controller.tokens[3].owner = 1;
    CHECK(controller.link(1, 0) && controller.link(2, 1));
    CHECK(!controller.link(0, 2) && !controller.link(3, 0) && !controller.link(1, 1));
    yh::Grid grid(yh::GridType::Square, 10);
    yh::Camera camera;
    yh::Input input;
    auto passable = [](yh::Cell c) { return c.x >= 0 && c.x < 20 && c.y >= 0 && c.y < 10; };
    for (int i = 0; i < 60; ++i) controller.update(input, camera, grid, passable, 0.1);
    CHECK(controller.tokens[1].position.x > 60);
    CHECK(grid.distance(grid.cellAt(controller.tokens[0].position), grid.cellAt(controller.tokens[1].position)) >= 1);
    controller.settings.inCombat = true; controller.settings.activeTurn = 0;
    const auto before = controller.tokens[1].position;
    controller.tokens[1].path = {{195, 5}};
    controller.update(input, camera, grid, passable, 1);
    CHECK(controller.tokens[1].position == before);
    controller.setFloor(0, 2);
    CHECK(controller.tokens[0].floor == 2 && controller.tokens[1].floor == 2 && controller.tokens[2].floor == 2);
    CHECK(controller.tokens[1].path.empty());
    controller.unlink(1); CHECK(!controller.follows(1));
    controller.clearLinks(); CHECK(!controller.follows(2));
}

void inputFilesAndTheme()
{
    yh::Input input;
    yh::InputMap map;
    map.bind("go", yh::Binding::keyboard(SDLK_W)); input.setMap(map);
    SDL_Event e{}; e.type = SDL_EVENT_KEY_DOWN; e.key.key = SDLK_W;
    input.handle(e); CHECK(input.down("go") && input.pressed("go"));
    input.endFrame(); CHECK(input.down("go") && !input.pressed("go"));
    e.type = SDL_EVENT_WINDOW_FOCUS_LOST; input.handle(e); CHECK(!input.down("go"));
    e = {}; e.type = SDL_EVENT_MOUSE_BUTTON_UP; e.button.button = SDL_BUTTON_LEFT;
    input.handle(e); CHECK(!input.buttonClicked(yh::MouseButton::Left));
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.x = 10; e.button.y = 10; input.handle(e);
    e.type = SDL_EVENT_MOUSE_BUTTON_UP; e.button.x = 20; input.handle(e);
    CHECK(!input.buttonClicked(yh::MouseButton::Left));
    CHECK(yh::FileSystem::normalize("textures\\./a.png") == "textures/a.png");
    CHECK(yh::FileSystem::normalize("../a").empty());
    CHECK(yh::FileSystem::normalize("a/..").empty());
    CHECK(yh::FileSystem::normalize("C:/outside").empty());
    const auto root = std::filesystem::path(YH_DEV_STATE_DIR) / "unit-files";
    std::filesystem::create_directories(root / "base");
    std::filesystem::create_directories(root / "skin");
    std::ofstream(root / "base" / "test.txt") << "base";
    std::ofstream(root / "skin" / "test.txt") << "skin";
    CHECK(yh::FileSystem::packFolder((root / "skin").string(), (root / "test.yoreskin").string()));
    yh::FileSystem files;
    CHECK(files.mountFolder((root / "base").string(), "base"));
    CHECK(files.readText("test.txt") == "base");
    CHECK(files.mountZip((root / "test.yoreskin").string(), "skin"));
    CHECK(files.readText("test.txt") == "skin");
    CHECK(!files.readText("C:/Windows/win.ini"));
    CHECK(!files.readText("../../README.md"));
    files.unmount("skin"); CHECK(files.readText("test.txt") == "base");
    CHECK(files.list("").size() == 1);
    const auto theme = yh::UiTheme::fromJson("{\"accent\":[1,2,3,255],\"textScale\":1.5}");
    CHECK(theme && theme->accent == yh::Color{1, 2, 3, 255});
    CHECK(!yh::UiTheme::fromJson("{\"accent\":[999,2,3,255]}"));
    const auto savedBindings = yh::InputMap::fromJson(input.map().toJson());
    CHECK(savedBindings && savedBindings->toJson() == input.map().toJson());
    CHECK(!yh::InputMap::fromJson("{\"bad\":[{\"type\":\"mouse\",\"button\":99}]}"));
    yh::FileSystem defaults;
    CHECK(defaults.mountFolder(YH_FRAMEWORK_ASSETS, "framework"));
    const auto skin = yh::Skin::load(defaults);
    CHECK(skin && skin->name == "Yorehold" && skin->effects.contains("embers"));
}

void audioAndScenes()
{
    yh::Audio audio;
    const auto sound = yh::Sound::tone(440, 0.02f);
    CHECK(std::abs(sound->seconds() - 0.02) < 0.0001);
    const auto id = audio.play(sound, yh::AudioBus::Effects, 1, true, 1);
    std::array<float, 2048> mixed;
    audio.mix(mixed);
    bool haveRight = false;
    for (size_t i = 0; i < mixed.size(); i += 2) { CHECK(mixed[i] == 0); haveRight |= mixed[i + 1] != 0; }
    CHECK(haveRight && audio.playing(id));
    audio.setVolume(yh::AudioBus::Effects, 0); audio.mix(mixed);
    CHECK(std::all_of(mixed.begin(), mixed.end(), [](float f) { return f == 0; }));
    audio.stop(id); CHECK(audio.voices() == 0);
    audio.play(sound); audio.mix(mixed); CHECK(audio.voices() == 0);
    CHECK(!yh::Sound::fromWav({}));
    struct Counters { int loads = 0, unloads = 0, ticks = 0; } base, overlay;
    struct Screen : yh::Game
    {
        Counters& c;
        yh::Scenes* stack;
        bool close = false;
        Screen(Counters& counters, yh::Scenes* scenes) : c(counters), stack(scenes) {}
        void load() override { ++c.loads; }
        void unload() override { ++c.unloads; }
        void update(double) override { ++c.ticks; if (close) stack->pop(); }
    };
    yh::Scenes stack;
    stack.push(std::make_unique<Screen>(base, &stack)); stack.load();
    auto top = std::make_unique<Screen>(overlay, &stack); top->close = true;
    stack.push(std::move(top), true, false); stack.update(0.1);
    CHECK(base.loads == 1 && base.ticks == 0 && overlay.loads == 1 && overlay.unloads == 1 && stack.count() == 1);
    stack.update(0.1); stack.unload(); CHECK(base.ticks == 1 && base.unloads == 1);
}

void rpg()
{
    const auto expression = yh::DiceExpression::parse("4d6kh3+2");
    CHECK(expression && expression->minimum() == 5 && expression->maximum() == 20);
    CHECK(!yh::DiceExpression::parse("2d6+"));
    CHECK(!yh::DiceExpression::parse("4d6kh5"));
    std::string excessive;
    for (int i = 0; i < 30; ++i) excessive += (i ? "+" : "") + std::string("1000d100000");
    CHECK(!yh::DiceExpression::parse(excessive));
    yh::Random a(7), b(7);
    for (int i = 0; i < 100; ++i) CHECK(yh::roll(*expression, a).total == yh::roll(*expression, b).total);
    CHECK(a.range(INT_MIN, INT_MAX) >= INT_MIN);
    const auto rules = yh::Ruleset::modern();
    CHECK(rules.abilityModifier(9) == -1);
    const auto parsedRules = yh::Ruleset::fromJson(rules.toJson());
    CHECK(parsedRules && parsedRules->toJson() == rules.toJson());
    CHECK(!yh::Ruleset::fromJson("{\"version\":\"bad\"}"));
    CHECK(!yh::Character::fromJson("{\"version\":\"bad\"}"));
    yh::Character hero = yh::makeRandomCharacter(rules, "hero", "fighter", a);
    yh::Character enemy = yh::makeRandomCharacter(rules, "enemy", "fighter", b);
    hero.tempHp = 3;
    const int health = hero.hp;
    hero.takeDamage(4); CHECK(hero.tempHp == 0 && hero.hp == health - 1);
    auto restored = yh::Character::fromJson(hero.toJson());
    CHECK(restored && restored->toJson() == hero.toJson());
    yh::Encounter empty(rules, 7); empty.start();
    CHECK(!empty.started() && !empty.canAct() && !empty.attack(0).hit && !empty.spendMovement(1));
    yh::Encounter fight(rules, 7); fight.add(hero, 0); fight.add(enemy, 1); fight.start();
    const auto movement = fight.current().budget.movementLeft;
    CHECK(!fight.spendMovement(-1) && fight.current().budget.movementLeft == movement);
    CHECK(fight.spendMovement(1) && fight.current().budget.movementLeft == movement - 1);
    CHECK(fight.dash() && !fight.canAct() && !fight.dash());
}

void savesAndHistory()
{
    yh::SaveFormat v1("test.save", 1);
    const auto old = v1.write("{\"gold\":5}");
    yh::SaveFormat v3("test.save", 3);
    v3.migrate(1, [](nlohmann::json& d) { d["coins"] = d["gold"]; d.erase("gold"); });
    std::string error;
    int saved = 0;
    CHECK(!v3.read(old, &error) && !error.empty()); // missing step 2 -> 3
    v3.migrate(2, [](nlohmann::json& d) { d["coins"] = d["coins"].get<int>() * 10; });
    const auto upgraded = v3.read(old, &error, &saved);
    CHECK(upgraded && nlohmann::json::parse(*upgraded) == nlohmann::json({{"coins", 50}}) && saved == 1);
    CHECK(v3.read(v3.write(*upgraded)) == upgraded);
    CHECK(!v1.read(v3.write("{}"), &error)); // newer saves are refused
    CHECK(!yh::SaveFormat("other", 1).read(old));
    CHECK(!v3.read("not json"));
    yh::SaveFormat strict("test.save", 2);
    strict.migrate(1, [](nlohmann::json&) { throw std::invalid_argument("corrupt"); });
    CHECK(!strict.read(old, &error) && error == "corrupt");
    const auto path = (std::filesystem::path(YH_DEV_STATE_DIR) / "unit-files" / "slot.save").string();
    std::filesystem::remove(path); std::filesystem::remove(path + ".bak");
    CHECK(v3.writeFile(path, "{\"coins\":1}") && v3.writeFile(path, "{\"coins\":2}"));
    CHECK(v3.readFile(path) == std::optional<std::string>("{\"coins\":2}"));
    std::ofstream(path, std::ios::trunc) << "{broken";
    CHECK(v3.readFile(path) == std::optional<std::string>("{\"coins\":1}")); // falls back to the backup

    yh::History history(3);
    int value = 0;
    auto set = [&](int to) { const int from = value; history.perform("set", [&value, to] { value = to; }, [&value, from] { value = from; }); };
    set(1); set(2);
    CHECK(value == 2 && history.dirty() && history.undo() && value == 1 && history.redoLabel() == "set");
    history.markSaved();
    set(5); // discards the redo branch
    CHECK(!history.canRedo() && history.dirty() && history.undo() && !history.dirty() && history.undo() && value == 0);
    CHECK(!history.undo() && history.redo() && history.redo() && value == 5);
    for (int stroke = 6; stroke < 9; ++stroke)
    {
        const int from = value;
        history.perform("paint", [&value, stroke] { value = stroke; }, [&value, from] { value = from; }, "brush");
    }
    history.breakMerge();
    CHECK(value == 8 && history.undoLabel() == "paint" && history.undo() && value == 5);
    history.beginGroup("paste");
    set(20); set(30);
    CHECK(!history.canUndo());
    history.endGroup();
    CHECK(history.undo() && value == 5 && history.redo() && value == 30);
    for (int i = 0; i < 5; ++i) set(i);
    CHECK(history.size() == 3 && history.undo() && history.undo() && history.undo() && !history.undo() && value == 1);
    bool threw = false;
    history.record("nested", [&] { try { set(99); } catch (const std::logic_error&) { threw = true; } }, [] {});
    history.undo(); history.redo();
    CHECK(threw);
}

void textEditing()
{
    std::string text = "héllo wörld";
    yh::TextEdit edit;
    edit.end(text, false);
    edit.left(text, true, true); // Ctrl+Shift+Left selects "wörld"
    CHECK(edit.selected(text) == "wörld");
    CHECK(edit.insert(text, "there", 64) && text == "héllo there" && !edit.hasSelection());
    edit.left(text, false, false);
    CHECK(edit.caret == text.size() - 1);
    edit.home(false); edit.right(text, false, false); edit.right(text, true, false);
    CHECK(edit.selected(text) == "é");
    CHECK(edit.erase(text, false, false) && text == "hllo there" && edit.caret == 1);
    edit.end(text, false);
    CHECK(edit.erase(text, false, true) && text == "hllo ");
    edit.selectAll(text);
    CHECK(edit.insert(text, "a\r\nb\tc", 64) && text == "a  b c");
    std::string full = "abc";
    edit = {}; edit.end(full, false);
    CHECK(edit.insert(full, "dé", 4) && full == "abcd"); // "é" doesn't fit whole, so it's dropped
    CHECK(!edit.insert(full, "x", 4) && full == "abcd");
    edit.selectWord("one two", 5);
    CHECK(edit.selection() == std::pair<size_t, size_t>{4, 7});
    edit.caret = 1; edit.anchor = 99;
    edit.clamp("é"); // mid-code-point positions snap back
    CHECK(edit.caret == 0 && edit.anchor == 2);
    CHECK(yh::TextEdit::nextWord("ab, cd", 0) == 4 && yh::TextEdit::previousWord("ab, cd", 6) == 4);
    yh::Input input;
    SDL_Event e{}; e.type = SDL_EVENT_TEXT_EDITING; e.edit.text = "かな"; e.edit.start = 1;
    input.handle(e);
    CHECK(input.composition() == "かな" && input.compositionCursor() == 1);
    input.endFrame(); CHECK(input.composition() == "かな"); // composition lasts until committed
    e = {}; e.type = SDL_EVENT_TEXT_INPUT; e.text.text = "仮名"; input.handle(e);
    CHECK(input.composition().empty() && input.text() == "仮名");
    e = {}; e.type = SDL_EVENT_KEY_DOWN; e.key.key = SDLK_LSHIFT; input.handle(e);
    CHECK(input.shiftDown() && !input.shortcutDown());
}

void gamepads()
{
    yh::Input input;
    input.setMap(yh::makeControlScheme(yh::ControlPreset::BG3).map);
    auto axis = [&](uint32_t pad, SDL_GamepadAxis a, float v) {
        SDL_Event e{}; e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION; e.gaxis.which = pad; e.gaxis.axis = static_cast<Uint8>(a);
        e.gaxis.value = static_cast<Sint16>(v * 32767); input.handle(e);
    };
    auto button = [&](uint32_t pad, SDL_GamepadButton b, bool down) {
        SDL_Event e{}; e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP; e.gbutton.which = pad;
        e.gbutton.button = static_cast<Uint8>(b); input.handle(e);
    };
    axis(7, SDL_GAMEPAD_AXIS_LEFTX, 0.1f);
    CHECK(input.axis(SDL_GAMEPAD_AXIS_LEFTX) == 0 && !input.down(yh::actions::panRight)); // inside the dead zone
    axis(7, SDL_GAMEPAD_AXIS_LEFTX, 0.6f);
    CHECK(near(input.value(yh::actions::panRight), 0.5f, 0.01f) && input.pressed(yh::actions::panRight) && input.value(yh::actions::panLeft) == 0);
    axis(8, SDL_GAMEPAD_AXIS_LEFTX, -0.3f); // a second pad pushing less doesn't cancel the first
    CHECK(input.down(yh::actions::panRight) && input.gamepads() == 2);
    input.endFrame();
    CHECK(!input.pressed(yh::actions::panRight) && input.down(yh::actions::panRight));
    axis(7, SDL_GAMEPAD_AXIS_LEFTX, 0);
    CHECK(input.released(yh::actions::panRight) && input.value(yh::actions::panLeft) > 0 && !input.down(yh::actions::panLeft));
    button(7, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    CHECK(input.pressed(yh::actions::zoomIn) && input.clicked(yh::actions::zoomIn) && input.value(yh::actions::zoomIn) == 1);
    button(8, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true); button(7, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
    CHECK(input.down(yh::actions::zoomIn)); // still held on the other pad
    SDL_Event removed{}; removed.type = SDL_EVENT_GAMEPAD_REMOVED; removed.gdevice.which = 8; input.handle(removed);
    CHECK(!input.down(yh::actions::zoomIn) && input.released(yh::actions::zoomIn) && input.gamepads() == 1);
    button(7, SDL_GAMEPAD_BUTTON_DPAD_UP, true);
    SDL_Event focus{}; focus.type = SDL_EVENT_WINDOW_FOCUS_LOST; input.handle(focus);
    CHECK(!input.down(yh::actions::panUp));
    yh::Camera camera;
    camera.setViewport({800, 600});
    yh::CameraControls controls;
    const auto before = camera.position();
    axis(7, SDL_GAMEPAD_AXIS_RIGHTY, 1);
    controls.update(camera, input, 0.5);
    CHECK(camera.position().y > before.y && camera.position().x == before.x);
    const auto saved = yh::InputMap::fromJson(input.map().toJson());
    CHECK(saved && saved->toJson() == input.map().toJson() && input.map().toJson().find("\"dpup\"") != std::string::npos);
    CHECK(!yh::InputMap::fromJson("{\"x\":[{\"type\":\"gamepadButton\",\"button\":\"nope\"}]}"));
    CHECK(!yh::InputMap::fromJson("{\"x\":[{\"type\":\"gamepadAxis\",\"axis\":\"leftx\",\"direction\":2}]}"));
}

void localization()
{
    const auto root = std::filesystem::path(YH_DEV_STATE_DIR) / "unit-files" / "lang-test";
    std::filesystem::create_directories(root / "lang");
    std::ofstream(root / "lang" / "en.json") << R"({"menu":{"start":"Start","quit":"Quit"},"hit":"{name} hits for {damage}",
        "coins":{"one":"{count} coin","other":"{count} coins"},"brace":"{{literal}} {unknown}"})";
    std::ofstream(root / "lang" / "pt.json") << R"({"menu":{"start":"Iniciar"},"coins":{"one":"{count} moeda","other":"{count} moedas"}})";
    std::ofstream(root / "lang" / "pt-BR.json") << R"({"menu":{"start":"Começar"}})";
    std::ofstream(root / "lang" / "ru.json") << R"({"coins":{"one":"{count} монета","few":"{count} монеты","many":"{count} монет"}})";
    std::ofstream(root / "lang" / "bad.json") << R"({"menu":{"start":5}})";
    yh::FileSystem files;
    CHECK(files.mountFolder(root.string()));
    yh::Strings strings;
    std::string error;
    CHECK(strings.load(files, "pt_BR", "en", &error) && strings.locale() == "pt-BR");
    CHECK(strings.get("menu.start") == "Começar" && strings.get("menu.quit") == "Quit");
    CHECK(strings.plural("coins", 1) == "1 moeda" && strings.plural("coins", 0) == "0 moeda" && strings.plural("coins", 5) == "5 moedas");
    CHECK(strings.get("hit", {{"name", "Ana"}, {"damage", "7"}}) == "Ana hits for 7");
    CHECK(strings.get("brace") == "{literal} {unknown}");
    CHECK(strings.get("menu.nope") == "menu.nope" && strings.missing().contains("menu.nope"));
    CHECK(strings.load(files, "ru") && strings.plural("coins", 21) == "21 монета" && strings.plural("coins", 23) == "23 монеты"
        && strings.plural("coins", 11) == "11 монет" && strings.plural("coins", 112) == "112 монет");
    CHECK(!strings.load(files, "de", "en", &error) && !error.empty() && strings.get("menu.start") == "Start");
    CHECK(!strings.load(files, "bad", "en", &error) && error.find("bad.json") != std::string::npos);
    CHECK(!strings.add("en", "[1]"));
    CHECK(strings.add("en", R"({"menu":{"start":"Begin"}})") && strings.load(files, "en") && strings.get("menu.start") == "Start");
    const auto locales = strings.available(files);
    CHECK(locales.size() == 5 && std::find(locales.begin(), locales.end(), "pt-BR") != locales.end());
    CHECK(yh::Strings::pluralCategory("ja", 1) == "other" && yh::Strings::pluralCategory("pl", 22) == "few"
        && yh::Strings::pluralCategory("ar", 2) == "two" && yh::Strings::pluralCategory("en", -1) == "one");
    CHECK(yh::Strings::format("{a}{b}", {{"a", "1"}, {"b", "2"}}) == "12");
}

void templates()
{
    yh::Grid grid(yh::GridType::Square, 10);
    // A 20-unit (two cell) radius burst on a grid intersection covers the 12 cells whose centres are within reach.
    yh::AreaTemplate burst{yh::TemplateShape::Circle, {50, 50}, {1, 0}, 20};
    const auto burstCells = burst.cells(grid);
    CHECK(burstCells.size() == 12);
    for (yh::Cell c : burstCells) CHECK(burst.contains(grid.center(c), 10));
    yh::AreaTemplate cube{yh::TemplateShape::Square, {50, 50}, {1, 0}, 20};
    CHECK(cube.cells(grid).size() == 4);
    const auto line = yh::AreaTemplate::aimed(yh::TemplateShape::Line, {5, 5}, {200, 5}, 50);
    const auto lineCells = line.cells(grid);
    CHECK(lineCells.size() == 6); // centres at x = 5..55 on the origin's row
    for (yh::Cell c : lineCells) CHECK(c.y == 0);
    const auto cone = yh::AreaTemplate::aimed(yh::TemplateShape::Cone, {0, 50}, {100, 50}, 30);
    const auto coneCells = cone.cells(grid);
    CHECK(!coneCells.empty());
    for (yh::Cell c : coneCells) CHECK(grid.center(c).x >= 0 && grid.center(c).x <= 30);
    CHECK(cone.contains({25, 50}, 10) && !cone.contains({-5, 50}, 10) && !cone.contains({10, 70}, 10));
    CHECK(cone.outline(grid).size() > 3 && burst.outline(grid, 24).size() == 24);
    yh::Grid hex(yh::GridType::Hex, 10);
    yh::AreaTemplate hexBurst{yh::TemplateShape::Circle, hex.center({4, 4}), {1, 0}, 10.5f};
    CHECK(hexBurst.cells(hex).size() == 7); // a hex and its six neighbours

    const std::array<yh::Vec2, 3> route{{{5, 5}, {35, 35}, {65, 65}}}; // 3 + 3 diagonals
    grid.diagonals = yh::DiagonalRule::Alternating;
    const auto measured = yh::measure(grid, route);
    CHECK(measured.legs.size() == 2 && measured.legs[0] == 4 && measured.legs[1] == 5 && measured.total == 9);
    grid.diagonals = yh::DiagonalRule::Equal;
    CHECK(yh::measure(grid, route).total == 6);
    yh::Grid open(yh::GridType::Gridless, 10);
    const std::array<yh::Vec2, 2> straight{{{0, 0}, {30, 40}}};
    CHECK(near(yh::measure(open, straight).total, 5));
}

void atlases()
{
    yh::RectPacker packer(64, 64);
    std::vector<yh::PixelRect> placed;
    while (auto spot = packer.insert(16, 16)) placed.push_back(*spot);
    CHECK(placed.size() == 16 && near(packer.occupancy(), 1));
    for (size_t i = 0; i < placed.size(); ++i)
        for (size_t j = i + 1; j < placed.size(); ++j)
        {
            const auto& a = placed[i]; const auto& b = placed[j];
            CHECK(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
        }
    packer.clear();
    CHECK(packer.insert(64, 10) && packer.insert(10, 54) && packer.insert(54, 54) && !packer.insert(1, 1));
    CHECK(!packer.insert(65, 1));

    auto solid = [](int w, int h, unsigned char shade) {
        yh::Image image{w, h, std::vector<unsigned char>(static_cast<size_t>(w) * h * 4, shade)};
        return image;
    };
    yh::Atlas atlas(64, 2);
    CHECK(atlas.add("a", solid(20, 20, 10)) && atlas.add("b", solid(30, 10, 20)) && atlas.add("tall", solid(8, 50, 30)));
    CHECK(!atlas.add("a", solid(1, 1, 0)) && !atlas.add("huge", solid(61, 4, 0)) && !atlas.add("broken", yh::Image{2, 2, {}}));
    for (int i = 0; i < 20; ++i) atlas.add("tile" + std::to_string(i), solid(14, 14, static_cast<unsigned char>(100 + i)));
    atlas.pack();
    CHECK(atlas.size() == 23 && atlas.pages() >= 2);
    for (const char* name : {"a", "b", "tall", "tile0", "tile19"})
    {
        const yh::AtlasRegion* r = atlas.find(name);
        CHECK(r != nullptr);
        if (!r) continue;
        const auto& page = atlas.pageImage(static_cast<size_t>(r->page));
        const auto pixel = [&](int x, int y) { return page.rgba[(static_cast<size_t>(y) * 64 + static_cast<size_t>(x)) * 4]; };
        const unsigned char shade = pixel(r->pixels.x, r->pixels.y);
        // Inside and the padding just outside repeat the image's edge colour.
        CHECK(pixel(r->pixels.x + r->pixels.w - 1, r->pixels.y + r->pixels.h - 1) == shade && pixel(r->pixels.x - 1, r->pixels.y - 1) == shade);
        CHECK(near(r->uv.x * 64, static_cast<float>(r->pixels.x)) && near(r->uv.w * 64, static_cast<float>(r->pixels.w)));
    }
    CHECK(atlas.find("tile7")->pixels.w == 14 && !atlas.find("nope"));
}

void spriteSheets()
{
    const char* json = R"({"frameWidth":16,"frameHeight":16,"clips":{
        "idle":{"frames":"0-1","fps":2},
        "attack":{"frames":[2,3,4],"fps":10,"loop":false,"next":"idle","events":{"0":"windup","2":"hit"}},
        "die":{"frames":"7-5","durations":[0.1,0.2,0.3],"loop":false}}})";
    std::string error;
    auto sheet = yh::SpriteSheet::fromJson(json, {64, 32}, {0, 0, 1, 1}, &error);
    CHECK(sheet && error.empty() && sheet->frames.size() == 8 && sheet->frameSize == yh::Vec2{16, 16});
    if (!sheet) return;
    CHECK(near(sheet->frames[5].x, 0.25f) && near(sheet->frames[5].y, 0.5f) && near(sheet->frames[5].w, 0.25f));
    CHECK((sheet->clip("die")->frames == std::vector<int>{7, 6, 5}));
    yh::SpriteAnimator animator;
    std::vector<std::string> events;
    auto record = [&](std::string_view e) { events.emplace_back(e); };
    animator.play(*sheet, "attack");
    animator.update(*sheet, 0.05, record);
    CHECK(animator.frame(*sheet) == 2 && events == std::vector<std::string>{"windup"});
    animator.update(*sheet, 0.3, record); // skips past the last frame straight into idle
    CHECK(animator.clip() == "idle" && events.size() == 2 && events[1] == "hit" && animator.frame(*sheet) == 0);
    animator.update(*sheet, 0.5);
    CHECK(animator.frame(*sheet) == 1);
    animator.update(*sheet, 0.5);
    CHECK(animator.frame(*sheet) == 0); // loops
    animator.play(*sheet, "idle");
    CHECK(animator.frame(*sheet) == 0);
    animator.play(*sheet, "die");
    animator.speed = 2;
    animator.update(*sheet, 0.1);
    CHECK(animator.frame(*sheet) == 6);
    animator.update(*sheet, 10);
    CHECK(animator.finished() && animator.frame(*sheet) == 5);
    animator.play(*sheet, "missing");
    CHECK(animator.clip() == "die");
    // An atlas region holding a 2x1 strip: frames map inside it.
    auto strip = yh::SpriteSheet::fromJson(R"({"frames":[{"x":0,"y":0,"w":8,"h":8},{"x":8,"y":0,"w":8,"h":8}]})", {64, 64}, {0.5f, 0.5f, 0.25f, 0.125f});
    CHECK(strip && near(strip->frames[1].x, 0.625f) && near(strip->frames[1].y, 0.5f));
    CHECK(!yh::SpriteSheet::fromJson(R"({"frames":[{"x":10,"y":0,"w":8,"h":8}]})", {64, 64}, {0, 0, 0.25f, 0.25f}));
    CHECK(!yh::SpriteSheet::fromJson(R"({"frameWidth":16,"frameHeight":16,"clips":{"a":{"frames":[9]}}})", {32, 32}));
    CHECK(!yh::SpriteSheet::fromJson(R"({"frameWidth":16,"frameHeight":16,"clips":{"a":{"frames":[0],"next":"b"}}})", {32, 32}));
    CHECK(!yh::SpriteSheet::fromJson(R"({"frameWidth":16,"frameHeight":16,"clips":{"a":{"frames":[0],"fps":0}}})", {32, 32}));
}

void richText()
{
    const yh::RichText::Palette palette{{"bad", {220, 90, 80, 255}}};
    auto text = yh::RichText::parse("[color=#ff8040]Fire[/color] deals [color=bad]12[/color] damage [icon=fire] [[ok] [nope] [/wave]", &palette);
    CHECK(text.plain() == "Fire deals 12 damage  [ok] [nope] [/wave]");
    text.layout(nullptr, 0, 1); // debug font: 8 px per character at scale 1
    CHECK(text.lines() == 1 && near(text.size().x, 8 * 41 + yh::Renderer::lineHeight(1) + 2));
    text.layout(nullptr, 8 * 11, 1); // "Fire deals " fits; the rest wraps at spaces
    CHECK(text.lines() > 2 && text.size().x <= 8 * 11);
    auto nested = yh::RichText::parse("[wave][color=#00ff00]a[/wave][/color]");
    CHECK(nested.plain() == "a[/wave]"); // closing out of order is shown as text
    CHECK(yh::RichText::parse("[color=#zzzzzz]x").plain() == "[color=#zzzzzz]x");
    const std::string sneaky = "[color=#ff0000]admin[/color]";
    CHECK(yh::RichText::parse(yh::RichText::escape(sneaky)).plain() == sneaky);
    auto lines = yh::RichText::parse("one\ntwo three");
    lines.layout(nullptr, 0, 1);
    CHECK(lines.lines() == 2 && near(lines.size().x, 8 * 9));
    auto longWord = yh::RichText::parse("abcdefghij");
    longWord.layout(nullptr, 8 * 4, 1);
    CHECK(longWord.lines() == 3);
    CHECK(yh::RichText::parse("héllo").plain() == "héllo");
}

void profiler()
{
    yh::debug::clear();
    yh::debug::value("tokens", 12);
    for (int i = 0; i < 3; ++i) { YH_PROFILE("work"); SDL_DelayNS(200000); }
    const auto& entries = yh::debug::entries();
    CHECK(entries.size() == 2 && entries[0].current == 12 && entries[1].timer && entries[1].current >= 0.5);
    yh::debug::endFrame();
    CHECK(entries[1].current == 0 && entries[1].smoothed >= 0.5 && entries[0].current == 12);
    const double before = entries[1].smoothed;
    yh::debug::endFrame(); // the timer didn't run: it eases toward zero
    CHECK(entries[1].smoothed < before && entries[0].smoothed == 12);
    yh::debug::clear();
    CHECK(yh::debug::entries().empty());
}

// A tiny shared game: a counter that only grows by 1-3 per move.
struct CounterGame
{
    int value = 0;
    std::vector<std::string> log;
    void apply(const yh::NetCommand& c) { value += std::stoi(c.data); log.push_back(std::to_string(c.player) + ":" + c.data); }
};

void networking()
{
    yh::LoopbackHub hub;
    CounterGame hostGame;
    std::vector<std::string> events;
    yh::SessionHost::Rules rules;
    rules.validate = [](yh::PlayerId, std::string_view type, std::string_view data, std::string& reason) -> std::optional<std::string> {
        const int step = std::atoi(std::string(data).c_str());
        if (type != "add" || step < 1 || step > 3) { reason = "illegal move"; return std::nullopt; }
        return std::string(data);
    };
    rules.apply = [&](const yh::NetCommand& c) { hostGame.apply(c); };
    rules.snapshot = [&] { return std::to_string(hostGame.value); };
    rules.checksum = [&] { return static_cast<uint64_t>(hostGame.value); };
    rules.admit = [](std::string_view name, std::string& reason) { reason = "banned"; return name != "griefer"; };
    rules.joined = [&](yh::PlayerId p, std::string_view name) { events.push_back("join " + std::to_string(p) + " " + std::string(name)); };
    rules.left = [&](yh::PlayerId p) { events.push_back("left " + std::to_string(p)); };
    yh::SessionHost host(hub.host(), "yorehold", "1.0", rules);
    CHECK(host.submit("add", "2") && hostGame.value == 2);
    std::string reason;
    CHECK(!host.submit("add", "9", &reason) && reason == "illegal move");

    struct Client
    {
        CounterGame game;
        std::string ended;
        std::vector<std::string> rejections;
        std::unique_ptr<yh::SessionClient> session;
        Client(yh::LoopbackHub& hub, std::string version, std::string name, bool honestChecksum = true)
        {
            yh::SessionClient::Handlers h;
            h.welcomed = [this](yh::PlayerId, std::string_view snapshot) { game.value = std::stoi(std::string(snapshot)); };
            h.apply = [this](const yh::NetCommand& c) { game.apply(c); };
            h.rejected = [this](uint64_t, std::string_view why) { rejections.emplace_back(why); };
            h.disconnected = [this](std::string_view why) { ended = why; };
            h.checksum = [this, honestChecksum] { return static_cast<uint64_t>(game.value + (honestChecksum ? 0 : 1)); };
            session = std::make_unique<yh::SessionClient>(hub.connect(), "yorehold", std::move(version), std::move(name), h);
        }
    };
    auto pump = [&](std::initializer_list<Client*> clients) {
        for (int i = 0; i < 4; ++i) { host.update(0.01); for (Client* c : clients) c->session->update(0.01); }
    };
    Client alice(hub, "1.0", "alice"), bob(hub, "1.0", "bob");
    CHECK(alice.session->submit("add", "1") == 0); // not joined yet
    pump({&alice, &bob});
    CHECK(alice.session->joined() && bob.session->joined() && alice.game.value == 2 && host.players().size() == 3);
    CHECK(host.playerName(alice.session->player()) == "alice");
    alice.session->submit("add", "3");
    bob.session->submit("add", "7");
    pump({&alice, &bob});
    CHECK(hostGame.value == 5 && alice.game.value == 5 && bob.game.value == 5 && bob.rejections.size() == 1);
    CHECK(alice.session->sequence() == host.sequence() && bob.session->sequence() == host.sequence());
    host.submit("add", "1");
    pump({&alice, &bob});
    Client late(hub, "1.0", "carol");
    pump({&alice, &bob, &late});
    CHECK(late.game.value == 6 && late.game.log.empty());
    late.session->submit("add", "1");
    pump({&alice, &bob, &late});
    // Alice joined after the host's first move, so she saw every later command in the same order.
    CHECK(std::equal(alice.game.log.begin(), alice.game.log.end(), hostGame.log.begin() + 1, hostGame.log.end()));
    CHECK(alice.game.value == 7 && late.game.value == 7);

    Client old(hub, "0.9", "dave"), griefer(hub, "1.0", "griefer"), liar(hub, "1.0", "eve", false);
    pump({&old, &griefer, &liar});
    CHECK(old.ended.starts_with("Version mismatch") && griefer.ended == "banned" && liar.session->joined());
    host.submit("add", "1");
    pump({&alice, &liar});
    CHECK(liar.ended == "Out of sync with the host" && alice.ended.empty());
    host.kick(bob.session->player(), "bye bob");
    alice.session->leave();
    pump({&alice, &bob, &late});
    CHECK(bob.ended == "bye bob" && alice.ended == "Left the session" && host.players().size() == 2);
    CHECK(std::count(events.begin(), events.end(), "left " + std::to_string(bob.session->player())) == 1);
    for (int i = 0; i < 200; ++i) host.update(0.1); // carol stops answering
    CHECK(host.players().size() == 1 && late.ended.empty());
    late.session->update(0); CHECK(late.ended == "Timed out");

    std::string error;
    auto server = yh::TcpTransport::listen(0, &error, true);
    CHECK(server && server->port() != 0);
    if (!server) return;
    auto client = yh::TcpTransport::connect("127.0.0.1", server->port(), &error);
    CHECK(client != nullptr);
    std::string big(300000, 'x');
    big.front() = 'a'; big.back() = 'z';
    std::optional<yh::PeerId> joined;
    bool clientConnected = false;
    std::vector<std::string> received;
    for (int i = 0; i < 400 && (received.size() < 2); ++i)
    {
        while (auto e = server->poll())
        {
            if (e->type == yh::NetEvent::Type::Connected) { joined = e->peer; server->send(e->peer, "hi"); server->send(e->peer, big); }
        }
        while (auto e = client->poll())
        {
            if (e->type == yh::NetEvent::Type::Connected) clientConnected = true;
            if (e->type == yh::NetEvent::Type::Message) received.push_back(e->data);
        }
        SDL_Delay(5);
    }
    CHECK(joined && clientConnected && received.size() == 2 && received[0] == "hi" && received[1] == big);
    client.reset();
    bool serverSawLeave = false;
    for (int i = 0; i < 200 && !serverSawLeave; ++i)
    {
        while (auto e = server->poll()) serverSawLeave |= e->type == yh::NetEvent::Type::Disconnected && joined && e->peer == *joined;
        SDL_Delay(5);
    }
    CHECK(serverSawLeave);
    auto nobody = yh::TcpTransport::connect("127.0.0.1", 1, &error);
    bool refused = !nobody;
    for (int i = 0; i < 1000 && !refused; ++i) // Windows retries refused connections for about two seconds
    {
        while (auto e = nobody->poll()) refused |= e->type == yh::NetEvent::Type::Disconnected;
        SDL_Delay(5);
    }
    CHECK(refused);
}
}

int main()
{
    const std::pair<const char*, void(*)()> tests[] = {
        {"Images", regression::images},
        {"Dialogue", regression::dialogues},
        {"Tweens", tweens},
        {"Particles", particles},
        {"Visibility/fog", visibilityAndFog},
        {"Maps/navigation", mapsAndPaths},
        {"Objects/regions", objectsAndRegions},
        {"Tokens/party", tokensAndParty},
        {"Input/files/themes", inputFilesAndTheme},
        {"Audio/scenes", audioAndScenes},
        {"RPG", rpg},
        {"Saves/history", savesAndHistory},
        {"Networking", networking},
        {"Text editing", textEditing},
        {"Gamepads", gamepads},
        {"Localization", localization},
        {"Templates", templates},
        {"Atlases", atlases},
        {"Sprite sheets", spriteSheets},
        {"Rich text", richText},
        {"Profiler", profiler},
    };
    for (const auto& [name, test] : tests)
    {
        const int before = regression::failures;
        try
        {
            test();
        }
        catch (const std::exception& error)
        {
            ++regression::failures;
            std::fprintf(stderr, "%s threw: %s\n", name, error.what());
        }
        std::printf("%s: %s\n", name, regression::failures == before ? "PASS" : "FAIL");
    }
    std::printf("%d checks, %d failures\n", regression::checks, regression::failures);
    return regression::failures ? 1 : 0;
}
