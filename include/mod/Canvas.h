#pragma once

#include "mod/Shaders.h"
#include "mod/Types.h"

#include <string_view>
#include <vector>

namespace kestrel::mod {

enum class TextStyle {
    // The game's pixel font, what the HUD uses.
    Pixel,
    Ui,
    UiSmall,
    UiLarge,
    Heading,
};

/**
 * Draws on top of the game's HUD. Coordinates are interface units from the
 * top left corner, the same units the HUD itself is laid out in.
 */
class Canvas {
public:
    virtual ~Canvas() = default;

    virtual float width() const = 0;
    virtual float height() const = 0;

    virtual void fill(const Rect& rect, Color color) = 0;
    virtual void outline(const Rect& rect, Color color, float thickness = 1.0f) = 0;
    virtual void text(std::string_view value, float x, float y, Color color, TextStyle style = TextStyle::Pixel, bool shadow = true) = 0;
    virtual void textCentered(std::string_view value, const Rect& rect, Color color, TextStyle style = TextStyle::Pixel) = 0;
    virtual float measure(std::string_view value, TextStyle style = TextStyle::Pixel) const = 0;
    virtual float lineHeight(TextStyle style = TextStyle::Pixel) const = 0;

    /**
     * A texture of the game's UI by name, like "textures/ui/heart" or
     * "textures/ui/hotbar_start_cap".
     */
    virtual void sprite(const Rect& rect, std::string_view name, Color tint = { 255, 255, 255, 255 }) = 0;
    virtual void nineSlice(const Rect& rect, std::string_view name, Color tint = { 255, 255, 255, 255 }) = 0;

    virtual void setClip(const Rect& rect) = 0;
    virtual void clearClip() = 0;

    /**
     * Custom shaded triangles in interface units. They go under the HUD, or
     * over everything with aboveHud, and ignore the clip.
     */
    virtual void shaderTriangles(const Shader& shader, const std::vector<ShaderVertex>& vertices, const ShaderParams& params = {}, bool aboveHud = false) = 0;

    /**
     * A rectangle with uvs from (0, 0) at the top left to (1, 1). Covering
     * the whole canvas makes a full screen effect.
     */
    void shader(const Shader& shader, const Rect& rect, const ShaderParams& params = {}, bool aboveHud = false)
    {
        std::vector<ShaderVertex> vertices {
            { rect.x, rect.y, 0.0f, 0.0f }, { rect.right(), rect.y, 1.0f, 0.0f }, { rect.right(), rect.bottom(), 1.0f, 1.0f },
            { rect.x, rect.y, 0.0f, 0.0f }, { rect.right(), rect.bottom(), 1.0f, 1.0f }, { rect.x, rect.bottom(), 0.0f, 1.0f },
        };
        shaderTriangles(shader, vertices, params, aboveHud);
    }
};

}
