#include "client/DebugShapes.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace kestrel {

namespace {

constexpr float LineWidth = 0.03f;
constexpr int DefaultSegments = 20;
constexpr int MaxSegments = 128;
constexpr float DefaultHeadLength = 0.5f;
constexpr float DefaultHeadRadius = 0.25f;
constexpr int DefaultHeadSegments = 4;

using Segments = std::vector<std::pair<mod::Vec3, mod::Vec3>>;

mod::Color colorOf(uint32_t argb)
{
    return { static_cast<uint8_t>(argb >> 16), static_cast<uint8_t>(argb >> 8), static_cast<uint8_t>(argb), static_cast<uint8_t>(argb >> 24) };
}

/**
 * Turns an offset by the shape's rotation in degrees: roll about z, then
 * pitch about x, then yaw about y.
 */
struct Rotation {
    explicit Rotation(const Vector3f& degrees)
    {
        double toRadians = std::numbers::pi / 180.0;
        pitch = degrees.x * toRadians;
        yaw = degrees.y * toRadians;
        roll = degrees.z * toRadians;
    }

    mod::Vec3 apply(const mod::Vec3& offset) const
    {
        double x = offset.x * std::cos(roll) - offset.y * std::sin(roll);
        double y = offset.x * std::sin(roll) + offset.y * std::cos(roll);
        double z = offset.z;
        double y2 = y * std::cos(pitch) - z * std::sin(pitch);
        double z2 = y * std::sin(pitch) + z * std::cos(pitch);
        double x3 = x * std::cos(yaw) + z2 * std::sin(yaw);
        double z3 = -x * std::sin(yaw) + z2 * std::cos(yaw);
        return { x3, y2, z3 };
    }

    double pitch = 0.0;
    double yaw = 0.0;
    double roll = 0.0;
};

class ShapeBuilder {
public:
    ShapeBuilder(const mod::Vec3& origin, const Rotation& rotation)
        : origin(origin)
        , rotation(rotation)
    {
    }

    mod::Vec3 at(double x, double y, double z) const
    {
        mod::Vec3 turned = rotation.apply({ x, y, z });
        return { origin.x + turned.x, origin.y + turned.y, origin.z + turned.z };
    }

    void line(const mod::Vec3& from, const mod::Vec3& to)
    {
        segments.emplace_back(from, to);
    }

    void ellipse(double centerY, double radiusX, double radiusZ, int count)
    {
        for (int i = 0; i < count; ++i) {
            double a = 2.0 * std::numbers::pi * i / count;
            double b = 2.0 * std::numbers::pi * (i + 1) / count;
            line(at(std::cos(a) * radiusX, centerY, std::sin(a) * radiusZ), at(std::cos(b) * radiusX, centerY, std::sin(b) * radiusZ));
        }
    }

    void verticalEllipse(double radiusA, double radiusY, int count, bool alongX)
    {
        for (int i = 0; i < count; ++i) {
            double a = 2.0 * std::numbers::pi * i / count;
            double b = 2.0 * std::numbers::pi * (i + 1) / count;
            mod::Vec3 from = alongX ? at(std::cos(a) * radiusA, std::sin(a) * radiusY, 0.0) : at(0.0, std::sin(a) * radiusY, std::cos(a) * radiusA);
            mod::Vec3 to = alongX ? at(std::cos(b) * radiusA, std::sin(b) * radiusY, 0.0) : at(0.0, std::sin(b) * radiusY, std::cos(b) * radiusA);
            line(from, to);
        }
    }

