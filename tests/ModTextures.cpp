#include "modding/TextureStore.h"
#include "ui/Image.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>

using namespace kestrel;
using Request = modding::TextureRequest;
using Action = Request::Action;

void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

class Api final : public mod::Textures {
public:
    explicit Api(modding::TextureStore& store) : store(store) { }

    size_t owner = 1;

    bool supported() const override
    {
        return send(Action::Supported).result;
    }

    mod::TextureHandle load(const std::filesystem::path& path) override
    {
        Request request;
        request.path = path;
        return created(Action::Load, request);
    }

    mod::TextureHandle decode(std::span<const uint8_t> encoded) override
    {
        Request request;
        request.encoded = encoded;
        return created(Action::Decode, request);
    }

    mod::TextureHandle create(mod::Image image) override
    {
        Request request;
        request.image = std::move(image);
        return created(Action::Create, request);
    }

    mod::TextureInfo info(mod::TextureHandle handle) const override
    {
        Request request = send(Action::Info, handle);
        return { request.image.width, request.image.height, request.result };
    }

    mod::Image read(mod::TextureHandle handle) const override
    {
        Request request = send(Action::Read, handle);
        return request.result ? std::move(request.image) : mod::Image {};
    }

    bool update(mod::TextureHandle handle, mod::Image image) override
    {
        return write(Action::Update, handle, std::move(image), 0, 0);
    }

    bool updateRegion(mod::TextureHandle handle, uint32_t x, uint32_t y, mod::Image image) override
    {
        return write(Action::Patch, handle, std::move(image), x, y);
    }

    bool draw(mod::Canvas& canvas, mod::TextureHandle handle, mod::Rect rect, mod::Color tint = { 255, 255, 255, 255 }) override
    {
        Request request;
        request.action = Action::Draw;
        request.handle = handle;
        request.canvas = &canvas;
        request.rect = rect;
        request.tint = tint;
        store.process(owner, request);
        return request.result;
    }

    bool destroy(mod::TextureHandle handle) override
    {
        return send(Action::Destroy, handle).result;
    }

    void clear() override
    {
        send(Action::Clear);
    }

private:
    Request send(Action action, mod::TextureHandle handle = 0) const
    {
        Request request;
        request.action = action;
        request.handle = handle;
        store.process(owner, request);
        return request;
    }

    mod::TextureHandle created(Action action, Request& request)
    {
        request.action = action;
        store.process(owner, request);
        return request.result ? request.handle : 0;
    }

    bool write(Action action, mod::TextureHandle handle, mod::Image image, uint32_t x, uint32_t y)
    {
        Request request;
        request.action = action;
        request.handle = handle;
        request.image = std::move(image);
        request.x = x;
        request.y = y;
        store.process(owner, request);
        return request.result;
    }

    modding::TextureStore& store;
};

struct Canvas : mod::Canvas {
    int draws = 0;
    std::string name;
    mod::Rect rect;
    mod::Color tint;
    float width() const override { return 640; }
    float height() const override { return 480; }
    void fill(const mod::Rect&, mod::Color) override { }
    void outline(const mod::Rect&, mod::Color, float) override { }
    void text(std::string_view, float, float, mod::Color, mod::TextStyle, bool) override { }
    void textCentered(std::string_view, const mod::Rect&, mod::Color, mod::TextStyle) override { }
    float measure(std::string_view, mod::TextStyle) const override { return 0; }
    float lineHeight(mod::TextStyle) const override { return 8; }
    void sprite(const mod::Rect& area, std::string_view sprite, mod::Color color) override { ++draws; name = sprite; rect = area; tint = color; }
    void nineSlice(const mod::Rect&, std::string_view, mod::Color) override { }
    void setClip(const mod::Rect&) override { }
    void clearClip() override { }
    void shaderTriangles(const mod::Shader&, const std::vector<mod::ShaderVertex>&, const mod::ShaderParams&, bool) override { }
};

mod::Image solid(uint32_t width, uint32_t height, uint8_t value)
{
    return { width, height, std::vector<uint8_t>(size_t(width) * height * 4, value) };
}

