#pragma once

#include "mod/Types.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel::mod {

enum class ShaderBlend {
    Opaque,
    Alpha,
    Premultiplied,
    // Scales what is under it by the shader's color.
    Multiply,
};

/**
 * The code of a custom shader, one version per graphics backend: SPIR-V for
 * Vulkan (Linux), HLSL for Direct3D 12 (Windows) and Metal Shading Language
 * for macOS. Only the one for the running backend is needed, the rest can
 * stay empty. The entry points are vs_main and ps_main (main in GLSL).
 *
 * Vertices come in as position (float3, location 0 / POSITION), uv (float2,
 * location 1 / TEXCOORD0) and color (unorm4, location 2 / COLOR). The 32
 * push constants (cbuffer b0, Metal buffer(1)) are:
 *
 *   float4x4 transform   position to clip space, transform * position
 *   float    seconds     since Kestrel started
 *   float2   resolution  frame size in pixels
 *   float    unused
 *   float4   params[3]   whatever the draw passed as ShaderParams
 *
 * Clip space is y up like Direct3D and Metal; GLSL for Vulkan flips it the
 * way Kestrel's own shaders do: gl_Position.y = -gl_Position.y.
 */
struct ShaderSource {
    std::vector<uint32_t> spirvVertex;
    std::vector<uint32_t> spirvFragment;
    std::string hlsl;
    std::string metal;
    ShaderBlend blend = ShaderBlend::Alpha;

    /**
     * Reads a .spv file as glslc writes it; empty when it cannot be read.
     */
    static std::vector<uint32_t> loadSpirv(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::vector<char> bytes { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
        std::copy_n(bytes.data(), words.size() * sizeof(uint32_t), reinterpret_cast<char*>(words.data()));
        return words;
    }

    static std::string loadText(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    }
};

class Shader {
public:
    virtual ~Shader() = default;

    /**
     * False when the backend had nothing to build from or the code failed
     * to compile; error says why. Drawing an invalid shader does nothing.
     */
    virtual bool valid() const = 0;
    virtual const std::string& error() const = 0;
};

/**
 * The twelve floats a draw hands its shader as params[0..2].
 */
struct ShaderParams {
    std::array<float, 12> values {};

    ShaderParams() = default;

    ShaderParams(std::initializer_list<float> list)
    {
        std::copy_n(list.begin(), std::min(list.size(), values.size()), values.begin());
    }
};

class Shaders {
public:
    virtual ~Shaders() = default;

    /**
     * Builds a shader. It lives as long as the returned pointer or the mod,
     * whichever goes first.
     */
    virtual std::shared_ptr<Shader> create(const ShaderSource& source) = 0;

    /**
     * "Vulkan", "Direct3D 12" or "Metal", to pick which code to load.
     */
    virtual std::string_view backend() const = 0;
};

struct ShaderVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    Color color { 255, 255, 255, 255 };
};

struct WorldVertex {
    Vec3 position;
    float u = 0.0f;
    float v = 0.0f;
    Color color { 255, 255, 255, 255 };
};

/**
 * Draws custom shaded geometry into the world, depth tested against it.
 * Positions are world coordinates.
 */
class WorldPainter {
public:
    virtual ~WorldPainter() = default;

    virtual Vec3 camera() const = 0;

    // A triangle list, three vertices per triangle.
    virtual void triangles(const Shader& shader, const std::vector<WorldVertex>& vertices, const ShaderParams& params = {}) = 0;

    /**
     * Corners go around the quad; uvs run from (0, 0) at the first to (1, 1)
     * at the third.
     */
    void quad(const Shader& shader, const std::array<Vec3, 4>& corners, const ShaderParams& params = {}, Color color = { 255, 255, 255, 255 })
    {
        const float us[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
        const float vs[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        std::vector<WorldVertex> vertices;
        vertices.reserve(6);
        for (int corner : { 0, 1, 2, 0, 2, 3 }) {
            vertices.push_back({ corners[corner], us[corner], vs[corner], color });
        }
        triangles(shader, vertices, params);
    }

    void box(const Shader& shader, const Vec3& min, const Vec3& max, const ShaderParams& params = {}, Color color = { 255, 255, 255, 255 })
    {
        const Vec3 c[8] = {
            { min.x, min.y, min.z }, { max.x, min.y, min.z }, { max.x, max.y, min.z }, { min.x, max.y, min.z },
            { min.x, min.y, max.z }, { max.x, min.y, max.z }, { max.x, max.y, max.z }, { min.x, max.y, max.z },
        };
        const int faces[6][4] = { { 0, 1, 2, 3 }, { 5, 4, 7, 6 }, { 4, 0, 3, 7 }, { 1, 5, 6, 2 }, { 3, 2, 6, 7 }, { 4, 5, 1, 0 } };
        for (const auto& face : faces) {
            quad(shader, { c[face[0]], c[face[1]], c[face[2]], c[face[3]] }, params, color);
        }
    }
};

}
