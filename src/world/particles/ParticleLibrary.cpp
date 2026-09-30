#include "world/Particles.h"
#include "world/ParticleBinary.h"

#include "Core/Json/Json.h"
#include "util/JsonText.h"
#include "util/Text.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace kestrel::world {

namespace {

using util::endsWith;
using util::lowercase;
using util::startsWith;
using util::stripJsonComments;

/**
 * A component value as a Molang script: a JSON number becomes a constant, a
 * string is compiled, anything else keeps the fallback.
 */
ParticleExpression expression(const json::Value* value, double fallback = 0.0)
{
    if (!value) {
        return ParticleExpression(fallback);
    }
    if (value->isNumber()) {
        return ParticleExpression(value->mNumber);
    }
    if (value->mType == json::Value::Type::Boolean) {
        return ParticleExpression(value->mBoolean ? 1.0 : 0.0);
    }
    if (value->isString()) {
        return ParticleExpression::compile(value->mString, fallback);
    }
    return ParticleExpression(fallback);
}

template <size_t Count>
void readVector(const json::Value* value, std::array<ParticleExpression, Count>& out, double fallback = 0.0)
{
    if (!value || !value->isArray()) {
        if (value) {
            ParticleExpression single = expression(value, fallback);
            out.fill(single);
        }
        return;
    }
    for (size_t index = 0; index < Count; ++index) {
        out[index] = index < value->mArray.size() ? expression(value->mArray[index].get(), fallback) : ParticleExpression(fallback);
    }
}

/**
 * A value that has to be a plain number: a JSON number, or a Molang string
 * evaluated once with no variables.
 */
float constantNumber(const json::Value* value, float fallback)
{
    if (!value) {
        return fallback;
    }
    if (value->isNumber()) {
        return static_cast<float>(value->mNumber);
    }
    if (value->isString()) {
        ParticleExpression script = ParticleExpression::compile(value->mString, fallback);
        if (script.isConstant()) {
            return static_cast<float>(script.constantValue());
        }
        std::unordered_map<std::string, double> variables;
        molang::Scope scope;
        scope.variables = &variables;
        return static_cast<float>(script.run(scope));
    }
    return fallback;
}

/**
 * A list of event names given as one string or an array of strings.
 */
std::vector<std::string> readNames(const json::Value* value)
{
    std::vector<std::string> out;
    if (!value) {
        return out;
    }
    if (value->isString()) {
        out.push_back(value->mString);
    } else if (value->isArray()) {
        for (const std::unique_ptr<json::Value>& entry : value->mArray) {
            if (entry->isString()) {
                out.push_back(entry->mString);
            }
        }
    }
    return out;
}

/**
 * A timeline object whose keys are times in seconds and whose values name the
 * events firing then, sorted by time.
 */
ParticleTimeline readTimeline(const json::Value* value)
{
    ParticleTimeline out;
    if (!value || !value->isObject()) {
        return out;
    }
    for (const std::string& key : value->mKeys) {
        std::vector<std::string> names = readNames(value->mObject.at(key).get());
        if (!names.empty()) {
            out.emplace_back(std::strtod(key.c_str(), nullptr), std::move(names));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    return out;
}

ParticleEventAction::Type eventTypeOf(const std::string& type)
{
    std::string name = lowercase(type);
    if (name == "emitter_bound") {
        return ParticleEventAction::Type::EmitterBound;
    }
    if (name == "particle") {
        return ParticleEventAction::Type::Particle;
    }
    if (name == "particle_with_velocity") {
        return ParticleEventAction::Type::ParticleWithVelocity;
    }
    return ParticleEventAction::Type::Emitter;
}

/**
 * One event node: its particle_effect and expression make one action, and its
 * sequence and randomize children add theirs, a sequence flattened into the
 * actions that all run.
 */
void readEventNode(const json::Value& node, ParticleEvent& event, int depth)
{
    constexpr int MaxEventDepth = 4;
    if (!node.isObject() || depth > MaxEventDepth) {
        return;
    }
    const json::Value* effect = node.get("particle_effect");
    const json::Value* script = node.get("expression");
    if ((effect && effect->isObject()) || (script && script->isString())) {
        ParticleEventAction action;
        if (effect && effect->isObject()) {
            if (const json::Value* name = effect->get("effect"); name && name->isString()) {
                action.effect = name->mString;
            }
            if (const json::Value* type = effect->get("type"); type && type->isString()) {
                action.type = eventTypeOf(type->mString);
            }
            if (const json::Value* before = effect->get("pre_effect_expression"); before && before->isString() && !(script && script->isString())) {
                action.expression = ParticleExpression::compile(before->mString);
            }
        }
        if (script && script->isString()) {
            action.expression = ParticleExpression::compile(script->mString);
        }
        event.sequence.push_back(std::move(action));
    }
    if (const json::Value* sequence = node.get("sequence"); sequence && sequence->isArray()) {
        for (const std::unique_ptr<json::Value>& entry : sequence->mArray) {
            readEventNode(*entry, event, depth + 1);
        }
    }
    if (const json::Value* randomize = node.get("randomize"); randomize && randomize->isArray()) {
        for (const std::unique_ptr<json::Value>& entry : randomize->mArray) {
            ParticleEvent option;
            readEventNode(*entry, option, depth + 1);
            double weight = util::jsonNumber(entry->get("weight"), 1.0f);
            for (ParticleEventAction& action : option.sequence) {
                event.randomized.emplace_back(weight, std::move(action));
            }
        }
    }
}

void readEvents(const json::Value& events, ParticleEvents& out)
{
    for (const std::string& key : events.mKeys) {
        ParticleEvent event;
        readEventNode(*events.mObject.at(key), event, 0);
        if (!event.sequence.empty() || !event.randomized.empty()) {
            out.events[key] = std::move(event);
        }
    }
}

void readCollisionEvents(const json::Value* value, ParticleEvents& out)
{
    if (!value) {
        return;
    }
    auto readOne = [&out](const json::Value& entry) {
        if (!entry.isObject()) {
            return;
        }
        for (const std::string& name : readNames(entry.get("event"))) {
            out.collision.emplace_back(name, constantNumber(entry.get("min_speed"), 2.0f));
        }
    };
    if (value->isArray()) {
        for (const std::unique_ptr<json::Value>& entry : value->mArray) {
            readOne(*entry);
        }
    } else {
        readOne(*value);
    }
}

bool readBool(const json::Value* value, bool fallback)
{
    if (!value) {
        return fallback;
    }
    if (value->mType == json::Value::Type::Boolean) {
        return value->mBoolean;
    }
    if (value->isNumber()) {
        return value->mNumber != 0.0;
    }
    if (value->isString()) {
        std::string text = lowercase(value->mString);
        return text == "true" || text == "1";
    }
    return fallback;
}

std::vector<ParticleExpression> readStatements(const json::Value* value)
{
    std::vector<ParticleExpression> out;
    if (!value) {
        return out;
    }
    if (value->isArray()) {
        for (const std::unique_ptr<json::Value>& entry : value->mArray) {
            if (entry->isString()) {
                out.push_back(ParticleExpression::compile(entry->mString));
            }
        }
    } else if (value->isString()) {
        out.push_back(ParticleExpression::compile(value->mString));
    }
    return out;
}

std::vector<std::string> readBlockNames(const json::Value* value)
{
    std::vector<std::string> out;
    if (!value || !value->isArray()) {
        return out;
    }
    for (const std::unique_ptr<json::Value>& entry : value->mArray) {
        if (entry->isString()) {
            out.push_back(entry->mString);
        } else if (entry->isObject()) {
            if (const json::Value* name = entry->get("name"); name && name->isString()) {
                out.push_back(name->mString);
            }
        }
    }
    return out;
}

int hexDigit(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

/**
 * A "#AARRGGBB" or "#RRGGBB" color as red, green, blue, alpha from 0 to 1.
 */
bool parseHexColor(const std::string& text, std::array<float, 4>& out)
{
    std::string_view digits(text);
    if (!digits.empty() && digits.front() == '#') {
        digits.remove_prefix(1);
    }
    if (digits.size() != 6 && digits.size() != 8) {
        return false;
    }
    std::array<float, 4> channels { 1.0f, 1.0f, 1.0f, 1.0f };
    for (size_t index = 0; index < digits.size() / 2; ++index) {
        int high = hexDigit(digits[index * 2]);
        int low = hexDigit(digits[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        channels[index] = (float) (high * 16 + low) / 255.0f;
    }
    if (digits.size() == 8) {
        out = { channels[1], channels[2], channels[3], channels[0] };
    } else {
        out = { channels[0], channels[1], channels[2], 1.0f };
    }
    return true;
}

bool readColor(const json::Value* value, std::array<float, 4>& out)
{
    if (!value) {
        return false;
    }
    if (value->isString()) {
        return parseHexColor(value->mString, out);
    }
    if (value->isArray()) {
        out = { 1.0f, 1.0f, 1.0f, 1.0f };
        for (size_t index = 0; index < 4 && index < value->mArray.size(); ++index) {
            out[index] = (float) value->mArray[index]->number(1.0);
        }
        return true;
    }
    return false;
}

void writeJson(const json::Value& value, std::string& out)
{
    switch (value.mType) {
    case json::Value::Type::Null:
        out += "null";
        break;
    case json::Value::Type::Boolean:
        out += value.mBoolean ? "true" : "false";
        break;
    case json::Value::Type::Number: {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.17g", value.mNumber);
        out += buffer;
        break;
    }
    case json::Value::Type::String:
        out += '"';
        out += json::escape(value.mString);
        out += '"';
        break;
    case json::Value::Type::Array:
        out += '[';
        for (size_t index = 0; index < value.mArray.size(); ++index) {
            if (index > 0) {
                out += ',';
            }
            writeJson(*value.mArray[index], out);
        }
        out += ']';
        break;
    case json::Value::Type::Object:
        out += '{';
        for (size_t index = 0; index < value.mKeys.size(); ++index) {
            if (index > 0) {
                out += ',';
            }
            out += '"';
            out += json::escape(value.mKeys[index]);
            out += "\":";
            writeJson(*value.mObject.at(value.mKeys[index]), out);
        }
        out += '}';
        break;
    }
}

ParticleAppearance::Facing facingOf(const std::string& mode)
{
    static const std::unordered_map<std::string, ParticleAppearance::Facing> modes {
        { "rotate_xyz", ParticleAppearance::Facing::RotateXyz },
        { "rotate_y", ParticleAppearance::Facing::RotateY },
        { "lookat_xyz", ParticleAppearance::Facing::LookatXyz },
        { "lookat_y", ParticleAppearance::Facing::LookatY },
        { "lookat_direction", ParticleAppearance::Facing::LookatDirection },
        { "direction_x", ParticleAppearance::Facing::DirectionX },
        { "direction_y", ParticleAppearance::Facing::DirectionY },
        { "direction_z", ParticleAppearance::Facing::DirectionZ },
        { "emitter_transform_xy", ParticleAppearance::Facing::EmitterTransformXy },
        { "emitter_transform_xz", ParticleAppearance::Facing::EmitterTransformXz },
        { "emitter_transform_yz", ParticleAppearance::Facing::EmitterTransformYz },
    };
    auto found = modes.find(lowercase(mode));
    return found == modes.end() ? ParticleAppearance::Facing::RotateXyz : found->second;
}

void readShape(const json::Value& body, ParticleShape::Kind kind, ParticleShape& shape)
{
    shape = ParticleShape {};
    shape.kind = kind;
    readVector(body.get("offset"), shape.offset);
    shape.radius = expression(body.get("radius"), 1.0);
    readVector(body.get("half_dimensions"), shape.halfDimensions);
    shape.planeNormal = { ParticleExpression(0.0), ParticleExpression(1.0), ParticleExpression(0.0) };
    if (const json::Value* normal = body.get("plane_normal")) {
        if (normal->isString()) {
            std::string axis = lowercase(normal->mString);
            shape.planeNormal = {
                ParticleExpression(axis == "x" ? 1.0 : 0.0),
                ParticleExpression(axis == "y" ? 1.0 : 0.0),
                ParticleExpression(axis == "z" ? 1.0 : 0.0),
            };
        } else {
            readVector(normal, shape.planeNormal);
        }
    }
    shape.surfaceOnly = readBool(body.get("surface_only"), false);
    if (const json::Value* direction = body.get("direction")) {
        if (direction->isString()) {
            shape.direction = lowercase(direction->mString) == "inwards" ? ParticleShape::Direction::Inwards : ParticleShape::Direction::Outwards;
        } else if (direction->isArray()) {
            shape.direction = ParticleShape::Direction::Custom;
            readVector(direction, shape.customDirection);
        }
    }
}

void readFlipbook(const json::Value& flipbook, ParticleAppearance& appearance)
{
    appearance.flipbook = true;
    readVector(flipbook.get("base_UV"), appearance.flipbookBase);
    readVector(flipbook.get("size_UV"), appearance.flipbookSize);
    readVector(flipbook.get("step_UV"), appearance.flipbookStep);
    appearance.framesPerSecond = expression(flipbook.get("frames_per_second"), 0.0);
    appearance.maxFrame = expression(flipbook.get("max_frame"), 1.0);
    appearance.stretchToLifetime = readBool(flipbook.get("stretch_to_lifetime"), false);
    appearance.loop = readBool(flipbook.get("loop"), false);
}

void readBillboard(const json::Value& body, ParticleAppearance& appearance)
{
    readVector(body.get("size"), appearance.size);
    if (const json::Value* mode = body.get("facing_camera_mode"); mode && mode->isString()) {
        appearance.facing = facingOf(mode->mString);
    }
    if (const json::Value* direction = body.get("direction"); direction && direction->isObject()) {
        if (const json::Value* mode = direction->get("mode"); mode && mode->isString()) {
            appearance.directionMode = lowercase(mode->mString) == "custom" ? ParticleAppearance::DirectionMode::Custom : ParticleAppearance::DirectionMode::DeriveFromVelocity;
        }
        readVector(direction->get("custom_direction"), appearance.customDirection);
        appearance.minSpeedThreshold = constantNumber(direction->get("min_speed_threshold"), 0.01f);
    }
    const json::Value* uv = body.get("uv");
    if (!uv || !uv->isObject()) {
        return;
    }
    appearance.textureWidth = util::jsonNumber(uv->get("texture_width"), 1.0f);
    appearance.textureHeight = util::jsonNumber(uv->get("texture_height"), 1.0f);
    readVector(uv->get("uv"), appearance.uv);
    readVector(uv->get("uv_size"), appearance.uvSize);
    if (const json::Value* flipbook = uv->get("flipbook"); flipbook && flipbook->isObject()) {
        readFlipbook(*flipbook, appearance);
    }
}

void readTinting(const json::Value& body, ParticleAppearance& appearance)
{
    const json::Value* color = body.get("color");
    if (!color) {
        return;
    }
    appearance.tinted = true;
    if (color->isObject()) {
        appearance.gradient = true;
        appearance.gradientInterpolant = expression(color->get("interpolant"));
        const json::Value* stops = color->get("gradient");
        if (!stops) {
            return;
        }
        if (stops->isObject()) {
            for (const std::string& key : stops->mKeys) {
                std::array<float, 4> value {};
                if (readColor(stops->mObject.at(key).get(), value)) {
                    appearance.gradientStops.emplace_back(std::strtof(key.c_str(), nullptr), value);
                }
            }
        } else if (stops->isArray()) {
            size_t count = stops->mArray.size();
            for (size_t index = 0; index < count; ++index) {
                std::array<float, 4> value {};
                if (readColor(stops->mArray[index].get(), value)) {
                    float time = count > 1 ? (float) index / (float) (count - 1) : 0.0f;
                    appearance.gradientStops.emplace_back(time, value);
                }
            }
        }
        std::stable_sort(appearance.gradientStops.begin(), appearance.gradientStops.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        return;
    }
    if (color->isString()) {
        std::array<float, 4> value {};
        if (parseHexColor(color->mString, value)) {
            for (size_t index = 0; index < 4; ++index) {
                appearance.color[index] = ParticleExpression(value[index]);
            }
        }
        return;
    }
    if (color->isArray()) {
        readVector(color, appearance.color, 1.0);
        if (color->mArray.size() < 4) {
            appearance.color[3] = ParticleExpression(1.0);
        }
    }
}

void readComponent(const std::string& name, const json::Value& body, ParticleEffect& effect)
{
    ParticleEmitterRules& emitter = effect.emitter;
    ParticleMotion& motion = effect.motion;
    if (name == "minecraft:emitter_rate_instant") {
        emitter.rate = ParticleEmitterRules::Rate::Instant;
        emitter.numParticles = expression(body.get("num_particles"), 10.0);
    } else if (name == "minecraft:emitter_rate_steady") {
        emitter.rate = ParticleEmitterRules::Rate::Steady;
        emitter.spawnRate = expression(body.get("spawn_rate"), 1.0);
        emitter.maxParticles = expression(body.get("max_particles"), 50.0);
    } else if (name == "minecraft:emitter_rate_manual") {
        emitter.rate = ParticleEmitterRules::Rate::Manual;
        emitter.maxParticles = expression(body.get("max_particles"), 50.0);
    } else if (name == "minecraft:emitter_lifetime_once") {
        emitter.lifetime = ParticleEmitterRules::Lifetime::Once;
        emitter.activeTime = expression(body.get("active_time"), 10.0);
    } else if (name == "minecraft:emitter_lifetime_looping") {
        emitter.lifetime = ParticleEmitterRules::Lifetime::Looping;
        emitter.activeTime = expression(body.get("active_time"), 10.0);
        emitter.sleepTime = expression(body.get("sleep_time"), 0.0);
    } else if (name == "minecraft:emitter_lifetime_expression") {
        emitter.lifetime = ParticleEmitterRules::Lifetime::Expression;
        emitter.activation = expression(body.get("activation_expression"), 1.0);
        emitter.expiration = expression(body.get("expiration_expression"), 0.0);
    } else if (name == "minecraft:emitter_shape_point") {
        readShape(body, ParticleShape::Kind::Point, effect.shape);
    } else if (name == "minecraft:emitter_shape_sphere") {
        readShape(body, ParticleShape::Kind::Sphere, effect.shape);
    } else if (name == "minecraft:emitter_shape_box") {
        readShape(body, ParticleShape::Kind::Box, effect.shape);
    } else if (name == "minecraft:emitter_shape_disc") {
        readShape(body, ParticleShape::Kind::Disc, effect.shape);
    } else if (name == "minecraft:emitter_shape_entity_aabb") {
        readShape(body, ParticleShape::Kind::EntityBox, effect.shape);
    } else if (name == "minecraft:emitter_local_space") {
        emitter.localPosition = readBool(body.get("position"), false);
        emitter.localRotation = readBool(body.get("rotation"), false);
        emitter.localVelocity = readBool(body.get("velocity"), false);
    } else if (name == "minecraft:emitter_initialization") {
        emitter.initialization = readStatements(body.get("creation_expression"));
        emitter.perUpdate = readStatements(body.get("per_update_expression"));
    } else if (name == "minecraft:particle_initial_speed") {
        if (body.isArray()) {
            motion.initialSpeedIsVector = true;
            readVector(&body, motion.initialSpeedVector);
        } else {
            motion.initialSpeedIsVector = false;
            motion.initialSpeed = expression(&body);
        }
    } else if (name == "minecraft:particle_initial_spin") {
        motion.initialRotation = expression(body.get("rotation"));
        motion.initialRotationRate = expression(body.get("rotation_rate"));
    } else if (name == "minecraft:particle_motion_dynamic") {
        readVector(body.get("linear_acceleration"), motion.linearAcceleration);
        motion.linearDragCoefficient = expression(body.get("linear_drag_coefficient"));
        motion.rotationAcceleration = expression(body.get("rotation_acceleration"));
        motion.rotationDragCoefficient = expression(body.get("rotation_drag_coefficient"));
    } else if (name == "minecraft:particle_motion_parametric") {
        motion.parametric = true;
        readVector(body.get("relative_position"), motion.relativePosition);
        readVector(body.get("direction"), motion.direction);
        motion.rotation = expression(body.get("rotation"));
    } else if (name == "minecraft:particle_motion_collision") {
        motion.collides = true;
        motion.collisionEnabled = expression(body.get("enabled"), 1.0);
        motion.collisionRadius = constantNumber(body.get("collision_radius"), 0.0f);
        motion.collisionDrag = constantNumber(body.get("collision_drag"), 0.0f);
        motion.coefficientOfRestitution = constantNumber(body.get("coefficient_of_restitution"), 0.0f);
        motion.expireOnContact = readBool(body.get("expire_on_contact"), false);
        readCollisionEvents(body.get("events"), effect.events);
    } else if (name == "minecraft:particle_kill_plane") {
        if (body.isArray() && body.mArray.size() >= 4) {
            effect.lifetime.hasKillPlane = true;
            for (size_t index = 0; index < 4; ++index) {
                effect.lifetime.killPlane[index] = constantNumber(body.mArray[index].get(), 0.0f);
            }
        }
    } else if (name == "minecraft:particle_lifetime_events") {
        effect.events.particleCreation = readNames(body.get("creation_event"));
        effect.events.particleExpiration = readNames(body.get("expiration_event"));
        effect.events.particleTimeline = readTimeline(body.get("timeline"));
    } else if (name == "minecraft:emitter_lifetime_events") {
        effect.events.emitterCreation = readNames(body.get("creation_event"));
        effect.events.emitterExpiration = readNames(body.get("expiration_event"));
        effect.events.emitterTimeline = readTimeline(body.get("timeline"));
    } else if (name == "minecraft:particle_lifetime_expression") {
        effect.lifetime.maxLifetime = expression(body.get("max_lifetime"), 1.0);
        effect.lifetime.expiration = expression(body.get("expiration_expression"), 0.0);
    } else if (name == "minecraft:particle_expire_if_in_blocks") {
        effect.lifetime.expireInBlocks = readBlockNames(&body);
    } else if (name == "minecraft:particle_expire_if_not_in_blocks") {
        effect.lifetime.expireOutsideBlocks = readBlockNames(&body);
    } else if (name == "minecraft:particle_appearance_billboard") {
        readBillboard(body, effect.appearance);
    } else if (name == "minecraft:particle_appearance_tinting") {
        readTinting(body, effect.appearance);
    } else if (name == "minecraft:particle_appearance_lighting") {
        effect.appearance.lit = true;
    } else {
        effect.unsupported.push_back(name);
    }
}

ParticleMaterial materialOf(const std::string& material)
{
    std::string name = lowercase(material);
    if (name == "particles_blend") {
        return ParticleMaterial::Blend;
    }
    if (name == "particles_add") {
        return ParticleMaterial::Add;
    }
    return ParticleMaterial::AlphaTest;
}

/**
 * Reads one particles/*.json file into the effect it defines; false when the
 * text holds no effect with an identifier.
 */
bool parseEffect(const std::string& text, ParticleEffect& effect)
{
    if (isParticleBinary(text)) {
        std::optional<std::string> converted = particleBinaryToJson(text);
        return converted && parseEffect(*converted, effect);
    }
    std::unique_ptr<json::Value> document = json::parse(stripJsonComments(text));
    if (!document || !document->isObject()) {
        return false;
    }
    const json::Value* root = document->get("particle_effect");
    if (!root || !root->isObject()) {
        return false;
    }
    const json::Value* description = root->get("description");
    if (!description || !description->isObject()) {
        return false;
    }
    const json::Value* identifier = description->get("identifier");
    if (!identifier || !identifier->isString() || identifier->mString.empty()) {
        return false;
    }
    effect.identifier = identifier->mString;
    if (const json::Value* render = description->get("basic_render_parameters"); render && render->isObject()) {
        if (const json::Value* material = render->get("material"); material && material->isString()) {
            effect.material = materialOf(material->mString);
        }
        if (const json::Value* texture = render->get("texture"); texture && texture->isString()) {
            effect.texture = texture->mString;
        }
    }
    if (const json::Value* curves = root->get("curves"); curves && curves->isObject()) {
        for (const std::string& key : curves->mKeys) {
            std::string raw;
            writeJson(*curves->mObject.at(key), raw);
            effect.curves[key] = std::move(raw);
        }
    }
    if (const json::Value* events = root->get("events"); events && events->isObject()) {
        readEvents(*events, effect.events);
    }
    effect.lifetime.maxLifetime = ParticleExpression(1.0);
    effect.emitter.numParticles = ParticleExpression(10.0);
    effect.emitter.activeTime = ParticleExpression(10.0);
    effect.shape.planeNormal = { ParticleExpression(0.0), ParticleExpression(1.0), ParticleExpression(0.0) };
    effect.appearance.customDirection = { ParticleExpression(0.0), ParticleExpression(1.0), ParticleExpression(0.0) };
    effect.appearance.color = { ParticleExpression(1.0), ParticleExpression(1.0), ParticleExpression(1.0), ParticleExpression(1.0) };
    if (const json::Value* components = root->get("components"); components && components->isObject()) {
        for (const std::string& key : components->mKeys) {
            readComponent(key, *components->mObject.at(key), effect);
        }
    }
    return true;
}

}

void ParticleLibrary::load(PackSource& game, const std::vector<std::shared_ptr<const PackFiles>>& packs)
{
    effects.clear();
    for (const std::string& name : game.archiveEntries("particles")) {
        std::string text;
        if (!game.readBaseArchived("particles", name, text)) {
            continue;
        }
        ParticleEffect effect;
        if (parseEffect(text, effect)) {
            std::string identifier = effect.identifier;
            effects[identifier] = std::move(effect);
        }
    }
    for (auto layer = packs.rbegin(); layer != packs.rend(); ++layer) {
        const std::shared_ptr<const PackFiles>& pack = *layer;
        if (!pack) {
            continue;
        }
        for (const auto& [path, content] : pack->files) {
            if (!startsWith(path, "particles/") || !endsWith(path, ".json")) {
                continue;
            }
            ParticleEffect effect;
            if (parseEffect(content, effect)) {
                std::string identifier = effect.identifier;
                effects[identifier] = std::move(effect);
            }
        }
    }
}

const ParticleEffect* ParticleLibrary::find(const std::string& identifier) const
{
    auto found = effects.find(identifier);
    return found == effects.end() ? nullptr : &found->second;
}

}
