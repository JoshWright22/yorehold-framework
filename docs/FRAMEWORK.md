# Framework APIs and verification

The framework is a C++20 library in namespace `yh`. The client supplies game screens, content, the rules that accept or reject multiplayer actions, and an editor. All runtime APIs run on the calling thread. GPU handles are renderer-specific.

## Runtime and rendering

Implement `Game::load`, `update`, `draw`, `handleEvent` and `unload`, then call `run`. `unload` runs while the window and renderer still exist. `Scenes` owns a stack of `Game` screens; push/pop/clear requested from callbacks are deferred until callbacks finish. Overlay flags independently allow drawing or updating lower screens.

`Renderer` batches textured quads and coloured shapes. Camera transforms, nested viewports, blend modes, offscreen targets and gradients share that pipeline. Pair each push with its matching pop. `textureRelease(id)` returns a weak lifetime-safe callback; invoke it once when an owned texture is no longer needed. Releases are drained at the next frame boundary, after previously queued commands have been submitted. Assets, fonts, overview textures, lights, particles and tokens use this ownership mechanism.

`HostSettings` controls window size, vsync, feedback and state directories. `--fixed-dt` drives deterministic simulation; `maxDeltaSeconds` limits measured pauses. Scripted events and screenshots are available without publishing anything. Coordinates delivered to games use drawable pixels, including high-density windows.

F3 shows frame times with a graph of the last 120 frames, the frame's draw calls, passes, vertices and texture memory (`Renderer::stats()`), and anything the game reports. `yh::debug::value("tokens", n)` shows a number; `YH_PROFILE("pathfinding")` times a scope, and scopes with the same name add up across the frame. Scripted `shot` screenshots include the overlay.

## Input, camera and movement

`InputMap` binds named actions to keys or mouse buttons. Persist `toJson()` and restore with `fromJson()`. `makeControlScheme(ControlPreset::Foundry)` and `makeControlScheme(ControlPreset::BG3)` provide presets. Feed SDL events into `Input`, read it, then call `endFrame()` once. Focus loss clears held buttons and keys; UI key repeats and text input remain separate from action presses.

Gamepads open automatically when connected. `Binding::gamepadButton` and `Binding::gamepadAxis(axis, ±1)` bind buttons and stick or trigger halves; bindings save with SDL's names (`"a"`, `"dpup"`, `"leftx"`). Buttons and axes combine across all pads, and the most-pushed pad wins an axis. Sticks have a 0.2 dead zone; an axis half counts as pressed past 0.5. `value(action)` is analog (0–1) for sticks and 1 for held keys, so camera panning follows stick tilt. Both presets pan with either stick or the d-pad, zoom with the shoulders and recentre with Back.

`CameraControls` handles eased pan/zoom, bounds, edge scroll, selection following and the Home key. Panning stops following; `resumeFollowing()` restarts it (for example when the player orders a walk). `Grid` supports square, hex and gridless cell lookup, neighbours, distance and snapping. `findPath` provides bounded A* with no diagonal corner cutting. `Navigation` caches connected components within chunks and searches between those components before refining a route. Its coarse route favors bounded work; it does not promise a globally shortest cell path. Call `invalidate(cell)` after changing passability, or `clear()` after changing the grid/topology.

`TokenController` implements clicks, drags, box selection, path previews and group destinations. `link(follower, leader)` creates same-owner chains and rejects cycles; followers trail their parent outside combat. `settings.inCombat` and `settings.activeTurn` restrict movement to the active token. `Token::pace` scales `settings.walkCellsPerSecond` for one token (0.5 walks at half speed). `setFloor` can transfer a chain; `viewedFloor` filters drawing and interaction. The client selects when stairs or transitions trigger it. The passability callback describes the current floor. Links use token indices: call `clearLinks()` before erasing/reordering the public token vector. Set `contextActions` to client labels and consume `contextChoice` after update.

Set `Token::image` to a virtual file path and call `controller.useAssets(assets)` once; the assets must outlive the controller. Portraits crop the image's centre to an antialiased circle with a ring of the token's colour. `TokenImageStyle::Cutout` draws the complete image, preserves transparency and aspect ratio, and uses `imageScale` to adjust its size relative to the token's footprint. Selection, movement, hit testing and collision still use the token's radius. Empty image paths keep the coloured disc and initial. Set `initialFont` to draw initials with a real font, and `labelFont` for the distance label and context menu; both fall back to the debug font when null and must outlive draw calls.

