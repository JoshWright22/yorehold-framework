# Framework design

The framework is a generic RPG framework: Foundry VTT's map, layers and tokens meet BG3's exploring, objects and throws.
Nothing in here is Yorehold-specific; rules, content and UI flow live in the client.

Decisions are dated. Open questions are at the bottom.

## Controls (decided 2026-10-02)

- **Several control schemes, all rebindable.** Input goes through named actions (`pan`, `select`, `moveTo`, `dragToken`, `zoomIn`...) and a scheme maps buttons to actions. Ship presets:
  - **Foundry:** right-drag pans, left-click selects, left-drag moves a token.
  - **BG3:** left-click selects, then left-click the ground to walk there along a path; right-click opens the context menu; WASD, middle-drag or edge-scroll pans.
  - Players can mix and change any binding in settings.
- **Moving characters:** both drag-the-token and select-then-click-to-go work in every scheme. Click-to-go walks straight away (no confirm click).
- **Group moves:** box-select works when you control more than one character; the group moves together.
- **Grid (2026-10-02):** works like Foundry: a flat 2D grid (no height axis for movement), drawn or hidden with a toggle. Dragged tokens snap to the grid.
  The framework supports square, hex and gridless maps behind one interface (cell lookup, neighbours, distance, snapping); Yorehold uses square.
- **Movement measuring:** the framework handles both grid squares and free distance; Yorehold counts in squares.
- **Party movement:** out of combat, characters follow like BG3 (companions trail whoever they're linked to). In combat, one character moves at a time on its turn.
- **Camera:** follow-or-stay is a setting. Default copies BG3: the camera follows the selected character while it moves, you can pan away freely, and a key (Home) snaps back.
- **Zoom:** zooms toward the **screen center by default**; "zoom toward cursor" is a setting. Max zoom-out is capped (default: whole map fits the screen; the cap is a setting). Zoom is smooth (eased), not stepped.

- **Drag snapping:** a setting with both modes: jump square to square while dragging, or move smoothly and snap on drop.
- **Party links:** exactly like BG3: link/unlink characters into chains, and each co-op player leads their own group.

## Maps (decided 2026-10-02)

- **Floors are layers of the same map**, not separate maps. Each floor level has its own ground, walls and objects; stairs/ladders move tokens between levels; levels above the viewer's fade or cut away (like BG3 hiding roofs).
- **Art comes from players** (custom images and tiles), plus a starter art set shipped with the first release.
- **Objects use tags**, any number per object (a crate can be throwable + destructible + container). Under the hood each tag is a component with its own few settings (container: contents; throwable: weight; light source: radius, colour). Map makers only see checkboxes and plain fields.
- **No programming for map makers.** Behaviour is packaged into reusable, shareable building blocks called **Kits**: a lever, a pressure-plate trap, a locked chest. Anyone can make one from art + tags + plain settings and publish it for other map makers.
- **Links are tags too**, not target fields: doors get tags like `door`, `locked`, and opening rules come from tags (proposed: a lever and a door sharing a key tag such as `key:red` are linked).
- **Map art: both, tiles first.** Tile painting is the main way to build maps in the game; whole painted images (Foundry style) are also supported as a floor layer.

## World size (decided 2026-10-02)

There's no single shared world map. Each author makes their own areas, and one author may want a huge Skyrim/BotW-sized one (up to ~10 x 10 km, ~7,000 x 7,000 squares at 5 ft).

- **Regions, not seamless (2026-10-02):** a big area is split into regions joined by transitions (doorways, map edges, fast travel), like BG3's areas. Easier for map makers (build and test one region at a time) and simpler to load. Inside a region, chunks (e.g. 64 x 64 squares) are still used for drawing and culling.
- **Split-up co-op:** players can be in different regions at once; every region with a player in it is loaded and running.
- **Nobody nearby = frozen:** regions with no players don't simulate; their state is saved and picks up when someone returns (like BG3).
- **Sparse storage:** only store what was painted/placed; empty or uniform ground costs nothing. Saves store changes on top of the authored map.
- **Precision:** positions are chunk + local offset (or doubles), so things far from the origin don't jitter.
- **Pathfinding:** hierarchical (path between chunks first, then inside them), so long walks stay cheap.
- **Zoomed-out view:** low-detail versions of chunks for the world map / far zoom.
- **Editor:** edits stream too; map makers work on one area without loading the rest.

## Framework pieces this needs

- Input actions + bindings + saved control schemes (`yh::input`)
- Camera with eased pan/zoom, zoom anchor setting, zoom limits from map bounds (`yh::gfx` / camera)
- Pathfinding (grid A*) and a path preview helper, shared by drag and click-to-move

## Open questions

- Confirm shared key tags (`key:red`) as the lever/door/pressure-plate link.
- Tiles: square tiles that match the grid, auto-joining walls/edges (autotiling), tile size?
- Object tags beyond: blocks movement, blocks sight, throwable, pickupable, interactable, destructible, container, door, trap, light source?
- Surfaces (fire, water, grease, ice)?
- Fog: per-player view or shared party view? Explored areas remembered?
- Lighting: day/night + darkvision, or lit/unlit?
- Throws: hit along the arc or only at the landing spot?
- Editor: built into the client or separate tool? Foundry-style layer toolbar or Photoshop-style layer list?
- View: straight overhead or slight angle?
- Art style: pixel, painted, or skin-decided?
