#include "client/Session.h"

#include "Protocol/Packets/PrimitiveShapesPacket.h"

#include <cmath>

namespace kestrel {

namespace {

constexpr size_t MaxDebugShapes = 4096;
constexpr size_t MaxShapeText = 1024;

/**
 * Copies the common fields an update carries over a known shape. A field the
 * server leaves out keeps its last value, as the game does.
 */
void mergeShape(DebugShapeData& into, const DebugShapeData& update)
{
    DebugShapeType previous = into.mType;
    DebugShapeData kept = into;
    into = update;
    if (!update.mHasPosition && kept.mHasPosition) {
        into.mHasPosition = true;
        into.mPosition = kept.mPosition;
    }
    if (!update.mHasScale && kept.mHasScale) {
        into.mHasScale = true;
        into.mScale = kept.mScale;
    }
    if (!update.mHasRotation && kept.mHasRotation) {
        into.mHasRotation = true;
        into.mRotation = kept.mRotation;
    }
    if (!update.mHasMaximumRenderDistance && kept.mHasMaximumRenderDistance) {
        into.mHasMaximumRenderDistance = true;
        into.mMaximumRenderDistance = kept.mMaximumRenderDistance;
    }
    if (!update.mHasColor && kept.mHasColor) {
        into.mHasColor = true;
        into.mColor = kept.mColor;
    }
    if (!update.mHasDimension && kept.mHasDimension) {
        into.mHasDimension = true;
        into.mDimension = kept.mDimension;
    }
    if (!update.mHasAttachedToActorId && kept.mHasAttachedToActorId) {
        into.mHasAttachedToActorId = true;
        into.mAttachedToActorId = kept.mAttachedToActorId;
    }
    if (update.mType == previous && update.mType == DebugShapeType::Arrow) {
        if (!update.mHasArrowEndPosition && kept.mHasArrowEndPosition) {
            into.mHasArrowEndPosition = true;
            into.mArrowEndPosition = kept.mArrowEndPosition;
        }
        if (!update.mHasArrowHeadLength && kept.mHasArrowHeadLength) {
            into.mHasArrowHeadLength = true;
            into.mArrowHeadLength = kept.mArrowHeadLength;
        }
        if (!update.mHasArrowHeadRadius && kept.mHasArrowHeadRadius) {
            into.mHasArrowHeadRadius = true;
            into.mArrowHeadRadius = kept.mArrowHeadRadius;
        }
        if (!update.mHasArrowHeadSegments && kept.mHasArrowHeadSegments) {
            into.mHasArrowHeadSegments = true;
            into.mArrowHeadSegments = kept.mArrowHeadSegments;
        }
    }
}

}

/**
 * Adds, updates or removes the server's debug shapes. A shape sent without a
 * type is a removal; the rest replace or update the shape with that id.
 */
void Session::handlePrimitiveShapes(const PrimitiveShapesPacket& packet)
{
    double now = secondsNow();
    for (const DebugShapeData& shape : packet.mShapes) {
        if (!shape.mHasType) {
            debugShapes.erase(shape.mId);
            continue;
        }
        if (static_cast<int>(shape.mType) < 0 || static_cast<int>(shape.mType) > static_cast<int>(DebugShapeType::Cone)) {
            continue;
        }
        auto found = debugShapes.find(shape.mId);
        if (found == debugShapes.end()) {
            if (debugShapes.size() >= MaxDebugShapes) {
                continue;
            }
            found = debugShapes.emplace(shape.mId, DebugShapeView {}).first;
            found->second.data = shape;
        } else {
            mergeShape(found->second.data, shape);
        }
        DebugShapeView& view = found->second;
        if (view.data.mText.size() > MaxShapeText) {
            view.data.mText.resize(MaxShapeText);
        }
        if (shape.mHasTotalTimeLeft) {
            float left = shape.mTotalTimeLeft;
            view.expires = std::isfinite(left) && left > 0.0f ? now + left : -1.0;
        }
        view.attachedRuntime = 0;
        if (view.data.mHasAttachedToActorId) {
            int64_t unique = static_cast<int64_t>(view.data.mAttachedToActorId);
            if (auto runtime = runtimeByUnique.find(unique); runtime != runtimeByUnique.end()) {
                view.attachedRuntime = runtime->second;
            } else if (unique == localUniqueId) {
                view.attachedRuntime = localRuntimeId;
            }
        }
    }

    std::vector<DebugShapeView> published;
    published.reserve(debugShapes.size());
    for (const auto& [id, view] : debugShapes) {
        published.push_back(view);
    }
    std::lock_guard<std::mutex> guard(mutex);
    current.debugShapes = std::move(published);
}

}
