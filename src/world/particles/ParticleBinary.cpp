#include "world/ParticleBinary.h"

#include "Core/Json/Json.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace kestrel::world {

namespace {

constexpr std::string_view Magic = "\x7fMCB";
constexpr size_t HeaderSize = 12;

/**
 * The FNV-1a hash the binary form names each component by.
 */
constexpr uint32_t componentHash(std::string_view name)
{
    uint32_t hash = 0x811c9dc5u;
    for (char character : name) {
        hash = (hash ^ static_cast<uint8_t>(character)) * 0x01000193u;
    }
    return hash;
}

/**
 * Reads the little endian fields of the binary form, throwing when the data
 * runs out or a flag holds something other than 0 or 1.
 */
class Reader {
public:
    explicit Reader(const std::string& data)
        : data(data)
    {
    }

    void seek(size_t to)
    {
        at = to;
    }

    uint8_t byte()
    {
        need(1);
        return static_cast<uint8_t>(data[at++]);
    }

    uint32_t u32()
    {
        need(4);
        uint32_t value = 0;
        std::memcpy(&value, data.data() + at, 4);
        at += 4;
        return value;
    }

    int32_t i32()
    {
        return static_cast<int32_t>(u32());
    }

    float f32()
    {
        uint32_t bits = u32();
        float value = 0.0f;
        std::memcpy(&value, &bits, 4);
        return value;
    }

    uint32_t varint()
    {
        uint32_t value = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            uint8_t part = byte();
            value |= static_cast<uint32_t>(part & 0x7f) << shift;
            if (!(part & 0x80)) {
                return value;
            }
        }
        throw std::runtime_error("varint too long");
    }

    std::string text()
    {
        uint32_t length = varint();
        need(length);
        std::string value = data.substr(at, length);
        at += length;
        return value;
    }

    bool flag()
    {
        uint8_t value = byte();
        if (value > 1) {
            throw std::runtime_error("bad flag");
        }
        return value != 0;
    }

private:
    void need(size_t count) const
    {
        if (at + count > data.size()) {
            throw std::runtime_error("data cut short");
        }
    }

    const std::string& data;
    size_t at = 0;
};

/**
 * Writes the JSON text of one decoded effect.
 */
class Writer {
public:
    Writer(Reader& reader, std::string& out)
        : in(reader)
        , out(out)
    {
    }

    void key(std::string_view name)
    {
        comma();
        out += '"';
        out += name;
        out += "\":";
        fresh = false;
    }

    void open(char bracket)
    {
        comma();
        out += bracket;
        fresh = true;
    }

    void close(char bracket)
    {
        out += bracket;
        fresh = false;
    }

    void number(double value)
    {
        comma();
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.9g", value);
        out += buffer;
    }

    void string(const std::string& value)
    {
        comma();
        out += '"';
        out += json::escape(value);
        out += '"';
    }

    void boolean(bool value)
    {
        comma();
        out += value ? "true" : "false";
    }

    void molang()
    {
        uint8_t kind = in.byte();
        if (kind == 1) {
            number(in.f32());
        } else if (kind == 0) {
            string(in.text());
        } else {
            throw std::runtime_error("bad molang tag");
        }
    }

    void molangs(size_t count)
    {
        open('[');
        for (size_t index = 0; index < count; ++index) {
            molang();
        }
        close(']');
    }

    void floats(size_t count)
    {
        open('[');
        for (size_t index = 0; index < count; ++index) {
            number(in.f32());
        }
        close(']');
    }

    void field(std::string_view name)
    {
        key(name);
        molang();
    }

    void vector(std::string_view name)
    {
        key(name);
        molangs(3);
    }

    Reader& in;

private:
    void comma()
    {
        if (!fresh && !out.empty() && out.back() != ':' && out.back() != '{' && out.back() != '[') {
            out += ',';
        }
        fresh = false;
    }

    std::string& out;
    bool fresh = true;
};

void shapeDirection(Writer& w)
{
    if (w.in.flag() && w.in.flag()) {
        w.vector("direction");
    }
}

void blockList(Writer& w)
{
    w.open('[');
    uint32_t count = w.in.varint();
    for (uint32_t index = 0; index < count; ++index) {
        w.string(w.in.text());
    }
    w.close(']');
}