```cpp
tokens.useAssets(assets);
tokens.tokens[0].image = "tokens/hero.jpg"; // circular portrait
tokens.tokens[1].image = "tokens/archer.webp";
tokens.tokens[1].imageStyle = yh::TokenImageStyle::Cutout;
tokens.tokens[1].imageScale = 1.2f;
```

`AreaTemplate` covers circles, cones, lines and squares in world units. `AreaTemplate::aimed(shape, origin, target, size)` aims cones and lines. `cells(grid)` returns every cell whose centre lies inside (the Foundry/5e rule), on square, hex and gridless grids; `draw` fills and outlines it. `measure(grid, waypoints)` returns per-leg and total distances in cells. The 5-10-5 diagonal count carries across waypoints. `drawRuler` labels each waypoint in client units (`5, "ft"`).

## Maps, objects and streamed worlds

`TileMap(width, height, tileSize)` stores 32×32 chunks only where cells differ from a layer's uniform default. `addLayer(name, floor, defaultTile)` makes a huge uniform map without filling every cell. `setLayerImage` supports painted floors behind tiles. Far views use 1024×1024 overview pages. Tile/image data survives JSON saves; texture handles do not, so bind the tileset and image textures after loading. Ownership of supplied atlas/image textures stays with the caller.

Call `trackChanges()` after loading authored terrain. `changesJson()` stores final edited cells, including removals; `applyChanges()` validates the entire delta before applying it. Full map snapshots preserve the tracking state. Layer structure and images are authored data; cell deltas require the same dimensions, tile size and layer indices.

Use `assets.image(path)` and `buildTileset(renderer, images, tilePixels)` to turn separate PNG/JPEG/WebP or other supported pictures into a tileset. Non-square pictures are centre-cropped and resampled; each image becomes one tile id, starting at 1. Repeated edge padding prevents filtering seams. The resulting tileset shares ownership of its GPU texture and computes colours and alpha for distant overview rendering. `sliceImage(sheet, cellWidth, cellHeight, margin, spacing)` splits existing sheets, skipping incomplete cells and the border on every side. Rebuild an atlas after its source files change.

```cpp
auto stone = assets.image("tiles/stone.jpg");
auto grass = assets.image("tiles/grass.png");
if (stone && grass)
{
    const std::array<yh::Image, 2> images{*stone, *grass};
    if (auto tileset = yh::buildTileset(renderer, images, 64))
        map.setTileset(std::move(*tileset)); // stone = tile 1, grass = tile 2
}
// A painted map uses the same decoder, without splitting it into cells.
map.setLayerImage(ground, "maps/tavern.webp", assets.texture("maps/tavern.webp", true), map.worldBounds());
```

`Kit` is a JSON object prototype. `Objects::place` creates independent instances with unique ids. Tags control movement/sight blocking, containers, destructibility, levers and throwing; optional components carry door state, health and lights. Inventory keys use `key:*`; levers and doors share `link:*`. `walls()` and `lights()` feed visibility and lighting. `throwTo` animates ground position plus a visual arc; `update` reports landings after iteration, so callbacks can safely remove objects. Flight state survives saves. The client decides throw range, collision/hit rules and landing damage.

`Regions` registers authored loaders. `enter(player, id)` loads before leaving the current region. Every occupied region ticks; leaving its last player saves and unloads it. Returning restores object/map/variable/clock state. Empty regions never advance. Membership changes must happen outside update callbacks. `WorldPosition` retains doubles until subtracting the camera origin, preserving local motion far from the origin.

```cpp
yh::Regions world;
world.add("village", loadAuthoredRegion);
world.enter(0, "village");
world.update(dt, [](yh::Region& region, double seconds) {
    region.objects.update(seconds);
});
std::string save = world.changesJson();
world.leave(0);
world.restoreChanges(save); // loaders supply authored maps; saved edits are applied
world.enter(0, "village");
```

`toJson/restore` provide complete sparse snapshots. `changesJson/restoreChanges` save terrain edits over authored maps and complete object/variable/clock state. All players must leave before restoring. Restore validates all entries before committing, and regions absent from the save reset to authored state. Player membership is client/session state, so explicitly re-enter players after restore.

