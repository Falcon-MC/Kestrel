#include "modding/TextureStore.h"
#include "ui/Image.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace kestrel::modding {

namespace {
constexpr uint32_t MaxDimension = 1024;
constexpr size_t MaxEncoded = 16 * 1024 * 1024;
constexpr size_t OwnerBytes = 8 * 1024 * 1024;
constexpr size_t TotalBytes = 16 * 1024 * 1024;

bool valid(const mod::Image& image)
{
    return image.width && image.height && image.width <= MaxDimension && image.height <= MaxDimension
        && image.pixels.size() == size_t(image.width) * image.height * 4;
}
}

TextureStore::~TextureStore()
{
    while (!entries.empty()) remove(entries.begin());
}

bool TextureStore::capacity(size_t owner, size_t bytes, size_t replaced, bool adding) const
{
    size_t total = 0, own = 0, count = 0;
    for (const auto& [id, entry] : entries) {
        total += entry.image.pixels.size();
        if (entry.owner == owner) { own += entry.image.pixels.size(); ++count; }
    }
    return (!adding || (entries.size() < 128 && count < 32))
        && total - replaced + bytes <= TotalBytes && own - replaced + bytes <= OwnerBytes;
}

void TextureStore::remove(std::map<mod::TextureHandle, Entry>::iterator entry)
{
    if (backend) backend(entry->second.name, nullptr);
    entries.erase(entry);
}

void TextureStore::release(size_t owner)
{
    for (auto it = entries.begin(); it != entries.end();) {
        if (it->second.owner == owner) remove(it++);
        else ++it;
    }
}

void TextureStore::process(size_t owner, TextureRequest& request)
{
    using Action = TextureRequest::Action;
    request.result = false;
    if (!backend) return;
    if (request.action == Action::Supported) { request.result = true; return; }
    if (request.action == Action::Clear) { release(owner); request.result = true; return; }
    if (request.action == Action::Load || request.action == Action::Decode || request.action == Action::Create) {
        if (next == std::numeric_limits<mod::TextureHandle>::max()) return;
        if (request.action != Action::Create) {
            std::string encoded;
            if (request.action == Action::Load) {
                std::error_code error;
                if (!std::filesystem::is_regular_file(request.path, error) || error) return;
                auto size = std::filesystem::file_size(request.path, error);
                if (error || size == 0 || size > MaxEncoded) return;
                std::ifstream file(request.path, std::ios::binary);
                if (!file) return;
                encoded.resize(static_cast<size_t>(size));
                if (!file.read(encoded.data(), static_cast<std::streamsize>(size))) return;
            } else {
                if (request.encoded.empty() || request.encoded.size() > MaxEncoded) return;
                encoded.assign(reinterpret_cast<const char*>(request.encoded.data()), request.encoded.size());
            }
            if (!ui::decodeImageLimited(encoded, MaxDimension, request.image.width, request.image.height, request.image.pixels)) return;
        }
        if (!valid(request.image) || !capacity(owner, request.image.pixels.size())) return;
        mod::TextureHandle handle = ++next;
        Entry entry { owner, "mod-image:" + std::to_string(handle), std::move(request.image) };
        backend(entry.name, &entry.image);
        entries.emplace(handle, std::move(entry));
        request.handle = handle;
        request.result = true;
        return;
    }
    auto found = entries.find(request.handle);
    if (found == entries.end() || found->second.owner != owner) return;
    Entry& entry = found->second;
    switch (request.action) {
    case Action::Info:
        request.image = { entry.image.width, entry.image.height, {} };
        break;
    case Action::Read:
        request.image = entry.image;
        break;
    case Action::Update:
        if (!valid(request.image) || !capacity(owner, request.image.pixels.size(), entry.image.pixels.size(), false)) return;
        backend(entry.name, &request.image);
        entry.image = std::move(request.image);
        break;
    case Action::Patch: {
        const auto& patch = request.image;
        if (!valid(patch) || request.x > entry.image.width || request.y > entry.image.height
            || patch.width > entry.image.width - request.x || patch.height > entry.image.height - request.y) return;
        for (uint32_t row = 0; row < patch.height; ++row) {
            auto target = entry.image.pixels.begin() + (size_t(request.y + row) * entry.image.width + request.x) * 4;
            std::copy_n(patch.pixels.begin() + size_t(row) * patch.width * 4, size_t(patch.width) * 4, target);
        }
        backend(entry.name, &entry.image);
        break;
    }
    case Action::Draw:
        if (!request.canvas || !std::isfinite(request.rect.x) || !std::isfinite(request.rect.y)
            || !std::isfinite(request.rect.w) || !std::isfinite(request.rect.h) || request.rect.w <= 0 || request.rect.h <= 0) return;
        request.canvas->sprite(request.rect, entry.name, request.tint);
        break;
    case Action::Destroy:
        remove(found);
        break;
    default: return;
    }
    request.result = true;
}

}