void billboard(Writer& w)
{
    w.key("size");
    w.molangs(2);
    if (w.in.flag()) {
        w.key("facing_camera_mode");
        w.string(w.in.text());
    }
    w.key("direction");
    w.open('{');
    if (w.in.flag()) {
        w.key("mode");
        w.string(w.in.text());
    }
    if (w.in.flag()) {
        w.vector("custom_direction");
    }
    w.key("min_speed_threshold");
    w.number(w.in.f32());
    w.close('}');
    w.in.flag();
    w.key("uv");
    w.open('{');
    w.key("texture_width");
    w.number(w.in.i32());
    w.key("texture_height");
    w.number(w.in.i32());
    if (w.in.flag()) {
        w.key("uv");
        w.molangs(2);
    }
    if (w.in.flag()) {
        w.key("uv_size");
        w.molangs(2);
    }
    if (w.in.flag()) {
        w.key("flipbook");
        w.open('{');
        w.key("base_UV");
        w.molangs(2);
        w.key("size_UV");
        w.floats(2);
        w.key("step_UV");
        w.floats(2);
        w.key("frames_per_second");
        w.number(w.in.f32());
        w.field("max_frame");
        w.key("stretch_to_lifetime");
        w.boolean(w.in.flag());
        w.key("loop");
        w.boolean(w.in.flag());
        w.close('}');
    }
    w.close('}');
}

void tinting(Writer& w)
{
    w.in.flag();
    w.key("color");
    if (!w.in.flag()) {
        w.molangs(4);
        return;
    }
    w.open('{');
    w.key("gradient");
    w.open('{');
    uint32_t stops = w.in.varint();
    for (uint32_t index = 0; index < stops; ++index) {
        w.key(w.in.text());
        w.molangs(4);
    }
    w.close('}');
    w.field("interpolant");
    w.close('}');
}

void collision(Writer& w)
{
    w.field("enabled");
    w.key("collision_drag");
    w.number(w.in.f32());
    w.key("coefficient_of_restitution");
    w.number(w.in.f32());
    w.key("collision_radius");
    w.number(w.in.f32());
    w.key("expire_on_contact");
    w.boolean(w.in.flag());
    w.key("events");
    w.open('[');
    if (w.in.flag()) {
        uint32_t count = w.in.varint();
        for (uint32_t index = 0; index < count; ++index) {
            w.open('{');
            w.key("min_speed");
            w.number(w.in.f32());
            w.key("event");
            w.string(w.in.text());
            w.close('}');
        }
    }
    w.close(']');
}

/**
 * Writes one component's body; false for a component the binary form reader
 * does not know, whose length cannot be skipped.
 */
bool component(uint32_t hash, Writer& w)
{
    switch (hash) {
    case componentHash("minecraft:emitter_lifetime_expression"):
        w.open('{');
        w.field("activation_expression");
        w.field("expiration_expression");
        w.close('}');
        return true;
    case componentHash("minecraft:emitter_lifetime_once"):
        w.open('{');
        w.field("active_time");
        w.close('}');
        return true;
    case componentHash("minecraft:emitter_lifetime_looping"):
        w.open('{');
        w.field("active_time");
        w.field("sleep_time");
        w.close('}');
        return true;
    case componentHash("minecraft:emitter_rate_instant"):
        w.open('{');
        w.field("num_particles");
        w.close('}');
        return true;
    case componentHash("minecraft:emitter_rate_manual"):
        w.open('{');
        w.field("max_particles");
        w.close('}');
        return true;
    case componentHash("minecraft:emitter_rate_steady"):
        w.open('{');
        w.field("spawn_rate");
        w.field("max_particles");
        w.close('}');
        return true;
    case componentHash("minecraft:emitter_initialization"):
        w.open('{');
        w.field("creation_expression");
        w.field("per_update_expression");
        w.close('}');
        return true;
    case componentHash("minecraft:emitter_local_space"):
        w.open('{');
        w.key("position");
        w.boolean(w.in.flag());
        w.key("rotation");
        w.boolean(w.in.flag());
        w.key("velocity");
        w.boolean(w.in.flag());
        w.close('}');
        return true;
    case componentHash("minecraft:emitter_shape_point"):
        w.open('{');
        w.vector("offset");
        if (w.in.flag()) {
            w.vector("direction");
        }
        w.close('}');
        return true;
    case componentHash("minecraft:emitter_shape_custom"):
        w.open('{');
        w.vector("offset");
        w.vector("direction");
        w.close('}');
        return true;
    case componentHash("minecraft:emitter_shape_sphere"):
        w.open('{');
        w.vector("offset");
        w.field("radius");
        w.key("surface_only");
        w.boolean(w.in.flag());
        shapeDirection(w);
        w.close('}');
        return true;
    case componentHash("minecraft:emitter_shape_entity_aabb"):
        w.open('{');
        w.key("surface_only");
        w.boolean(w.in.flag());
        shapeDirection(w);
        w.close('}');
        return true;
    case componentHash("minecraft:particle_appearance_billboard"):
        w.open('{');
        billboard(w);
        w.close('}');
        return true;
    case componentHash("minecraft:particle_appearance_tinting"):
        w.open('{');
        tinting(w);
        w.close('}');
        return true;
    case componentHash("minecraft:particle_appearance_lighting"):
        w.open('{');
        w.close('}');
        return true;
    case componentHash("minecraft:particle_initial_speed"):
        if (w.in.flag()) {
            w.molang();
        } else {
            w.molangs(3);
        }
        return true;
    case componentHash("minecraft:particle_initialization"):
        w.open('{');
        w.field("per_update_expression");
        w.field("per_render_expression");
        w.close('}');
        return true;
    case componentHash("minecraft:particle_lifetime_expression"):
        w.open('{');
        if (w.in.flag()) {
            w.field("max_lifetime");
        }
        w.field("expiration_expression");
        w.close('}');
        return true;
    case componentHash("minecraft:particle_motion_dynamic"):
        w.open('{');
        w.vector("linear_acceleration");
        w.field("linear_drag_coefficient");
        w.field("rotation_acceleration");
        w.field("rotation_drag_coefficient");
        w.close('}');
        return true;
    case componentHash("minecraft:particle_motion_parametric"):
        w.open('{');
        if (w.in.flag()) {
            w.vector("relative_position");
        }
        if (w.in.flag()) {
            w.vector("direction");
        }
        if (w.in.flag()) {
            w.field("rotation");
        }
        w.close('}');
        return true;
    case componentHash("minecraft:particle_motion_collision"):
        w.open('{');
        collision(w);
        w.close('}');
        return true;
    case componentHash("minecraft:particle_expire_if_in_blocks"):
    case componentHash("minecraft:particle_expire_if_not_in_blocks"):
        blockList(w);
        return true;
    case componentHash("minecraft:particle_kill_plane"):
        w.floats(4);
        return true;
    default:
        return false;
    }
}

