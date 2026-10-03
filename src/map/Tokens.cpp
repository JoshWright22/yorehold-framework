#include "yorehold/framework/map/Tokens.h"

#include "yorehold/framework/assets/Assets.h"
#include "yorehold/framework/graphics/Font.h"
#include "yorehold/framework/input/ControlScheme.h"
#include "yorehold/framework/map/Pathfinding.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <unordered_set>

namespace yh
{

namespace
{

constexpr Color selectionColor{255, 196, 64, 255};
constexpr Color previewColor{255, 255, 255, 140};
constexpr float portraitInnerFraction = 0.88f;

Rect centeredRect(Vec2 center, Vec2 size)
{
    return {center.x - size.x / 2, center.y - size.y / 2, size.x, size.y};
}

float length(Vec2 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

// White disc (or ring) with a soft one-pixel edge, so tinted tokens look round at any zoom.
std::vector<unsigned char> makeCircle(int size, float innerFraction)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(size) * size * 4, 255);
    const float r = size / 2.0f;
    for (int y = 0; y < size; y++)
    {
        for (int x = 0; x < size; x++)
        {
            const float d = length({x + 0.5f - r, y + 0.5f - r});
            float alpha = std::clamp(r - d, 0.0f, 1.0f);
            if (innerFraction > 0)
                alpha = std::min(alpha, std::clamp(d - r * innerFraction, 0.0f, 1.0f));
            pixels[(static_cast<size_t>(y) * size + x) * 4 + 3] = static_cast<unsigned char>(alpha * 255);
        }
    }
    return pixels;
}

}

void TokenController::useAssets(Assets& assets)
{
    images = [&assets](const Token& token) {
        if (token.imageStyle == TokenImageStyle::Portrait)
            return assets.circleTexture(token.image);
        return assets.texture(token.image, true);
    };
}

TokenController::~TokenController()
{
    if (releaseDisc_) releaseDisc_();
    if (releaseRing_) releaseRing_();
}

bool TokenController::link(size_t follower, size_t leader)
{
    if (follower >= tokens.size() || leader >= tokens.size() || follower == leader || tokens[follower].owner != tokens[leader].owner)
        return false;
    size_t parent = leader;
    for (size_t visits = 0; visits <= links_.size(); ++visits)
    {
        if (parent == follower) return false;
        const auto it = links_.find(parent);
        if (it == links_.end()) break;
        parent = it->second;
    }
    links_[follower] = leader;
    followerTargets_.erase(follower);
    return true;
}

void TokenController::unlink(size_t follower)
{
    links_.erase(follower);
    followerTargets_.erase(follower);
    if (follower < tokens.size()) tokens[follower].path.clear();
}

void TokenController::clearLinks()
{
    for (const auto& [follower, leader] : links_)
    {
        (void)leader;
        if (follower < tokens.size()) tokens[follower].path.clear();
    }
    links_.clear(); followerTargets_.clear();
}

std::optional<size_t> TokenController::follows(size_t follower) const
{
    const auto it = links_.find(follower);
    return it == links_.end() ? std::nullopt : std::optional(it->second);
}

void TokenController::setFloor(size_t token, int floor, bool includeFollowers)
{
    if (token >= tokens.size()) return;
    tokens[token].floor = floor; tokens[token].path.clear();
    if (includeFollowers)
        for (const auto& [follower, leader] : links_)
            if (leader == token) setFloor(follower, floor, true);
    followerTargets_.clear(); previewCell_.reset(); previewPath_.clear();
}

bool TokenController::canMove(size_t index) const
{
    return index < tokens.size() && controllable(tokens[index]) && tokens[index].floor == viewedFloor
        && (!settings.inCombat || settings.activeTurn == index);
}

std::optional<size_t> TokenController::tokenAt(Vec2 world) const
{
    // Last drawn is on top, so search backwards.
    for (size_t i = tokens.size(); i-- > 0;)
    {
        if (tokens[i].floor == viewedFloor && length(world - tokens[i].position) <= tokens[i].radius)
            return i;
    }
    return std::nullopt;
}