    mod::Vec3 origin;
    const Rotation& rotation;
    Segments segments;
};

int segmentsOf(int requested)
{
    return requested > 2 ? std::min(requested, MaxSegments) : DefaultSegments;
}

void buildBox(ShapeBuilder& shape, const Vector3f& bounds, double scale)
{
    double sx = bounds.x * scale;
    double sy = bounds.y * scale;
    double sz = bounds.z * scale;
    mod::Vec3 c[8] = {
        shape.at(0, 0, 0), shape.at(sx, 0, 0), shape.at(sx, 0, sz), shape.at(0, 0, sz),
        shape.at(0, sy, 0), shape.at(sx, sy, 0), shape.at(sx, sy, sz), shape.at(0, sy, sz),
    };
    for (int i = 0; i < 4; ++i) {
        shape.line(c[i], c[(i + 1) % 4]);
        shape.line(c[i + 4], c[(i + 1) % 4 + 4]);
        shape.line(c[i], c[i + 4]);
    }
}

void buildArrow(ShapeBuilder& shape, const DebugShapeData& data, const mod::Vec3& start, const mod::Vec3& end)
{
    shape.line(start, end);
    double dx = end.x - start.x;
    double dy = end.y - start.y;
    double dz = end.z - start.z;
    double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (length < 1e-6) {
        return;
    }
    dx /= length;
    dy /= length;
    dz /= length;
    double headLength = data.mHasArrowHeadLength ? data.mArrowHeadLength : DefaultHeadLength;
    double headRadius = data.mHasArrowHeadRadius ? data.mArrowHeadRadius : DefaultHeadRadius;
    int count = data.mHasArrowHeadSegments && data.mArrowHeadSegments > 2 ? std::min<int>(data.mArrowHeadSegments, MaxSegments) : DefaultHeadSegments;
    double ux = std::abs(dy) < 0.9 ? 0.0 : 1.0;
    double uy = std::abs(dy) < 0.9 ? 1.0 : 0.0;
    double ax = uy * dz;
    double ay = -ux * dz;
    double az = ux * dy - uy * dx;
    double axisLength = std::sqrt(ax * ax + ay * ay + az * az);
    ax /= axisLength;
    ay /= axisLength;
    az /= axisLength;
    double bx = dy * az - dz * ay;
    double by = dz * ax - dx * az;
    double bz = dx * ay - dy * ax;
    mod::Vec3 base { end.x - dx * headLength, end.y - dy * headLength, end.z - dz * headLength };
    std::vector<mod::Vec3> rim;
    for (int i = 0; i < count; ++i) {
        double angle = 2.0 * std::numbers::pi * i / count;
        double c = std::cos(angle) * headRadius;
        double s = std::sin(angle) * headRadius;
        rim.push_back({ base.x + ax * c + bx * s, base.y + ay * c + by * s, base.z + az * c + bz * s });
    }
    for (size_t i = 0; i < rim.size(); ++i) {
        shape.line(rim[i], end);
        shape.line(rim[i], rim[(i + 1) % rim.size()]);
    }
}

void buildCylinder(ShapeBuilder& shape, const DebugShapeData& data, double scale)
{
    int count = segmentsOf(data.mSegments);
    double height = data.mHeight * scale;
    double bottomX = data.mRadiusX.x * scale;
    double topX = data.mRadiusX.y * scale;
    double bottomZ = data.mRadiusZ.x * scale;
    double topZ = data.mRadiusZ.y * scale;
    shape.ellipse(0.0, bottomX, bottomZ, count);
    shape.ellipse(height, topX, topZ, count);
    for (int i = 0; i < 4; ++i) {
        double angle = std::numbers::pi * 0.5 * i;
        shape.line(shape.at(std::cos(angle) * bottomX, 0.0, std::sin(angle) * bottomZ), shape.at(std::cos(angle) * topX, height, std::sin(angle) * topZ));
    }
}

void buildPyramid(ShapeBuilder& shape, const DebugShapeData& data, double scale)
{
    double halfWidth = data.mWidth * scale * 0.5;
    double halfDepth = (data.mHasDepth ? data.mDepth : data.mWidth) * scale * 0.5;
    double height = data.mHeight * scale;
    mod::Vec3 corners[4] = {
        shape.at(-halfWidth, 0, -halfDepth), shape.at(halfWidth, 0, -halfDepth), shape.at(halfWidth, 0, halfDepth), shape.at(-halfWidth, 0, halfDepth),
    };
    mod::Vec3 apex = shape.at(0, height, 0);
    for (int i = 0; i < 4; ++i) {
        shape.line(corners[i], corners[(i + 1) % 4]);
        shape.line(corners[i], apex);
    }
}

void buildCone(ShapeBuilder& shape, const DebugShapeData& data, double scale)
{
    int count = segmentsOf(data.mSegments);
    double radiusX = data.mRadii2f.x * scale;
    double radiusZ = data.mRadii2f.y * scale;
    double height = data.mHeight * scale;
    shape.ellipse(0.0, radiusX, radiusZ, count);
    mod::Vec3 apex = shape.at(0, height, 0);
    for (int i = 0; i < 4; ++i) {
        double angle = std::numbers::pi * 0.5 * i;
        shape.line(shape.at(std::cos(angle) * radiusX, 0.0, std::sin(angle) * radiusZ), apex);
    }
}

const ActorView* findActor(const SessionSnapshot& snapshot, uint64_t runtime)
{
    for (const ActorView& actor : snapshot.actors) {
        if (actor.runtimeId == runtime) {
            return &actor;
        }
    }
    return nullptr;
}

}