const char* componentName(uint32_t hash)
{
    static constexpr std::array<const char*, 24> Names {
        "minecraft:emitter_lifetime_expression",
        "minecraft:emitter_lifetime_once",
        "minecraft:emitter_lifetime_looping",
        "minecraft:emitter_rate_instant",
        "minecraft:emitter_rate_manual",
        "minecraft:emitter_rate_steady",
        "minecraft:emitter_initialization",
        "minecraft:emitter_local_space",
        "minecraft:emitter_shape_point",
        "minecraft:emitter_shape_custom",
        "minecraft:emitter_shape_sphere",
        "minecraft:emitter_shape_entity_aabb",
        "minecraft:particle_appearance_billboard",
        "minecraft:particle_appearance_tinting",
        "minecraft:particle_appearance_lighting",
        "minecraft:particle_initial_speed",
        "minecraft:particle_initialization",
        "minecraft:particle_lifetime_expression",
        "minecraft:particle_motion_dynamic",
        "minecraft:particle_motion_parametric",
        "minecraft:particle_motion_collision",
        "minecraft:particle_expire_if_in_blocks",
        "minecraft:particle_expire_if_not_in_blocks",
        "minecraft:particle_kill_plane",
    };
    for (const char* name : Names) {
        if (componentHash(name) == hash) {
            return name;
        }
    }
    return nullptr;
}

}

bool isParticleBinary(const std::string& data)
{
    return data.size() >= HeaderSize && std::string_view(data).substr(0, Magic.size()) == Magic;
}

std::optional<std::string> particleBinaryToJson(const std::string& data)
{
    if (!isParticleBinary(data)) {
        return std::nullopt;
    }
    std::string out;
    try {
        Reader in(data);
        in.seek(HeaderSize);
        Writer w(in, out);
        in.text();
        std::string identifier = in.text();
        std::string material = in.text();
        std::string texture = in.text();
        uint32_t count = in.u32();
        w.open('{');
        w.key("particle_effect");
        w.open('{');
        w.key("description");
        w.open('{');
        w.key("identifier");
        w.string(identifier);
        w.key("basic_render_parameters");
        w.open('{');
        w.key("material");
        w.string(material);
        w.key("texture");
        w.string(texture);
        w.close('}');
        w.close('}');
        w.key("components");
        w.open('{');
        for (uint32_t index = 0; index < count; ++index) {
            uint32_t hash = in.u32();
            const char* name = componentName(hash);
            if (!name) {
                return std::nullopt;
            }
            w.key(name);
            if (!component(hash, w)) {
                return std::nullopt;
            }
        }
        w.close('}');
        w.close('}');
        w.close('}');
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return out;
}

}