## Assets, themes, effects and audio

Mount default assets in `FileSystem`, then a skin folder or `.yoreskin` zip. Later mounts override earlier ones. Paths are normalized and parent/absolute paths are rejected. `Assets` caches textures and TTF fonts; `hotReload()` reloads changed folder assets. Clear it after changing mounts. Cached Font pointers must be reacquired after clearing/reloading. Archived assets are remounted to pick up archive changes.

`decodeImage` identifies files from their contents and reads PNG, JPEG, static WebP, GIF's first frame, BMP, TGA, flattened PSD and PNM. JPEG EXIF orientation is applied before cropping. `Assets::texture`, `circleTexture`, `image`, and `Renderer::loadTexture` share that decoder. Resampling filters premultiplied alpha to preserve transparent edges; texture uploads fit oversized images within an 8192-pixel side while keeping their proportions. Plain, smooth and circle textures have separate cache entries and all reload when their file changes. SVG, TIFF, HEIC and AVIF require conversion to a supported raster format.

[Editing image tiles and tokens](IMAGES.md) lists the functions to change for cropping, filtering, portraits, atlas padding and hot reload, with the relevant tests and ownership rules.

`Skin::load(files)` reads metadata, theme colours, body font, animation settings and particle effects from `skin.json`. `resolveTheme(assets)` refreshes its Font pointer. The shipped manifest demonstrates the schema. Reload the manifest itself when changing theme data; asset hot reload refreshes images/fonts independently. Theme `border`, `bevel` and `shadow` (pixels, 0–16, plus `shadowColor`) shape every frame: 1/0/0 is thin and flat, about 3/2/4 is chunky with light/dark bevels, and pressed buttons sink. `Ui` provides themed panels, buttons, toggles, checkboxes, bars, sliders, UTF-8 text boxes, logs and nested scrolling. Text boxes support clicking to place the caret, drag or Shift selection, double-click (word) and triple-click (all), Ctrl word jumps and word deletes, and Ctrl+A/C/X/V through the system clipboard. Input-method composition shows inline, underlined at the caret, and the OS candidate list opens next to it. Pasted line breaks become spaces, and text over the byte limit is cut at a character boundary. The editing logic is `TextEdit`, which editors can reuse without rendering.

`Atlas` packs many small images into a few large pages (skyline packing, tallest first) so sprites draw in one batch. Padding repeats each image's edge pixels so filtering never bleeds. `add` queues images (`decodeImage` reads the supported raster formats without the GPU), `upload` packs and creates or updates pages, and `find`/`draw` use regions by name. Atlases own their pages and release them when destroyed. `RectPacker` is available on its own.

`SpriteSheet::fromJson` cuts frames from a grid (`frameWidth`, `frameHeight`, optional `margin`/`spacing`/`count`) or an explicit `frames` list, inside a whole texture or an atlas region. Clips list frames as arrays or ranges (`"0-3"`, `"7-5"` plays backwards) with fps or per-frame `durations`, `loop`, `next` (attack → idle) and frame `events` ("hit"). `SpriteAnimator` is plain per-entity state: events fire for every frame entered, even when one long update skips several, and `speed` scales playback. Draw with `sheet.draw(renderer, animator.frame(sheet), dest, tint, flipX)`.

`Tweens::to` replaces a tween on the same float, samples its start after a delay and delivers completion callbacks after removal. Targets must remain alive until completion/cancellation. Zero-duration tweens finish on update, including `update(0)`. Nine easing curves are available.

`Cutscene::fromJson` reads a list of steps: `camera` ([x, y], optional `zoom`), `caption`, `title`, `fade` (to an [r,g,b,a] colour; alpha 0 fades back in), `bars` (letterbox on/off), `pause` and `event`. Each step has `seconds` and an optional `ease`; `"wait": false` starts the next step alongside it, such as a camera move under captions. `update(dt, camera, onEvent)` steers the camera (clamped to the map like the player camera) and calls `onEvent` with event names. `draw(renderer, bodyFont, titleFont)` draws the bars, fade and text in screen space. `skip` jumps to the end state and still fires every remaining event. Don't call camera controls while one runs.

