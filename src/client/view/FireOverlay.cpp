#include "client/FireOverlay.h"
#include "Protocol/BlockStateHasher.h"

#include "ui/DrawList.h"
#include "ui/Skin.h"
#include "world/BlockAssets.h"

namespace kestrel {

namespace {

constexpr const char* FireSprite = "kestrel/fire_overlay";

}

void FireOverlay::load(ui::Skin& skin, const std::shared_ptr<const world::BlockAssets>& assets)
{
    source = assets;
    frames = 0;
    skin.clearDynamic(FireSprite);
    Tag states = Tag::ofCompound();
    states.putInt("age", 0);
    auto value = assets->networkValueForState(uint32_t(BlockStateHasher::hash("minecraft:fire", states)), true, nullptr);
    if (!value) return;
    const auto& visual = assets->visual(*value, true);
    if ((visual.flags & world::FlagDiagnostic) || visual.modelTemplate >= assets->modelTemplates().size()) return;
    const auto& model = assets->modelTemplates()[visual.modelTemplate];
    const auto& quads = assets->modelQuads();
    const auto& materials = assets->materials();
    if (model.quadStart > quads.size() || model.quadCount > quads.size() - model.quadStart) return;
    const world::Material* material = nullptr;
    for (uint32_t index = 0; index < model.quadCount; ++index) {
        uint32_t id = quads[model.quadStart + index].material;
        if (id < materials.size() && (!material || materials[id].frameCount > material->frameCount)) material = &materials[id];
    }
    if (!material || material->frameCount == 0 || material->frameCount > 64 || material->ticksPerFrame == 0) return;
    const auto& textures = assets->textures();
    constexpr size_t LayerBytes = size_t(world::TextureSize) * world::TextureSize * 4;
    if (material->layer > textures.layers || material->frameCount > textures.layers - material->layer) return;
    size_t begin = size_t(material->layer) * LayerBytes;
    size_t size = size_t(material->frameCount) * LayerBytes;
    if (begin > textures.mips[0].size() || size > textures.mips[0].size() - begin) return;
    ui::Bitmap strip;
    strip.width = world::TextureSize;
    strip.height = world::TextureSize * material->frameCount;
    strip.rgba.assign(textures.mips[0].begin() + begin, textures.mips[0].begin() + begin + size);
    skin.setDynamic(FireSprite, std::move(strip));
    frames = material->frameCount;
    ticksPerFrame = material->ticksPerFrame;
}

void FireOverlay::draw(ui::DrawList& list, ui::Skin& skin, const std::shared_ptr<const world::BlockAssets>& assets, float width, float height, double seconds)
{
    if (!assets || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) return;
    if (source.lock() != assets) load(skin, assets);
    if (!frames) return;
    const auto& sprite = skin.sprite(FireSprite);
    if (!sprite.valid) return;
    uint32_t frame = fireOverlayFrame(seconds, frames, ticksPerFrame);
    float vStep = (sprite.image.v1 - sprite.image.v0) / frames;
    float v0 = sprite.image.v0 + frame * vStep;
    float v1 = v0 + vStep;
    float u0 = sprite.image.u0, u1 = sprite.image.u1;
    for (int side = 0; side < 2; ++side) {
        std::array<std::array<float, 2>, 4> points { { { -0.18f * width, 1.1f * height }, { 0.5f * width, 1.1f * height }, { 0.32f * width, 0.36f * height }, { -0.25f * width, 0.42f * height } } };
        if (side) {
            for (auto& point : points) point[0] = width - point[0];
        }
        const std::array<std::array<float, 2>, 4> uvs { { { u0, v1 }, { u1, v1 }, { u1, v0 }, { u0, v0 } } };
        list.freeQuad(points, uvs, ui::Color { 255, 255, 255, 230 }.packed());
    }
}

}