std::optional<Vec2> TokenController::leaderPosition() const
{
    for (const Token& token : tokens)
    {
        if (token.selected && token.floor == viewedFloor && controllable(token))
            return token.position;
    }
    return std::nullopt;
}

void TokenController::update(const Input& input, const Camera& camera, const Grid& grid, const Passable& passable, double deltaSeconds)
{
    contextChoice.reset();
    lastMouse_ = input.mouse();
    if (contextMenuToken && input.buttonPressed(MouseButton::Left) && !menuArea_.contains(input.mouse())) contextMenuToken.reset();
    if (contextMenuToken && menuArea_.contains(input.mouse()))
    {
        if (input.buttonClicked(MouseButton::Left))
        {
            const int row = static_cast<int>((input.mouse().y - menuArea_.y - 8) / 22) - 1;
            if (row >= 0 && static_cast<size_t>(row) < contextActions.size() && *contextMenuToken < tokens.size())
                contextChoice = std::pair{*contextMenuToken, contextActions[static_cast<size_t>(row)]};
            contextMenuToken.reset();
        }
        gesture_ = Gesture::None;
        followParty(grid, passable); walk(deltaSeconds, grid);
        return;
    }
    if (!input.down(actions::select) && !input.released(actions::select) && gesture_ != Gesture::None)
        gesture_ = Gesture::None; // window lost focus during a drag
    const Vec2 mouseWorld = camera.screenToWorld(input.mouse());
    const bool adding = input.down(actions::addToSelection);
    const std::optional<size_t> hovered = tokenAt(mouseWorld);
    const bool anySelected = leaderPosition().has_value();

    if (input.pressed(actions::select))
    {
        contextMenuToken.reset();
        pressWorld_ = camera.screenToWorld(input.pressPosition(actions::select));
        const std::optional<size_t> hit = tokenAt(pressWorld_);
        if (hit && controllable(tokens[*hit]))
        {
            if (!tokens[*hit].selected)
            {
                if (!adding)
                {
                    for (Token& token : tokens)
                        token.selected = false;
                }
                tokens[*hit].selected = true;
            }
            gesture_ = Gesture::PressedToken;
        }
        else
        {
            gesture_ = Gesture::PressedGround;
        }
    }

    if (gesture_ == Gesture::PressedToken && input.dragging(actions::select))
    {
        gesture_ = Gesture::DraggingTokens;
        dragStart_.clear();
        for (Token& token : tokens)
        {
            dragStart_.push_back(token.position);
            if (token.selected)
                token.path.clear();
        }
    }
    if (gesture_ == Gesture::DraggingTokens)
    {
        const Vec2 delta = mouseWorld - pressWorld_;
        for (size_t i = 0; i < tokens.size(); i++)
        {
            if (!tokens[i].selected || !canMove(i) || i >= dragStart_.size())
                continue;
            const Vec2 moved = dragStart_[i] + delta;
            tokens[i].position = settings.dragSnap == DragSnap::Live ? grid.snap(moved) : moved;
        }
    }

    if (gesture_ == Gesture::PressedGround && input.dragging(actions::select))
        gesture_ = Gesture::BoxSelecting;
    if (gesture_ == Gesture::BoxSelecting)
    {
        const Vec2 a = input.pressPosition(actions::select);
        const Vec2 b = input.mouse();
        box_ = {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(a.x - b.x), std::abs(a.y - b.y)};
    }

    // Clicking the ground walks the selection there (left in BG3, right in Foundry).
    const bool groundClick = input.clicked(actions::moveTo) && !hovered;
    if (groundClick && anySelected && gesture_ != Gesture::DraggingTokens && gesture_ != Gesture::BoxSelecting)
    {
        const Cell target = grid.cellAt(mouseWorld);
        if (passable(target))
        {
            moveSelectionTo(target, grid, passable);
            previewCell_.reset(); // the route changed for everyone
        }
    }

    if (input.clicked(actions::contextMenu) && hovered)
        contextMenuToken = hovered;

    if (input.released(actions::select))
    {
        switch (gesture_)
        {
        case Gesture::DraggingTokens:
            for (size_t i = 0; i < tokens.size(); i++)
            {
                if (!tokens[i].selected || !canMove(i) || i >= dragStart_.size())
                    continue;
                const Vec2 snapped = grid.snap(tokens[i].position);
                // Dropped somewhere you can't stand: go back.
                tokens[i].position = passable(grid.cellAt(snapped)) ? snapped : dragStart_[i];
            }
            break;
        case Gesture::BoxSelecting:
            for (Token& token : tokens)
            {
                const bool inside = box_.contains(camera.worldToScreen(token.position));
                if (inside && controllable(token) && token.floor == viewedFloor)
                    token.selected = true;
                else if (!adding)
                    token.selected = false;
            }
            break;
        case Gesture::PressedGround:
            if (!groundClick && !adding)
            {
                for (Token& token : tokens)
                    token.selected = false;
            }
            break;
        default:
            break;
        }
        gesture_ = Gesture::None;
    }

    // Path preview under the mouse, only recomputed when the hovered cell changes.
    const std::optional<Vec2> leader = leaderPosition();
    const Cell hoverCell = grid.cellAt(mouseWorld);
    if (!leader || hovered || gesture_ != Gesture::None || !passable(hoverCell))
    {
        previewCell_.reset();
        previewPath_.clear();
    }
    else if (!previewCell_ || *previewCell_ != hoverCell)
    {
        previewCell_ = hoverCell;
        const Token* leaderToken = nullptr;
        for (const Token& token : tokens)
        {
            if (token.selected && token.floor == viewedFloor && controllable(token) && !leaderToken)
                leaderToken = &token;
        }
        previewPath_ = findPath(grid, grid.cellAt(*leader), hoverCell, aroundCreatures(passable, grid, *leaderToken), 20000);
    }

    followParty(grid, passable);
    walk(deltaSeconds, grid);
}

