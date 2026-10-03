#include "yorehold/framework/map/Objects.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace yh
{
namespace
{
using Json = nlohmann::json;

Json encode(const MapObject& o)
{
    Json j{{"id", o.id}, {"name", o.name}, {"texture", o.texture}, {"area", {o.area.x, o.area.y, o.area.w, o.area.h}},
        {"floor", o.floor}, {"tags", o.tags}, {"weight", o.weight}, {"destroyed", o.destroyed}, {"contents", o.contents}};
    if (o.door) j["door"] = {{"open", o.door->open}, {"locked", o.door->locked}};
    if (o.durability) j["durability"] = {{"health", o.durability->health}, {"maximum", o.durability->maximum}};
    if (o.flight) j["flight"] = {{"from", {o.flight->from.x, o.flight->from.y}}, {"to", {o.flight->to.x, o.flight->to.y}},
        {"elapsed", o.flight->elapsed}, {"seconds", o.flight->seconds}, {"height", o.flight->height}};
    if (o.light)
    {
        const auto& l = *o.light;
        j["light"] = {{"position", {l.position.x, l.position.y}}, {"radius", l.radius},
            {"color", {l.color.r, l.color.g, l.color.b, l.color.a}}, {"shadows", l.shadows}};
    }
    return j;
}

MapObject decode(const Json& j)
{
    MapObject o;
    o.id = j.value("id", ObjectId(0));
    o.name = j.value("name", std::string{});
    o.texture = j.value("texture", std::string{});
    if (j.contains("area"))
    {
        const auto a = j.at("area").get<std::vector<float>>();
        if (a.size() != 4) throw std::invalid_argument("Object area needs [x,y,w,h]");
        o.area = {a[0], a[1], a[2], a[3]};
    }
    if (!std::isfinite(o.area.x) || !std::isfinite(o.area.y) || !std::isfinite(o.area.w) || !std::isfinite(o.area.h)
        || o.area.w <= 0 || o.area.h <= 0) throw std::invalid_argument("Invalid object area");
    o.floor = j.value("floor", 0);
    if (j.contains("tags")) o.tags = j.at("tags").get<decltype(o.tags)>();
    o.weight = j.value("weight", 1.0f);
    if (!std::isfinite(o.weight) || o.weight < 0) throw std::invalid_argument("Invalid object weight");
    o.destroyed = j.value("destroyed", false);
    if (j.contains("flight"))
    {
        const auto& f = j.at("flight");
        const auto from = f.at("from").get<std::vector<float>>(), to = f.at("to").get<std::vector<float>>();
        if (from.size() != 2 || to.size() != 2) throw std::invalid_argument("Invalid throw endpoints");
        ThrowMotion motion{{from[0], from[1]}, {to[0], to[1]}, f.at("elapsed").get<double>(), f.at("seconds").get<double>(), f.at("height").get<float>()};
        if (!std::isfinite(motion.seconds) || motion.seconds <= 0 || !std::isfinite(motion.elapsed) || motion.elapsed < 0 || motion.elapsed >= motion.seconds
            || !std::isfinite(motion.height) || motion.height < 0 || !std::isfinite(from[0]) || !std::isfinite(from[1])
            || !std::isfinite(to[0]) || !std::isfinite(to[1])) throw std::invalid_argument("Invalid throw motion");
        o.flight = motion;
    }
    if (j.contains("contents")) o.contents = j.at("contents").get<decltype(o.contents)>();
    for (const auto& [name, count] : o.contents)
        if (count < 0) throw std::invalid_argument("Negative inventory count");
    if (j.contains("door"))
    {
        const auto& d = j.at("door");
        o.door = Door{d.value("open", false), d.value("locked", false)};
        o.tags.insert("door");
    }
    if (j.contains("durability"))
    {
        const auto& d = j.at("durability");
        o.durability = Durability{d.at("health").get<int>(), d.at("maximum").get<int>()};
        if (o.durability->maximum <= 0 || o.durability->health < 0 || o.durability->health > o.durability->maximum)
            throw std::invalid_argument("Invalid object durability");
    }
    if (j.contains("light"))
    {
        const auto& l = j.at("light");
        Light light;
        const auto p = l.at("position").get<std::vector<float>>();
        const auto c = l.at("color").get<std::vector<int>>();
        if (p.size() != 2 || c.size() != 4) throw std::invalid_argument("Invalid object light");
        for (int v : c) if (v < 0 || v > 255) throw std::invalid_argument("Invalid light colour");
        light.position = {p[0], p[1]};
        light.radius = l.at("radius").get<float>();
        light.color = {static_cast<uint8_t>(c[0]), static_cast<uint8_t>(c[1]), static_cast<uint8_t>(c[2]), static_cast<uint8_t>(c[3])};
        light.shadows = l.value("shadows", true);
        if (!std::isfinite(light.radius) || light.radius <= 0 || !std::isfinite(p[0]) || !std::isfinite(p[1]))
            throw std::invalid_argument("Invalid light radius or position");
        o.light = light;
    }
    return o;
}
}

std::string Kit::toJson() const { return Json{{"name", name}, {"object", encode(prototype)}}.dump(); }
std::optional<Kit> Kit::fromJson(std::string_view json, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = Json::parse(json);
        return Kit{j.at("name").get<std::string>(), decode(j.at("object"))};
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return std::nullopt; }
}