`ParticleEffect::fromJson` validates ranges, colours and emitter settings. `ParticleSystem` reserves parallel arrays to its capacity, uses independent seeded emitter random streams and shares style data. Bursts and continuous emission support gravity, drag, scaling, fading, spin, custom textures and blending. Overflow is discarded. Closed-form integration and birth-time offsets keep normal unsaturated simulations consistent across frame lengths; pool saturation deliberately drops emissions.

`Visibility` casts rays to walls; `Lighting` combines ambient colour and additive radial shadowed lights, then multiplies the world. `FogOfWar` stores sparse explored/visible bits separately for every team and floor. Exploration survives saves; visibility is recalculated from current observers. Fog is cell-based; light shadow polygons are continuous. For lighting that follows the rules, `LightLevels` grades each cell Dark, Dim or Bright from fixed and carried lights, and `FogOfWar::update` takes an optional `lit` test: unlit cells are then only seen within an observer's `darkRadius`. Classes and creatures carry `darkvision` in feet, exposed as the `darkvision` stat so items can grant it.

`Sound::fromWav` converts decoded audio to 48 kHz stereo floats. `Audio` mixes up to 64 simultaneous voices with looping, pitch, pan, master gain and Effects/Music/Ui buses. `init` failure is recoverable when no device exists; `mix` works offline for tests. Call `update` regularly to fill the device's 50 ms queue. Samples shared with voices must not be modified during playback.

## RPG data

`Random` supplies seeded PCG32 rolls. Dice expressions support multiple terms, constants and keep-high/low rolls. `StatBlock` combines base values, additions, multiplication and overrides by source. `Ruleset` supplies ability/skill/condition definitions, score conversion, AC, proficiency and XP progression; built-in classic/modern presets can be serialized and customized as JSON. Two numbers are there for the game to read: `magicItemLimit` (magic items one character may carry, 0 = no limit) and `passiveBase` (a passive score is this plus the check modifier, 10 by default). An AI profile's `alarmReach` is how many squares of walking from allies not yet in the fight a creature running for help has to get before they join (3 by default).

Healing is ruleset data, so each game picks its own style. `rests` lists named rests (`short`, `long`…) with a per-adventure limit (0 = unlimited) and a `Recovery`: `none`, `full`, `fraction` of max HP, `flat` HP, or `hitDice` (that many dice, 0 = one per level, each plus `hitDieAbility`; sides from `hitDieByClass`, else `defaultHitDie`). `reviveDowned` lets a recovery lift characters at 0 HP. `afterVictory` heals the winners and `reviveAfterVictory` gets downed winners up with that much HP. `Character::recover(rules, recovery, random, &detail)` applies one and returns the HP gained. The modern preset has 2 hit-dice short rests and 1 full long rest per adventure and revives at 1 HP after a win; the classic preset has unlimited one-die rests and no revive.

Conditions are data too. A `ConditionDefinition` is one JSON object, either in a ruleset's `conditions` list or in a file of its own: `Ruleset::loadConditions(files, folder)` reads every `<id>.json` in a folder, replaces definitions with the same id, and changes nothing if any file fails (the error names it).

| Field | Meaning |
|---|---|
| `id`, `name`, `description` | Only `id` is required; a file's name must match it. |
| `modifiers` | `stat`, `op` (`add`, `multiply`, `override`) and `value`, applied while the condition lasts. |
| `advantageOnAttacks`, `disadvantageOnAttacks` | Forces the attack roll. |
| `flags` | Names the game asks about with `Character::hasFlag`. `Encounter` honours `cantAct` (no actions, bonus action or reaction that turn) and `cantMove` (no movement); any other name is the game's own. |
| `duration` | Rounds it lasts when applied without one; -1 (the default) until something ends it. |
| `stacking` | What applying it again does: `refresh` (the new duration stands; the default), `longest` (the longer one stays) or `value` (values add up to `maxValue`). |
| `maxValue`, `perValue`, `decay` | For `value` stacking: the cap, whether additive modifiers are multiplied by the value, and how much the value drops at the end of each round (at 0 the condition ends). |
| `ends` | Events that end it: `turnStart`, `turnEnd`, `attack`, `damage`, `healed`, `move`, `rest`, `fightStart`, `fightEnd`. |
| `save` | `ability` and `dc`: a save rolled at the end of each round that ends it on a success. |
| `removes` | Conditions taken off when this one is applied. |