TokenController::Passable TokenController::aroundCreatures(const Passable& passable, const Grid& grid, const Token& mover) const
{
    auto blocked = std::make_shared<std::unordered_set<Cell, CellHash>>();
    for (const Token& other : tokens)
    {
        if (other.floor != mover.floor || &other == &mover || (other.selected && other.owner == mover.owner))
            continue; // the moving group doesn't block itself; destinations are spread out instead
        const bool ally = other.owner == mover.owner;
        if (!ally || settings.avoidAllies)
            blocked->insert(grid.cellAt(other.path.empty() ? other.position : other.path.back()));
    }
    return [passable, blocked](Cell c) { return passable(c) && !blocked->contains(c); };
}

void TokenController::moveSelectionTo(Cell target, const Grid& grid, const Passable& passable)
{
    // The leader goes to the clicked cell; everyone else takes the nearest free cells around it.
    std::unordered_set<Cell, CellHash> taken;
    for (const Token& token : tokens)
    {
        if (!token.selected && token.floor == viewedFloor)
            taken.insert(grid.cellAt(token.path.empty() ? token.position : token.path.back()));
    }

    std::vector<Cell> neighbours;
    for (Token& token : tokens)
    {
        if (!token.selected || !canMove(static_cast<size_t>(&token - tokens.data())))
            continue;

        // Breadth-first search outward from the target for a free, standable cell.
        std::optional<Cell> destination;
        std::deque<Cell> queue{target};
        std::unordered_set<Cell, CellHash> seen{target};
        while (!queue.empty() && seen.size() < 400)
        {
            const Cell cell = queue.front();
            queue.pop_front();
            if (passable(cell) && !taken.contains(cell))
            {
                destination = cell;
                break;
            }
            grid.neighbours(cell, neighbours);
            for (const Cell next : neighbours)
            {
                if (seen.insert(next).second)
                    queue.push_back(next);
            }
        }
        if (!destination)
            continue;

        const std::vector<Cell> path = findPath(grid, grid.cellAt(token.position), *destination, aroundCreatures(passable, grid, token));
        if (path.empty())
            continue;
        taken.insert(*destination);
        token.path.clear();
        for (size_t i = 1; i < path.size(); i++)
            token.path.push_back(grid.center(path[i]));
        if (path.size() == 1)
            token.path.push_back(grid.center(path[0]));
    }
}