void drawDebugShapes(mod::WorldPainter& painter, const SessionSnapshot& snapshot, double now)
{
    mod::Vec3 camera = painter.camera();
    for (const DebugShapeView& view : snapshot.debugShapes) {
        const DebugShapeData& data = view.data;
        if (view.expires >= 0.0 && now >= view.expires) {
            continue;
        }
        if (data.mHasDimension && data.mDimension != snapshot.dimension) {
            continue;
        }
        mod::Vec3 origin { data.mPosition.x, data.mPosition.y, data.mPosition.z };
        if (data.mHasAttachedToActorId) {
            const ActorView* actor = findActor(snapshot, view.attachedRuntime);
            if (!actor) {
                continue;
            }
            origin = { origin.x + actor->x, origin.y + actor->y, origin.z + actor->z };
        }
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) {
            continue;
        }
        if (data.mHasMaximumRenderDistance && data.mMaximumRenderDistance > 0.0f) {
            double dx = origin.x - camera.x;
            double dy = origin.y - camera.y;
            double dz = origin.z - camera.z;
            if (dx * dx + dy * dy + dz * dz > static_cast<double>(data.mMaximumRenderDistance) * data.mMaximumRenderDistance) {
                continue;
            }
        }

        double scale = data.mHasScale && std::isfinite(data.mScale) ? data.mScale : 1.0;
        mod::Color color = data.mHasColor ? colorOf(data.mColor) : mod::Color { 255, 255, 255, 255 };
        Rotation rotation(data.mHasRotation ? data.mRotation : Vector3f {});
        ShapeBuilder shape(origin, rotation);

        switch (data.mType) {
        case DebugShapeType::Line:
            shape.line(origin, { data.mLineEndPosition.x, data.mLineEndPosition.y, data.mLineEndPosition.z });
            break;
        case DebugShapeType::Box:
            buildBox(shape, data.mBoxBounds, scale);
            break;
        case DebugShapeType::Sphere:
            shape.ellipse(0.0, scale, scale, segmentsOf(data.mSegments));
            shape.verticalEllipse(scale, scale, segmentsOf(data.mSegments), true);
            shape.verticalEllipse(scale, scale, segmentsOf(data.mSegments), false);
            break;
        case DebugShapeType::Circle:
            shape.ellipse(0.0, scale, scale, segmentsOf(data.mSegments));
            break;
        case DebugShapeType::Text:
            if (!data.mText.empty()) {
                painter.text3d(origin, data.mText, color, static_cast<float>(scale), !data.mDepthTest);
            }
            break;
        case DebugShapeType::Arrow: {
            mod::Vec3 end = data.mHasArrowEndPosition ? mod::Vec3 { data.mArrowEndPosition.x, data.mArrowEndPosition.y, data.mArrowEndPosition.z } : origin;
            buildArrow(shape, data, origin, end);
            break;
        }
        case DebugShapeType::Cylinder:
            buildCylinder(shape, data, scale);
            break;
        case DebugShapeType::Pyramid:
            buildPyramid(shape, data, scale);
            break;
        case DebugShapeType::Ellipsoid: {
            int count = segmentsOf(data.mSegments);
            double rx = data.mRadii3f.x * scale;
            double ry = data.mRadii3f.y * scale;
            double rz = data.mRadii3f.z * scale;
            shape.ellipse(0.0, rx, rz, count);
            shape.verticalEllipse(rx, ry, count, true);
            shape.verticalEllipse(rz, ry, count, false);
            break;
        }
        case DebugShapeType::Cone:
            buildCone(shape, data, scale);
            break;
        }
        if (!shape.segments.empty()) {
            painter.lines(shape.segments, color, LineWidth, false);
        }
    }
}

}
