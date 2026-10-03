#pragma once

#include "yorehold/framework/graphics/Lighting.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace yh
{

using ObjectId = uint64_t;

struct Door
{
    bool open = false;
    bool locked = false;
};

struct Durability
{
    int health = 10;
    int maximum = 10;
};

struct ThrowMotion
{
    Vec2 from, to; // ground-plane top-left positions
    double elapsed = 0, seconds = 0.6;
    float height = 80; // visual arc; ground collision is decided by the caller
};

// Tags select capabilities; components hold their settings. The client decides how to show
// these fields in an editor. Texture paths stay portable across skins and chapter archives.
struct MapObject
{
    ObjectId id = 0;
    std::string name;
    std::string texture;
    Rect area{0, 0, 32, 32};
    int floor = 0;
    std::set<std::string, std::less<>> tags;
    std::optional<Door> door;
    std::optional<Durability> durability;
    std::map<std::string, int, std::less<>> contents;
    std::optional<Light> light;
    float weight = 1;
    bool destroyed = false;
    std::optional<ThrowMotion> flight;

    bool has(std::string_view tag) const { return tags.contains(tag); }
    bool blocksMovement() const { return !destroyed && !flight && (!door || !door->open) && has("blocksMovement"); }
    bool blocksSight() const { return !destroyed && !flight && (!door || !door->open) && has("blocksSight"); }
};

struct Kit
{
    std::string name;
    MapObject prototype;
    std::string toJson() const;
    static std::optional<Kit> fromJson(std::string_view json, std::string* error = nullptr);
};

enum class Interaction { Missing, Unavailable, Locked, Opened, Closed, Activated };

class Objects
{
public:
    ObjectId add(MapObject object);
    ObjectId place(const Kit& kit, Vec2 position, int floor = 0);
    bool remove(ObjectId id) { return objects_.erase(id) != 0; }
    MapObject* get(ObjectId id);
    const MapObject* get(ObjectId id) const;
    const std::map<ObjectId, MapObject>& all() const { return objects_; }
    std::optional<ObjectId> at(Vec2 position, int floor) const;
    bool passable(const Rect& area, int floor) const;
    // key:* tags match keys in the supplied inventory. link:* tags connect levers to doors.
    Interaction interact(ObjectId id, std::span<const std::string> keys = {});
    bool damage(ObjectId id, int amount);
    int take(ObjectId id, std::string_view item, int amount);
    bool throwTo(ObjectId id, Vec2 destination, double seconds = 0.6, float height = 80);
    // Arc state is saved with the object, so frozen regions resume in-flight throws.
    void update(double dt, const std::function<void(ObjectId)>& landed = {});
    std::vector<Wall> walls(int floor) const;
    std::vector<Light> lights(int floor) const;
    void draw(Renderer& renderer, int floor, const std::function<TextureId(std::string_view)>& textureLookup = {}) const;
    std::string toJson() const;
    static std::optional<Objects> fromJson(std::string_view json, std::string* error = nullptr);

private:
    std::map<ObjectId, MapObject> objects_;
    ObjectId nextId_ = 1;
};

}