`Character::addCondition(rules, id, rounds, value)` applies one (leave `rounds` out for the definition's duration), `conditionValue` and `hasFlag` read them, `conditionEvent(rules, event)` ends everything that listens for an event and returns the ids, and `endRound(rules, &random)` counts durations down, applies decay and rolls saves. `Encounter` raises `turnStart`, `turnEnd`, `attack` (after the roll) and `damage` itself, runs `endRound` when a round ends and logs each condition that ends; the game raises the rest. A condition the ruleset does not define is still tracked by id, with nothing attached. Unknown events, stacking names, saves with an ability the ruleset lacks and `removes` naming a missing condition all fail validation.

### Effects

An `Effect` is what a spell, an action, an item, a trap or a feature does: a list of steps, read from JSON and run by the framework. `Effect::fromJson` takes either the list itself or an object with `effects` and an optional `save`:

```json
{
  "save": { "ability": "dex", "dc": "caster" },
  "effects": [
    { "do": "damage", "dice": "3d6", "type": "fire", "target": "area", "onSave": "half",
      "scale": { "by": "slot", "from": 1, "dice": "1d6" } },
    { "do": "condition", "id": "burning", "target": "area", "when": "saveFailed" }
  ]
}
```

Every step has `do` and may have:

| Field | Meaning |
|---|---|
| `target` | Who it lands on: `self` (whoever does it), `target` (who it was aimed at; the default), `area`, `allies` or `enemies` (the host says who those are). |
| `when` | Leave out for always. A result of a roll made earlier in the effect: `hit`, `miss`, `crit`, `success`, `failure` (a check), `saveFailed`, `saveSucceeded`. Or an event (`turnStart`, `turnEnd` and the other condition events): such a step is skipped when the effect is done, and is the only kind that runs when the effect is run for that event. |
| `onSave` | What a successful save does to this step: `full` (nothing; the default), `half` (damage only) or `none` (the step does not happen). |
| `scale` | Grows the step once for each `every` (default 1) levels above `from` (default 1), counted `by` `level` (the doer's, or the one the caller gives) or `slot`. `dice` is added to a rolled amount each time and `value` to an amount, a count or a condition's value. |

The steps:

| `do` | Fields | Does |
|---|---|---|
| `damage` | `dice`, `type`, `crit`, `minimum` | Takes HP (temporary HP first). `dice` is dice, a number or `"weapon"` (the doer's weapon dice and bonuses). After a critical hit the dice are rolled twice unless `crit` is `"normal"`. Never less than `minimum` (default 0) before a save halves it. One roll serves everyone the step lands on. Ends the conditions that end on `damage`. |
| `heal` | `dice` | Gives HP back up to the maximum. Getting someone up from 0 ends the conditions that end on `healed`. |
| `tempHp` | `dice` | Temporary HP; the larger amount stays, they never add up. |
| `condition` | `id`, `remove`, `duration`, `value` | Applies a condition for `duration` rounds (default: the condition's own; -1 until something ends it) at `value`, or takes it off with `"remove": true`. |
| `modifier` | `stat`, `op`, `value`, `duration`, `id` | A stat change that lasts `duration` rounds (default -1, until removed). It is tracked on the sheet as the condition `effect:<id>` (`id` defaults to the effect's name), so it counts down with the rounds, is saved with the sheet, and is replaced, not doubled, when the same thing is done again. |
| `move` | `how`, `distance` | `push`, `pull` or `teleport` by `distance` squares (default 1; `"speed"` is the doer's speed). Host hook. |
| `resource` | `id`, `op`, `amount` | `spend` (default) or `restore` `amount` (default 1) of a resource, within 0 and its maximum. The host may supply resources that are not on the sheet. |
| `summon` | `id`, `count`, `duration` | Host hook: `count` creatures of that id. |
| `light` | `radius`, `duration` | Host hook: a light on each target, radius in squares. |
| `surface` | `id`, `size`, `duration` | Host hook: an area of that kind on the map. |
| `flag` | `id`, `remove` | Host hook: sets or clears a story flag. |
| `roll` | `kind`, `ability`, `dc`, `against`, `steps` | Makes a roll about each target and then runs `steps` for that target, where `when` tells the results apart. `attack`: the doer's weapon attack against armour class; a natural 1 misses, a natural 20 hits and is critical; ends the doer's conditions that end on `attack`. `check`: the doer rolls `ability` (an ability or skill) against `dc`, the doer's own DC (`"dc": "caster"`), or the target's passive score in `against`. `save`: the target rolls `ability` against `dc`. |
| `repeat` | `times`, `steps` | Runs `steps` that many times (a number or dice, at most 100). |
| `choose` | `options` | Each option has a `name` and `steps`; the host picks one. |

A `save` beside the steps is made once by each creature that a step asks it of (through `onSave` or a save `when`), with `dc` a number or `"caster"`. Results of attacks and checks belong to whoever they were rolled against, so under a roll a step aimed at someone else (`"target": "self", "when": "hit"`) follows that roll.

An unknown step, an unknown or misspelt field, a bad name or a number out of range fails `fromJson`, and the message starts with the field: `effects[1].steps[0].dice: ...`. `check(rules)` then checks what only a ruleset can tell: the conditions, abilities and skills named exist, and a step with `onSave` has a save to answer to. Steps nest up to six deep.

### Actions

An `ActionDefinition` is something a creature can do on its turn: one JSON object, usually a file of its own. `loadActions(files, folder, rules, actions)` adds every `<id>.json` in a folder, replacing actions with the same id, sorts them by `order` and changes nothing if any file fails (the error names it). `basicActions(rules)` gives the three every turn-based fight has, for rulesets with no files: `strike`, `stride` and `end-turn`.

| Field | Meaning |
|---|---|
| `id`, `name`, `description` | Only `id` is required; a file's name must match it. |
| `order` | Where it comes in a list, lowest first. |
| `cost` | Actions it takes, 0 to 10 (default 1), or `"hands"`: one per hand the weapon in use needs. `costFor(character, rules)` works it out. |
| `endsTurn` | The turn is over once it is done. |
| `general` | `true` (the default): every creature has it. Otherwise something has to grant it. |
| `requires` | `flags` the doer must have, flags it must be `without`, and `resources` it must hold at least this much of. `meets(character, rules, &why)` checks them. |
| `target` | `kind` `self` (the default) or `creature`; for a creature, `side` (`enemy`, the default, `ally` or `any`), `range` in squares (1 = next to it), and `downed` (default false) to allow unconscious targets. |
| `readies` | An action id recorded for a later reaction. The folder loader checks that it exists and neither readies another action nor ends the turn. |
| `log` | A line for the game's log; `{name}` is whoever does it. |
| `effects`, `save` | What it does, as an `Effect`. The effect is checked against the ruleset when the folder is loaded. |

Paying for the action, checking the target and running the effect are the game's: the framework supplies the definition and the checks on the doer.

Every effect step also accepts `ifFlag`: it only runs for subjects carrying that condition flag. A missing sheet or a subject without the flag is skipped. For example, a Help action can heal only a `downed` target while its other steps aid any ally.

`run(host, context)` carries the steps out and returns what happened as a list of `EffectEvent`s (rolls with their dice, damage, healing, conditions added, removed or ended...), which the game turns into its log and its floating numbers. `EffectContext` names the ruleset and the dice, who does it, who it was aimed at, its name, the event if any, and the level, slot and DC to use. `EffectHost` is everything an effect needs from the game: `sheet(who)` and `group("area" | "allies" | "enemies", context)` must be supplied; `damage` and `resource` have defaults that work on the sheet (override `damage` for resistances); `move`, `summon`, `light`, `surface`, `flag` and `choose` do nothing until the game supplies them, and a step whose hook returns false reports nothing. Creatures are the host's own numbers (`EffectActor`); one without a sheet is skipped. An attack run with an encounter's dice rolls exactly what `Encounter::attack` would.

`Character::addModifier(id, modifier, rounds)` is the timed modifier on its own.

`Character` includes inventory, equipment, resources, conditions, HP/temp HP, checks and saves. `Encounter` handles initiative, turns, action/movement budgets, attacks, damage and condition durations. Empty encounters and negative movement requests are rejected. Which actions are legal, and the UI flow around them, remain client responsibilities.

### Movement reactions

`ReactionDefinition` names a movement trigger (`leavesReach` or `entersReach`) and an action, or uses `readied: true` to run the action a creature recorded. `matches(before, after, reach)` tests distances on each edge of a movement path. `loadReactions(files, folder, actions, reactions)` reads `<id>.json` files, checks action references, sorts by `order` then id, and changes nothing on an error.

Fields: `id` (required, matching the file), `name` (defaults to id), `trigger` (required), either `action` or `readied: true`, `order` (default 0), and `promptSeconds` (0.1 to 30, default 2). A referenced action must not end the turn or ready another action. The host decides when to offer it and how to show a prompt. `Encounter::useReaction(index)` spends the creature's one reaction, regardless of the action's turn cost; down, withdrawn or `cantAct` creatures cannot react. The budget refreshes on its own turn.

## Text and languages

`Strings` loads `lang/<locale>.json` from the `FileSystem`, so skins and chapters can add or override languages. Nested objects become dotted keys (`menu.start`). `load(files, "pt-BR")` layers the fallback (`en`), then `pt`, then `pt-BR`. Missing keys fall back the same way and finally show the key itself; `missing()` lists them for translators. `get(key, {{"name", "Ana"}})` fills `{name}` placeholders, and `{{`/`}}` are literal braces. `plural(key, count)` picks `zero/one/two/few/many/other` with simplified CLDR integer rules: English-style by default, French/Portuguese treat 0 as one, plus Slavic few/many, Czech/Slovak, Arabic, and no plurals for CJK/Thai/Vietnamese/Indonesian. `preferredLocales()` reads the OS languages.

`RichText::parse` understands `[color=#rrggbb]`/`[color=#rrggbbaa]`/`[color=name]` (from a palette such as the theme's accent/good/bad), `[wave]`, `[shake]`, `[rainbow]` and `[icon=name]`. `[[` is a literal bracket. Unknown or badly nested tags show as typed. Escape player-typed text with `RichText::escape` so chat and names can't inject tags. `layout(font, maxWidth)` wraps at spaces across style changes and keeps kerning inside words. `draw(renderer, position, baseColour, time, iconDrawer)` animates effects from the clock you pass, and icons draw through a callback (usually `Atlas::draw`). The debug font works when `font` is null.

## Saves and undo

`SaveFormat("yorehold.save", 3)` wraps client data in `{"format", "version", "framework", "data"}`. Register `migrate(from, step)` for every older version. Steps edit `nlohmann::json` in place and throw to reject a save. `read` refuses other formats and newer saves, runs framework migrations first and then client steps up to the current version, and changes nothing if any step fails. `frameworkSaveVersion` tracks the shape of the framework's own snapshots. `writeFile` writes through a temporary file and keeps the previous save as `.bak`; `readFile` falls back to the backup when the main file is missing or corrupt. `writeFileAtomically` does the same for any text.

`History` records undo/redo as pairs of callbacks that capture their old and new values. `perform` applies and records an edit; `record` stores one that is already applied. `beginGroup`/`endGroup` make one step out of many edits. A merge key joins consecutive edits (one brush stroke, one slider drag) until `breakMerge()`. Edits are refused while undoing or redoing. `dirty()`/`markSaved()` track unsaved changes, including after undoing past a save and then branching. The default limit is 200 steps.

## Multiplayer sessions

`Transport` moves reliable, ordered messages and reports Connected/Disconnected/Message events from `poll()`. Both sides get a Disconnected event, including the side that disconnected. `LoopbackHub` runs host and clients in one process (tests, local play). `TcpTransport::listen(port)` and `connect(host, port)` use non-blocking sockets with length-prefixed messages up to 16 MB. `listen(0, …, true)` binds an ephemeral port on this machine only (no firewall prompt). TCP isn't available in browser builds, which will need a WebSocket/WebRTC transport.

`SessionHost` and `SessionClient` are host-authoritative. Clients send *intents*. The host's `Rules::validate` accepts or rejects each one and may fill in host-side results such as dice. Accepted intents become numbered *commands*, which the host and every client apply in the same order, so all copies match. Joiners send their game name and version (mismatches are refused, and `admit` can refuse others), then receive `snapshot()` and every later command. An optional `checksum` after each command catches desyncs and disconnects the client. Heartbeats run every 2 s and silent peers drop after 15 s. `kick` and `leave` give reasons, and `disconnected` reports exactly one reason. The host applies its own intents through the same rules, and submitting from inside `apply` is refused so ordering can't diverge.

## Builds and verification

The Windows build passed **2,837 regression checks** (2026-10-04). GPU verification covers the new image scene and existing map, token and world scenes; the previous full sweep covered the original 14 framework scenes and client integration. [VALIDATION.md](VALIDATION.md) records the environment and repeatable interaction scripts.

Use the neighboring client build to verify library integration:

```powershell
cmake -S ../yorehold -B ../yorehold/out -G "Visual Studio 18 2026" -A x64
cmake --build ../yorehold/out --config RelWithDebInfo --parallel 4
ctest --test-dir ../yorehold/out -C RelWithDebInfo --output-on-failure
../yorehold/out/bin/RelWithDebInfo/yorehold-framework-tests.exe --list
```

`YOREHOLD_FRAMEWORK_TESTS` controls the visual browser; `YOREHOLD_FRAMEWORK_UNIT_TESTS` controls the CPU regression target. CTest uses release-enabled checks, so optimized builds still validate behavior. Tests cover tween timing/reentrancy, seeded emission and capacity, wall visibility, fog persistence, sparse map saves/deltas, navigation changes, object interactions/throws, region freezing/co-op/restores, party links/turns/floors, input focus, layered disk/zip files, theme/ruleset/sheet parsing, audio mixing, screen lifecycles, save migrations/backups, undo groups/merges/dirty state, loopback and TCP sessions (joins, rejections, version refusal, desync, kicks, timeouts), text selection/clipboard rules/composition, gamepad axes/buttons/hot-unplug, languages/plurals, templates/rulers, atlas packing/padding, sprite clips/events, rich-text parsing/wrapping and the profiler.

The visual browser contains Shapes, Sprite stress, Input, M1 Camera, M2 Grid, M3 Tile map, M4 Tokens, M5 Sheet, M6 Combat, F2 Skins + text, F4 Animation + particles, F5 Lighting + fog, F6 World + UI + audio, F7 Tools + text, and F8 Images. The image scene draws JPEG tiles and PNG decorations with portrait/cutout tokens; drop a supported image onto it to use your own art. Z toggles distant overview rendering. Scripts can drive `move`, `down`, `up`, `key`, `keyup`, `wheel`, `text` and `shot`. Key names use underscores for spaces (`key Left_Ctrl`), and `--fixed-dt` supplies reproducible timing. `tests/visual/scripts/tools.txt` exercises painting and undo/redo, the ruler, line templates, the attack event, text selection, switching language and F3. `tests/visual/scripts/images.txt` captures image tokens and near/far map rendering.

| Platform | Implementation | Validation in this workspace |
|---|---|---|
| Windows | SDL HWND, pinned Dawn binary, D3D12 runtime DLLs | Compiler, CPU tests and GTX 1080 GPU scenes |
| Linux | SDL Xlib/Wayland surface, native Dawn package/source | SDK build and runtime unverified |
| macOS/iOS | SDL Metal view/layer, native Dawn, iOS bundle target | Apple SDK build and runtime unverified |
| Android | SDL native window, shared-library app targets, native Dawn | NDK build, APK packaging and runtime unverified |
| Browser | Emscripten `emdawnwebgpu`, Asyncify loop, preloaded framework assets | Emscripten build and browser runtime unverified |

For native source builds, set `YOREHOLD_DAWN_FROM_SOURCE=ON`; an installed `Dawn_DIR` takes precedence. Native Dawn source is pinned to the same commit as the Windows package. Mobile consumers must provide the SDL application wrapper/signing/package configuration appropriate to their SDK.

For web builds, configure with `emcmake cmake -S . -B out-web -DBUILD_TESTING=OFF -DYOREHOLD_FRAMEWORK_UNIT_TESTS=OFF`, then build and serve the generated `.html/.js/.wasm/.data` from localhost or HTTPS. `YOREHOLD_WEBGPU_PORT` accepts the port name or a pinned port file. The default port can change its C++ API, so use a matching port when compiling this revision. See the primary [Dawn port instructions](https://dawn.googlesource.com/dawn/+/refs/heads/main/src/emdawnwebgpu/pkg/README.md), [native CMake guide](https://dawn.googlesource.com/dawn/+/refs/heads/main/docs/quickstart-cmake.md), and [Emscripten runtime guide](https://emscripten.org/docs/porting/emscripten-runtime-environment.html).
