#pragma once

#include "mod/Shaders.h"
#include "render/Renderer.h"

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace kestrel::modding {

/**
 * The mods' shaders and the custom draws queued for the current frame, one
 * batch per layer.
 */
class ShaderStore {
public:
    explicit ShaderStore(Renderer& renderer);
    ~ShaderStore();

    ShaderStore(const ShaderStore&) = delete;
    ShaderStore& operator=(const ShaderStore&) = delete;

    std::shared_ptr<mod::Shader> create(size_t owner, const mod::ShaderSource& source, bool post);
    void release(size_t owner);
    std::string_view backend() const;

    /**
     * Queues a triangle list; positions are already in the layer's space.
     */
    void queue(CustomLayer layer, const mod::Shader& shader, const CustomVertex* vertices, size_t count, const mod::ShaderParams& params);
    void submit(CustomLayer layer, const std::array<float, 16>& transform, float seconds);
    void queuePost(const mod::Shader& shader, const mod::ShaderParams& params, bool keepInput);
    void submitPost(const std::array<float, 16>& inverseViewProjection, float seconds);
    bool supportsPost() const;
    void clear();

private:
    class Program;

    struct Batch {
        std::vector<CustomVertex> vertices;
        std::vector<CustomDraw> draws;
    };

    void destroy(uint32_t id);

    Renderer& renderer;
    std::map<uint32_t, std::pair<size_t, std::weak_ptr<Program>>> programs;
    std::array<Batch, CustomLayerCount> batches;
    std::vector<CustomDraw> postPasses;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

uint32_t packColor(mod::Color color);

}