void TokenController::walk(double deltaSeconds, const Grid& grid)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0) return;
    for (Token& token : tokens)
    {
        if (settings.inCombat && settings.activeTurn != static_cast<size_t>(&token - tokens.data())) continue;
        float step = settings.walkCellsPerSecond * grid.size() * static_cast<float>(deltaSeconds);
        while (step > 0 && !token.path.empty())
        {
            const Vec2 toNext = token.path.front() - token.position;
            const float distance = length(toNext);
            if (distance <= step)
            {
                token.position = token.path.front();
                token.path.erase(token.path.begin());
                step -= distance;
            }
            else
            {
                token.position = token.position + toNext * (step / distance);
                step = 0;
            }
        }
    }
}

void TokenController::followParty(const Grid& grid, const Passable& passable)
{
    if (settings.inCombat) return;
    const float spacing = std::max(1.0f, settings.followDistanceCells);
    for (const auto& [follower, leader] : links_)
    {
        if (follower >= tokens.size() || leader >= tokens.size()) continue;
        Token& child = tokens[follower];
        const Token& parent = tokens[leader];
        if (!controllable(child) || child.selected || child.floor != parent.floor || child.floor != viewedFloor) continue;
        const Cell from = grid.cellAt(child.position), to = grid.cellAt(parent.position);
        if (grid.distance(from, to) <= spacing)
        {
            child.path.clear(); followerTargets_.erase(follower); continue;
        }
        const auto planned = followerTargets_.find(follower);
        if (planned != followerTargets_.end() && planned->second == to && !child.path.empty()) continue;
        followerTargets_[follower] = to;
        const auto route = findPath(grid, from, to, passable, 20000);
        child.path.clear();
        for (size_t i = 1; i < route.size() && grid.distance(route[i], to) >= spacing; ++i)
            child.path.push_back(grid.center(route[i]));
    }
}

void TokenController::ensureTextures(Renderer& renderer)
{
    if (texturesReady_)
        return;
    const std::vector<unsigned char> disc = makeCircle(128, 0);
    const std::vector<unsigned char> ring = makeCircle(128, 0.86f);
    disc_ = renderer.createTexture(128, 128, disc.data());
    ring_ = renderer.createTexture(128, 128, ring.data());
    releaseDisc_ = renderer.textureRelease(disc_);
    releaseRing_ = renderer.textureRelease(ring_);
    texturesReady_ = true;
}

void TokenController::draw(Renderer& renderer, const Camera& camera, const Grid& grid)
{
    ensureTextures(renderer);

    // Hover preview: a dot in each cell along the way.
    const float dot = grid.size() * 0.18f;
    for (size_t i = 1; i < previewPath_.size(); i++)
    {
        const Vec2 c = grid.center(previewPath_[i]);
        renderer.drawSprite(disc_, {c.x - dot / 2, c.y - dot / 2, dot, dot}, previewColor);
    }
    if (!previewPath_.empty())
    {
        const Vec2 end = grid.center(previewPath_.back());
        const float r = grid.size() * 0.4f;
        renderer.drawSprite(ring_, {end.x - r, end.y - r, r * 2, r * 2}, previewColor);
    }

    // Where walking tokens are headed.
    for (const Token& token : tokens)
    {
        if (token.floor != viewedFloor || token.path.empty())
            continue;
        Vec2 from = token.position;
        for (const Vec2& to : token.path)
        {
            renderer.drawLine(from, to, {token.color.r, token.color.g, token.color.b, 150}, 3.0f / camera.zoom());
            from = to;
        }
    }

    for (const Token& token : tokens)
    {
        if (token.floor != viewedFloor) continue;
        const float r = token.radius;
        if (token.selected)
        {
            const float rr = r * 1.18f;
            renderer.drawSprite(ring_, {token.position.x - rr, token.position.y - rr, rr * 2, rr * 2}, selectionColor);
        }
        if (!token.image.empty() && images)
        {
            drawImage(renderer, token);
            continue;
        }
        renderer.drawSprite(disc_, {token.position.x - r, token.position.y - r, r * 2, r * 2}, {20, 20, 24, 255});
        const float inner = r * 0.9f;
        renderer.drawSprite(disc_, {token.position.x - inner, token.position.y - inner, inner * 2, inner * 2}, token.color);

        const std::string initial = token.name.substr(0, 1);
        if (initialFont)
        {
            initialFont->drawCentered(renderer, {token.position.x - r, token.position.y - r, r * 2, r * 2}, initial, {20, 20, 24, 255});
            continue;
        }
        const float scale = r / 8.0f;
        renderer.drawText({token.position.x - Renderer::textWidth(initial, scale) / 2, token.position.y - 4 * scale}, initial, {20, 20, 24, 255}, scale);
    }
}

