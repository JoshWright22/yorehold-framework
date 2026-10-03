#pragma once

#include "yorehold/framework/graphics/Camera.h"
#include "yorehold/framework/graphics/Renderer.h"
#include "yorehold/framework/input/Input.h"
#include "yorehold/framework/map/Grid.h"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace yh
{

class Assets;
class Font;

enum class TokenImageStyle
{
    Portrait, // the picture's centre cut to a circle inside a ring of the token's colour
    Cutout,   // drawn whole, keeping its shape: top-down token art with a transparent background
};

struct Token
{
    std::string name;
    Vec2 position;       // world units, centre
    float radius = 40;   // world units
    Color color{200, 200, 200, 255};
    int owner = 0;       // player who controls it
    bool selected = false;
    std::vector<Vec2> path; // remaining waypoints while walking
    int floor = 0;
    // Virtual path of any image file (PNG, JPEG, WebP, ...). Empty draws a coloured disc with the initial.
    std::string image;
    TokenImageStyle imageStyle = TokenImageStyle::Portrait;
    float imageScale = 1.0f; // Cutout art bigger or smaller than the token's footprint
};

enum class DragSnap
{
    OnDrop, // moves smoothly with the mouse, snaps when let go
    Live,   // jumps square to square while dragging
};

struct TokenSettings
{
    DragSnap dragSnap = DragSnap::OnDrop;
    float walkCellsPerSecond = 5.0f;
    // Walk around party members instead of through them. Enemies (other owners) always block.
    bool avoidAllies = true;
    float followDistanceCells = 1.5f;
    // Followers standing idle in a party member's way step aside instead of blocking them.
    bool alliesMakeWay = true;
    bool inCombat = false;
    std::optional<size_t> activeTurn; // in combat, only this token may move
};

// Selecting and moving tokens with either control scheme:
// click/shift-click/box-select, drag to move (snapped), click the ground to walk there along a path,
// and group moves that keep the party's formation.
class TokenController
{
public:
    using Passable = std::function<bool(Cell)>;

    TokenSettings settings;
    std::vector<Token> tokens;
    int localPlayer = 0;
    int viewedFloor = 0;
    ~TokenController();
    TokenController() = default;
    TokenController(const TokenController&) = delete;
    TokenController& operator=(const TokenController&) = delete;

    // Indices remain valid while the caller keeps tokens in the same order. Links cannot
    // cycle or cross owners. Call clearLinks() before removing/reordering tokens.
    bool link(size_t follower, size_t leader);
    void unlink(size_t follower);
    void clearLinks();
    std::optional<size_t> follows(size_t follower) const;
    void setFloor(size_t token, int floor, bool includeFollowers = true);

    // Turns a token's image into a texture. Without one, tokens with images draw as discs.
    std::function<TextureId(const Token&)> images;
    // Looks token images up through `assets` (which must outlive the controller): portraits
    // as circle textures, cutouts as smooth textures.
    void useAssets(Assets& assets);
    // Initials on image-less tokens, and the distance label + context menu; null uses the
    // debug font. Must outlive draw calls.
    Font* initialFont = nullptr;
    Font* labelFont = nullptr;

    void update(const Input& input, const Camera& camera, const Grid& grid, const Passable& passable, double deltaSeconds);
    // Draws through the camera transform (call between camera.apply and renderer.pop).
    void draw(Renderer& renderer, const Camera& camera, const Grid& grid);
    // Screen-space extras: selection box and the "N squares" label.
    void drawOverlay(Renderer& renderer, const Camera& camera, const Grid& grid);

    // The first selected token.
    std::optional<Vec2> leaderPosition() const;
    // What the camera should follow: the leader, except while dragging (following then would slide the map under the mouse).
    std::optional<Vec2> followTarget() const { return gesture_ == Gesture::DraggingTokens ? std::nullopt : leaderPosition(); }
    // Client-provided labels keep game-specific actions out of the framework. A chosen
    // item is reported for this update; the client handles its gameplay consequence.
    std::vector<std::string> contextActions{"Inspect"};
    std::optional<std::pair<size_t, std::string>> contextChoice;
    std::optional<size_t> contextMenuToken;

private:
    std::optional<size_t> tokenAt(Vec2 world) const;
    bool controllable(const Token& token) const { return token.owner == localPlayer; }
    void moveSelectionTo(Cell target, const Grid& grid, const Passable& passable);
    // passable plus the cells other creatures stand in, as seen by mover.
    Passable aroundCreatures(const Passable& passable, const Grid& grid, const Token& mover) const;
    void walk(double deltaSeconds, const Grid& grid);
    void followParty(const Grid& grid, const Passable& passable);
    void makeWay(const Grid& grid, const Passable& passable);
    // A follower that will step aside rather than block mover.
    bool makesWayFor(size_t index, const Token& mover) const;
    bool canMove(size_t index) const;
    void ensureTextures(Renderer& renderer);
    void drawImage(Renderer& renderer, const Token& token);

    enum class Gesture { None, PressedToken, DraggingTokens, PressedGround, BoxSelecting };
    Gesture gesture_ = Gesture::None;
    Vec2 pressWorld_;
    std::vector<Vec2> dragStart_; // token positions when the drag began
    Rect box_;                    // screen space
    Rect menuArea_;
    Vec2 lastMouse_; // screen space, for menu hover

    // Hover path preview, recomputed only when the hovered cell changes.
    std::optional<Cell> previewCell_;
    std::vector<Cell> previewPath_;

    TextureId disc_ = 0;
    TextureId ring_ = 0;
    bool texturesReady_ = false;
    std::function<void()> releaseDisc_, releaseRing_;
    std::map<size_t, size_t> links_;
    std::map<size_t, Cell> followerTargets_;
    std::set<size_t> steppingAside_;
};

}