int main()
{
    std::map<std::string, mod::Image> uploaded;
    modding::TextureStore store([&](const std::string& name, const mod::Image* image) {
        if (image) uploaded[name] = *image;
        else uploaded.erase(name);
    });
    Api api(store);
    check(api.supported(), "supported textures");
    mod::detail::TextureRequest legacy;
    legacy.action = Action::Create;
    legacy.image = solid(2, 2, 255);
    mod::Event& posted = legacy;
    check(posted.type() == "kestrel:texture_request/v1", "legacy request keeps its event type");
    store.process(1, static_cast<Request&>(posted));
    check(legacy.result && api.info(legacy.handle).width == 2 && api.destroy(legacy.handle) && uploaded.empty(), "legacy request shares the API 4 store");
    check(!api.create({}) && !api.create({ 1, 1, { 0 } }), "invalid pixel sizes");
    check(!api.create({ UINT32_MAX, UINT32_MAX, {} }), "overflow dimensions");
    auto handle = api.create(solid(4, 4, 255));
    check(handle && api.info(handle).width == 4 && uploaded.size() == 1, "RGBA creation");
    auto copy = api.read(handle);
    copy.pixels[0] = 0;
    check(api.read(handle).pixels[0] == 255, "read returns independent pixels");
    check(api.updateRegion(handle, 1, 2, solid(2, 1, 80)), "patch accepted");
    auto image = api.read(handle);
    check(image.pixels[(2 * 4 + 1) * 4] == 80 && image.pixels[(2 * 4 + 3) * 4] == 255 && image.pixels[0] == 255, "patch preserves neighbours");
    check(!api.updateRegion(handle, UINT32_MAX, 0, solid(1, 1, 0)), "reject overflow patch offset");
    check(!api.updateRegion(handle, 3, 3, solid(2, 2, 0)), "reject out of bounds patch");
    check(api.update(handle, solid(2, 3, 90)) && api.info(handle).height == 3, "resize and replace");
    check(!api.update(handle, { 2, 3, {} }) && api.read(handle).pixels[0] == 90, "failed replace preserves content");
    Canvas canvas;
    check(api.draw(canvas, handle, { 10, 20, 30, 40 }, { 10, 20, 30, 80 }), "draw accepted");
    check(canvas.draws == 1 && canvas.rect.w == 30 && canvas.tint.a == 80 && uploaded.contains(canvas.name), "draw uses uploaded sprite and tint");
    check(!api.draw(canvas, handle, { 0, 0, std::numeric_limits<float>::infinity(), 10 }), "reject nonfinite drawing");
    api.owner = 2;
    check(!api.info(handle).valid && api.read(handle).pixels.empty() && !api.destroy(handle)
        && !api.update(handle, solid(1, 1, 0)) && !api.draw(canvas, handle, { 0, 0, 10, 10 }), "owner isolation");
    auto second = api.create(solid(1, 1, 0));
    store.release(1);
    check(uploaded.size() == 1 && api.info(second).valid, "unload removes only owner's images");
    api.clear();
    check(uploaded.empty() && !api.info(second).valid, "clear invalidates and removes sprites");

    auto original = solid(3, 2, 150);
    std::string png = ui::encodePng(original.pixels, original.width, original.height);
    auto decoded = api.decode(std::span(reinterpret_cast<const uint8_t*>(png.data()), png.size()));
    check(decoded > second && api.read(decoded).pixels == original.pixels && api.info(decoded).width == 3, "PNG decode preserves dimensions and alpha");
    check(!api.decode(std::span<const uint8_t>()), "empty image rejected");
    std::vector<uint8_t> garbage { 1, 2, 3, 4 };
    check(!api.decode(garbage), "malformed image rejected");
    std::vector<uint8_t> tga(18, 0);
    tga[2] = 2; tga[12] = 2; tga[14] = 1; tga[16] = 32; tga[17] = 0x28;
    tga.insert(tga.end(), { 30, 20, 10, 80, 60, 50, 40, 120 });
    auto tgaTexture = api.decode(tga);
    check(tgaTexture && api.read(tgaTexture).pixels == std::vector<uint8_t>({ 10, 20, 30, 80, 40, 50, 60, 120 }), "TGA channels and alpha");
    tga[12] = 0xff; tga[13] = 0xff;
    check(!api.decode(tga), "oversized header rejected before decompression");
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels;
    check(!ui::decodeImageLimited(png, 2, width, height, pixels) && pixels.empty(), "dimension cap before decode");
    auto path = std::filesystem::temp_directory_path() / ("kestrel-textures-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    { std::ofstream file(path, std::ios::binary); file.write(png.data(), static_cast<std::streamsize>(png.size())); }
    auto loaded = api.load(path);
    std::filesystem::remove(path);
    check(loaded && api.read(loaded).pixels == original.pixels, "file import");
    check(!api.load(path), "missing file rejected");
    api.clear();
    for (int i = 0; i < 32; ++i) check(api.create(solid(1, 1, 255)), "handle budget accepts limit");
    check(!api.create(solid(1, 1, 255)), "handle budget rejects overflow");
    api.clear();
    auto large = api.create(solid(1024, 1024, 255));
    check(large && api.create(solid(1024, 1024, 255)), "owner byte budget accepts limit");
    check(!api.create(solid(1, 1, 255)), "owner byte budget rejects overflow");
    check(api.update(large, solid(1, 1, 255)) && api.create(solid(1, 1, 255)), "shrinking reclaims budget");
    api.clear();
    check(uploaded.empty(), "all sprites released");
}