void TokenController::drawImage(Renderer& renderer, const Token& token)
{
    const TextureId texture = images(token);
    const float diameter = token.radius * 2;
    if (token.imageStyle == TokenImageStyle::Portrait)
    {
        renderer.drawSprite(disc_, centeredRect(token.position, {diameter, diameter}), token.color);
        const float portraitDiameter = diameter * portraitInnerFraction;
        renderer.drawSprite(texture, centeredRect(token.position, {portraitDiameter, portraitDiameter}));
        return;
    }

    float imageScale = token.imageScale;
    if (!std::isfinite(imageScale) || imageScale <= 0) imageScale = 1.0f;

    // Fit the complete cutout into its footprint. Scale the art, without changing movement or hit testing.
    const Vec2 textureSize = renderer.textureSize(texture);
    const float longestSide = std::max({textureSize.x, textureSize.y, 1.0f});
    const float fitScale = diameter * imageScale / longestSide;
    const Vec2 drawnSize = textureSize * fitScale;
    renderer.drawSprite(texture, centeredRect(token.position, drawnSize));
}

void TokenController::drawOverlay(Renderer& renderer, const Camera& camera, const Grid& grid)
{
    if (gesture_ == Gesture::BoxSelecting)
    {
        renderer.fillRect(box_, {255, 196, 64, 40});
        renderer.drawRect(box_, selectionColor, 1.5f);
    }

    auto text = [&](Vec2 at, std::string_view s, Color color) {
        if (labelFont) labelFont->draw(renderer, at, s, color);
        else renderer.drawText(at, s, color);
    };
    auto textWidth = [&](std::string_view s) { return labelFont ? labelFont->measure(s) : Renderer::textWidth(s); };
    const float textHeight = labelFont ? labelFont->lineHeight() : Renderer::lineHeight();

    if (previewPath_.size() > 1)
    {
        const float squares = grid.distance(previewPath_.front(), previewPath_.back());
        char label[48];
        std::snprintf(label, sizeof(label), "%.0f sq (%.0f ft)", squares, squares * 5);
        const Vec2 at = camera.worldToScreen(grid.center(previewPath_.back())) + Vec2{18, -30};
        renderer.fillRect({at.x - 6, at.y - 4, textWidth(label) + 12, textHeight + 6}, {0, 0, 0, 190});
        text(at, label, {255, 255, 255, 255});
    }

    if (contextMenuToken && *contextMenuToken < tokens.size())
    {
        const Token& token = tokens[*contextMenuToken];
        const Vec2 at = camera.worldToScreen(token.position) + Vec2{token.radius * camera.zoom() + 10, -20};
        menuArea_ = {at.x, at.y, 220, static_cast<float>(contextActions.size() + 1) * 22.0f + 12};
        renderer.fillRect(menuArea_, {30, 28, 36, 240});
        for (size_t i = 0; i < contextActions.size(); ++i)
        {
            const Rect row{at.x + 3, at.y + 8 + static_cast<float>(i + 1) * 22.0f, menuArea_.w - 6, 22};
            if (row.contains(lastMouse_))
                renderer.fillRect(row, {80, 70, 100, 255});
            text({at.x + 10, row.y}, contextActions[i], {230, 226, 214, 255});
        }
        renderer.drawRect(menuArea_, {8, 7, 10, 255}, 3);
        text({at.x + 10, at.y + 6}, token.name, selectionColor);
    }
}

}
