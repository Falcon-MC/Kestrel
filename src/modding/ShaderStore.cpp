#include "ShaderStore.h"

namespace kestrel::modding {

/**
 * What a mod holds on to. It destroys its pipeline when the last pointer
 * goes, unless the store went first.
 */
class ShaderStore::Program final : public mod::Shader {
public:
    Program(ShaderStore& store, uint32_t id, std::string error)
        : store(&store)
        , alive(store.alive)
        , id(id)
        , failure(std::move(error))
    {
    }

    ~Program() override
    {
        std::shared_ptr<bool> owner = alive.lock();
        if (owner && *owner && id != 0) {
            store->destroy(id);
        }
    }

    bool valid() const override
    {
        return id != 0 && !alive.expired();
    }

    const std::string& error() const override
    {
        return failure;
    }

    uint32_t pipeline() const
    {
        return valid() ? id : 0;
    }

    void forget()
    {
        id = 0;
        failure = "The mod that made this shader was unloaded";
    }

private:
    ShaderStore* store;
    std::weak_ptr<bool> alive;
    uint32_t id;
    std::string failure;
};

uint32_t packColor(mod::Color color)
{
    return color.packed();
}

ShaderStore::ShaderStore(Renderer& renderer)
    : renderer(renderer)
{
}

ShaderStore::~ShaderStore()
{
    *alive = false;
    for (auto& [id, entry] : programs) {
        if (std::shared_ptr<Program> program = entry.second.lock()) {
            program->forget();
        }
        renderer.destroyShader(id);
    }
}

std::shared_ptr<mod::Shader> ShaderStore::create(size_t owner, const mod::ShaderSource& source)
{
    ShaderSource code { source.spirvVertex, source.spirvFragment, source.hlsl, source.metal };
    std::string error;
    uint32_t id = renderer.createShader(code, static_cast<CustomBlend>(source.blend), error);
    auto program = std::make_shared<Program>(*this, id, std::move(error));
    if (id != 0) {
        programs[id] = { owner, program };
    }
    return program;
}

void ShaderStore::release(size_t owner)
{
    for (auto entry = programs.begin(); entry != programs.end();) {
        if (entry->second.first != owner) {
            ++entry;
            continue;
        }
        if (std::shared_ptr<Program> program = entry->second.second.lock()) {
            program->forget();
        }
        renderer.destroyShader(entry->first);
        entry = programs.erase(entry);
    }
}

std::string_view ShaderStore::backend() const
{
    return renderer.backendName();
}

void ShaderStore::queue(CustomLayer layer, const mod::Shader& shader, const CustomVertex* vertices, size_t count, const mod::ShaderParams& params)
{
    const auto* program = dynamic_cast<const Program*>(&shader);
    if (!program || program->pipeline() == 0 || count == 0) {
        return;
    }
    Batch& batch = batches[static_cast<size_t>(layer)];
    CustomDraw draw;
    draw.shader = program->pipeline();
    draw.firstVertex = static_cast<uint32_t>(batch.vertices.size());
    draw.vertexCount = static_cast<uint32_t>(count - count % 3);
    draw.params = params.values;
    batch.vertices.insert(batch.vertices.end(), vertices, vertices + count);
    batch.draws.push_back(draw);
}

void ShaderStore::submit(CustomLayer layer, const std::array<float, 16>& transform, float seconds)
{
    Batch& batch = batches[static_cast<size_t>(layer)];
    renderer.drawCustom(layer, batch.vertices, batch.draws, transform, seconds);
    batch.vertices.clear();
    batch.draws.clear();
}

void ShaderStore::clear()
{
    for (Batch& batch : batches) {
        batch.vertices.clear();
        batch.draws.clear();
    }
}

void ShaderStore::destroy(uint32_t id)
{
    if (programs.erase(id) != 0) {
        renderer.destroyShader(id);
    }
}

}