ObjectId Objects::add(MapObject object)
{
    // Validate programmatic objects using the same rules as imported kits.
    object = decode(encode(object));
    if (object.id == 0) object.id = nextId_;
    if (object.id == std::numeric_limits<ObjectId>::max() || objects_.contains(object.id))
        throw std::invalid_argument("Duplicate or exhausted object id");
    nextId_ = std::max(nextId_, object.id + 1);
    const ObjectId id = object.id;
    objects_.emplace(id, std::move(object));
    return id;
}

ObjectId Objects::place(const Kit& kit, Vec2 position, int floor)
{
    MapObject object = kit.prototype;
    object.id = 0;
    object.area.x = position.x;
    object.area.y = position.y;
    object.floor = floor;
    return add(std::move(object));
}

MapObject* Objects::get(ObjectId id)
{
    const auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : &it->second;
}
const MapObject* Objects::get(ObjectId id) const
{
    const auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : &it->second;
}
std::optional<ObjectId> Objects::at(Vec2 position, int floor) const
{
    for (auto it = objects_.rbegin(); it != objects_.rend(); ++it)
        if (it->second.floor == floor && !it->second.destroyed && it->second.area.contains(position)) return it->first;
    return std::nullopt;
}
bool Objects::passable(const Rect& area, int floor) const
{
    for (const auto& [id, object] : objects_)
    {
        if (object.floor != floor || !object.blocksMovement()) continue;
        const Rect overlap = area.intersect(object.area);
        if (overlap.w > 0 && overlap.h > 0) return false;
    }
    return true;
}

Interaction Objects::interact(ObjectId id, std::span<const std::string> keys)
{
    MapObject* object = get(id);
    if (!object) return Interaction::Missing;
    if (object->destroyed) return Interaction::Unavailable;
    if (object->door)
    {
        auto& door = *object->door;
        if (door.locked)
        {
            const bool unlock = std::any_of(keys.begin(), keys.end(), [&](const std::string& key) {
                return key.starts_with("key:") && object->has(key);
            });
            if (!unlock) return Interaction::Locked;
            door.locked = false;
        }
        door.open = !door.open;
        return door.open ? Interaction::Opened : Interaction::Closed;
    }
    if (object->has("lever"))
    {
        for (auto& [otherId, other] : objects_)
        {
            if (!other.door || other.destroyed) continue;
            for (const std::string& tag : object->tags)
                if (tag.starts_with("link:") && other.has(tag))
                {
                    other.door->open = !other.door->open;
                    break;
                }
        }
        return Interaction::Activated;
    }
    return object->has("interactable") || object->has("container") ? Interaction::Activated : Interaction::Unavailable;
}

bool Objects::damage(ObjectId id, int amount)
{
    MapObject* o = get(id);
    if (!o || o->destroyed || !o->durability || !o->has("destructible") || amount <= 0) return false;
    o->durability->health = std::max(0, o->durability->health - amount);
    o->destroyed = o->durability->health == 0;
    return true;
}

