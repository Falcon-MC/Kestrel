#pragma once

#include "ShaderStore.h"

#include "mod/Canvas.h"
#include "ui/Context.h"

namespace kestrel::modding {

/**
 * The HUD canvas mods draw on, over the interface context of the frame.
 */
class UiCanvas final : public mod::Canvas {
public:
    UiCanvas(ui::Context& context, ShaderStore& shaders, float width, float height);

    float width() const override;
    float height() const override;
    void fill(const mod::Rect& rect, mod::Color color) override;
    void outline(const mod::Rect& rect, mod::Color color, float thickness) override;
    void text(std::string_view value, float x, float y, mod::Color color, mod::TextStyle style, bool shadow) override;
    void textCentered(std::string_view value, const mod::Rect& rect, mod::Color color, mod::TextStyle style) override;
    float measure(std::string_view value, mod::TextStyle style) const override;
    float lineHeight(mod::TextStyle style) const override;
    void sprite(const mod::Rect& rect, std::string_view name, mod::Color tint) override;
    void nineSlice(const mod::Rect& rect, std::string_view name, mod::Color tint) override;
    void setClip(const mod::Rect& rect) override;
    void clearClip() override;
    void shaderTriangles(const mod::Shader& shader, const std::vector<mod::ShaderVertex>& vertices, const mod::ShaderParams& params, bool aboveHud) override;

private:
    ui::Context& context;
    ShaderStore& shaders;
    float canvasWidth;
    float canvasHeight;
    std::vector<CustomVertex> scratch;
};

/**
 * World space custom draws, stored relative to the camera so float
 * precision holds far from the origin.
 */
class WorldCanvas final : public mod::WorldPainter {
public:
    WorldCanvas(ShaderStore& shaders, const mod::Vec3& camera);

    mod::Vec3 camera() const override;
    void triangles(const mod::Shader& shader, const std::vector<mod::WorldVertex>& vertices, const mod::ShaderParams& params) override;

private:
    ShaderStore& shaders;
    mod::Vec3 eye;
    std::vector<CustomVertex> scratch;
};

}