int Objects::take(ObjectId id, std::string_view item, int amount)
{
    MapObject* o = get(id);
    if (!o || o->destroyed || !o->has("container") || amount <= 0 || (o->door && o->door->locked)) return 0;
    const auto it = o->contents.find(item);
    if (it == o->contents.end()) return 0;
    const int taken = std::min(it->second, amount);
    it->second -= taken;
    if (it->second == 0) o->contents.erase(it);
    return taken;
}

bool Objects::throwTo(ObjectId id, Vec2 destination, double seconds, float height)
{
    MapObject* o = get(id);
    if (!o || o->destroyed || o->flight || !o->has("throwable") || !std::isfinite(seconds) || seconds <= 0
        || !std::isfinite(height) || height < 0 || !std::isfinite(destination.x) || !std::isfinite(destination.y)) return false;
    o->flight = ThrowMotion{o->area.position(), destination, 0, seconds, height};
    return true;
}

void Objects::update(double dt, const std::function<void(ObjectId)>& landed)
{
    if (!std::isfinite(dt) || dt <= 0) return;
    std::vector<ObjectId> completed;
    for (auto& [id, o] : objects_)
    {
        if (!o.flight) continue;
        auto& f = *o.flight;
        f.elapsed = std::min(f.elapsed + dt, f.seconds);
        const float t = static_cast<float>(f.elapsed / f.seconds);
        const Vec2 p = f.from + (f.to - f.from) * t;
        o.area.x = p.x; o.area.y = p.y;
        if (f.elapsed >= f.seconds) { o.flight.reset(); if (landed) completed.push_back(id); }
    }
    for (ObjectId id : completed) landed(id); // safe to remove objects or launch another throw
}

std::vector<Wall> Objects::walls(int floor) const
{
    std::vector<Wall> out;
    for (const auto& [id, o] : objects_)
    {
        if (o.floor != floor || !o.blocksSight()) continue;
        const Vec2 a{o.area.x, o.area.y}, b{o.area.x + o.area.w, o.area.y};
        const Vec2 c{o.area.x + o.area.w, o.area.y + o.area.h}, d{o.area.x, o.area.y + o.area.h};
        out.insert(out.end(), {{a, b}, {b, c}, {c, d}, {d, a}});
    }
    return out;
}

std::vector<Light> Objects::lights(int floor) const
{
    std::vector<Light> out;
    for (const auto& [id, o] : objects_)
        if (o.floor == floor && !o.destroyed && o.light)
        {
            Light l = *o.light;
            l.position = l.position + o.area.position();
            out.push_back(l);
        }
    return out;
}

void Objects::draw(Renderer& renderer, int floor, const std::function<TextureId(std::string_view)>& lookup) const
{
    for (const auto& [id, o] : objects_)
    {
        if (o.floor != floor || o.destroyed) continue;
        Rect visual = o.area;
        if (o.flight)
        {
            const float t = static_cast<float>(o.flight->elapsed / o.flight->seconds);
            visual.y -= 4 * o.flight->height * t * (1 - t);
            renderer.fillRect(o.area, {0, 0, 0, 60});
        }
        if (!renderer.visible(visual)) continue;
        if (!o.texture.empty() && lookup) renderer.drawSprite(lookup(o.texture), visual);
        else renderer.fillRect(visual, o.door && o.door->open ? Color{80, 160, 100, 90} : Color{140, 100, 65, 255});
    }
}

std::string Objects::toJson() const
{
    Json j{{"nextId", nextId_}, {"objects", Json::array()}};
    for (const auto& [id, o] : objects_) j["objects"].push_back(encode(o));
    return j.dump();
}

std::optional<Objects> Objects::fromJson(std::string_view json, std::string* error)
{
    if (error) error->clear();
    try
    {
        const auto j = Json::parse(json);
        Objects objects;
        for (const auto& o : j.at("objects")) objects.add(decode(o));
        const auto next = j.value("nextId", objects.nextId_);
        if (next < objects.nextId_ || next == std::numeric_limits<ObjectId>::max()) throw std::invalid_argument("Invalid next object id");
        objects.nextId_ = next;
        return objects;
    }
    catch (const std::exception& e) { if (error) *error = e.what(); return std::nullopt; }
}

}
