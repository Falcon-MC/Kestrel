#include "client/FeaturedServers.h"

#include "client/DebugLog.h"
#include "Core/Json/Json.h"
#include "Network/Http/HttpClient.h"
#include "ui/Image.h"

#include <algorithm>
#include <cstdio>
#include <random>

namespace kestrel {

namespace {

constexpr const char* GameVersion = "1.26.51";
constexpr const char* DiscoveryUrl = "https://client.discovery.minecraft-services.net/api/v1.0/discovery/MinecraftPE/builds/";
constexpr const char* ServerScid = "4fc10100-5f7a-4470-899b-280835760c07";
constexpr const char* ServerFilter = "(contentType eq '3PP_V2.0') and platforms/any(tp: tp eq 'android.googleplay') and platforms/any(tp: tp eq 'title.bedrockvanilla')";

std::string randomUuid()
{
    std::random_device device;
    std::mt19937_64 random(device());
    uint64_t high = random();
    uint64_t low = random();
    high = (high & ~0xF000ull) | 0x4000ull;
    low = (low & ~(0xC0ull << 56)) | (0x80ull << 56);
    char text[37];
    std::snprintf(text, sizeof(text), "%08x-%04x-%04x-%04x-%012llx",
        static_cast<unsigned>(high >> 32), static_cast<unsigned>((high >> 16) & 0xFFFF), static_cast<unsigned>(high & 0xFFFF),
        static_cast<unsigned>(low >> 48), static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFull));
    return text;
}

const json::Value* path(const json::Value* value, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        if (!value) {
            return nullptr;
        }
        value = value->get(key);
    }
    return value;
}

std::string text(const json::Value* value)
{
    return value ? value->string() : std::string();
}

std::unique_ptr<json::Value> postJson(const std::string& url, const HttpClient::Headers& extra, const std::string& body)
{
    HttpClient::Headers headers { { "Content-Type", "application/json" }, { "Accept", "application/json" } };
    headers.insert(headers.end(), extra.begin(), extra.end());
    HttpResponse response;
    std::string error;
    if (!HttpClient::post(url, headers, body, response, error) || response.mStatus < 200 || response.mStatus >= 300) {
        debugLog("featured servers: " + url + " failed, " + (error.empty() ? "status " + std::to_string(response.mStatus) : error));
        return nullptr;
    }
    return json::parse(response.mBody);
}

std::string localized(const json::Value* strings, const std::string& language)
{
    if (!strings) {
        return {};
    }
    if (const json::Value* exact = strings->get(language)) {
        return exact->string();
    }
    return text(strings->get("NEUTRAL"));
}

/**
 * Picks the icon and the showcase screenshots out of an item's images. The
 * catalog is loose about types, so icons and banners filed as screenshots
 * are skipped by their tag.
 */
void readImages(const json::Value* images, FeaturedServer& server)
{
    if (!images) {
        return;
    }
    std::string thumbnail;
    std::vector<std::string> activities;
    for (const std::unique_ptr<json::Value>& image : images->mArray) {
        std::string type = text(image->get("Type"));
        std::string tag = text(image->get("Tag"));
        std::string url = text(image->get("Url"));
        if (url.empty()) {
            continue;
        }
        if (type == "Icon") {
            server.iconUrl = url;
        } else if (type == "Thumbnail") {
            thumbnail = url;
        } else if (type == "Screenshot" && tag != "Icon" && tag != "Banner" && tag != "DevActivity") {
            server.showcaseUrls.push_back(url);
        } else if (type == "Activity") {
            activities.push_back(url);
        }
    }
    if (server.iconUrl.empty()) {
        server.iconUrl = thumbnail;
    }
    if (server.showcaseUrls.empty()) {
        server.showcaseUrls = std::move(activities);
    }
}

std::string escapeUrl(const std::string& url)
{
    std::string escaped;
    escaped.reserve(url.size());
    for (char c : url) {
        if (c == ' ') {
            escaped += "%20";
        } else {
            escaped.push_back(c);
        }
    }
    return escaped;
}

/**
 * Scales an image to cover width by height, cropping whatever sticks out,
 * averaging the source texels each target texel covers.
 */
ui::Bitmap cover(const std::vector<uint8_t>& rgba, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t width, uint32_t height)
{
    float fit = std::max(float(width) / float(sourceWidth), float(height) / float(sourceHeight));
    float spanX = float(width) / fit;
    float spanY = float(height) / fit;
    float originX = (float(sourceWidth) - spanX) * 0.5f;
    float originY = (float(sourceHeight) - spanY) * 0.5f;
    ui::Bitmap result { width, height, std::vector<uint8_t>(size_t(width) * height * 4) };
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t sy0 = std::min(sourceHeight - 1, uint32_t(originY + float(y) / fit));
        uint32_t sy1 = std::clamp(uint32_t(originY + float(y + 1) / fit), sy0 + 1, sourceHeight);
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t sx0 = std::min(sourceWidth - 1, uint32_t(originX + float(x) / fit));
            uint32_t sx1 = std::clamp(uint32_t(originX + float(x + 1) / fit), sx0 + 1, sourceWidth);
            uint32_t sum[4] = {};
            uint32_t count = 0;
            for (uint32_t sy = sy0; sy < sy1; ++sy) {
                for (uint32_t sx = sx0; sx < sx1; ++sx) {
                    const uint8_t* texel = rgba.data() + (size_t(sy) * sourceWidth + sx) * 4;
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += texel[c];
                    }
                    ++count;
                }
            }
            uint8_t* out = result.rgba.data() + (size_t(y) * width + x) * 4;
            for (int c = 0; c < 4; ++c) {
                out[c] = static_cast<uint8_t>(sum[c] / count);
            }
        }
    }
    return result;
}

}

FeaturedServers::FeaturedServers(std::string language)
{
    worker = std::thread([this, language = std::move(language)]() mutable {
        run(std::move(language));
    });
}

FeaturedServers::~FeaturedServers()
{
    stopping = true;
    wake.notify_all();
    if (worker.joinable()) {
        worker.join();
    }
}

bool FeaturedServers::loading() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return listLoading;
}

std::vector<FeaturedServer> FeaturedServers::servers() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return list;
}

void FeaturedServers::requestImage(const std::string& url, bool showcase)
{
    if (url.empty()) {
        return;
    }
    std::lock_guard<std::mutex> guard(mutex);
    if (!requested.emplace(url, true).second) {
        return;
    }
    queue.push_back({ url, showcase });
    wake.notify_one();
}

std::map<std::string, ui::Bitmap> FeaturedServers::takeImages()
{
    std::lock_guard<std::mutex> guard(mutex);
    return std::exchange(images, {});
}

void FeaturedServers::run(std::string language)
{
    fetchList(language);
    {
        std::lock_guard<std::mutex> guard(mutex);
        listLoading = false;
    }
    while (!stopping) {
        ImageJob job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] {
                return stopping || !queue.empty();
            });
            if (stopping) {
                return;
            }
            job = std::move(queue.front());
            queue.pop_front();
        }
        downloadImage(job);
    }
}

void FeaturedServers::fetchList(const std::string& language)
{
    HttpResponse response;
    std::string error;
    if (!HttpClient::get(std::string(DiscoveryUrl) + GameVersion, {}, response, error) || response.mStatus != 200) {
        debugLog("featured servers: discovery failed, " + (error.empty() ? "status " + std::to_string(response.mStatus) : error));
        return;
    }
    std::unique_ptr<json::Value> discovery = json::parse(response.mBody);
    const json::Value* services = path(discovery.get(), { "result", "serviceEnvironments" });
    std::string authUri = text(path(services, { "auth", "prod", "serviceUri" }));
    std::string gatheringsUri = text(path(services, { "gatherings", "prod", "serviceUri" }));
    std::string titleId = text(path(services, { "auth", "prod", "playfabTitleId" }));
    if (authUri.empty() || gatheringsUri.empty() || titleId.empty() || stopping) {
        return;
    }

    std::string lowerTitle = titleId;
    std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::unique_ptr<json::Value> login = postJson("https://" + lowerTitle + ".playfabapi.com/Client/LoginWithIOSDeviceID", {},
        "{\"CreateAccount\":true,\"TitleId\":\"" + json::escape(titleId) + "\",\"DeviceId\":\"" + randomUuid() + "\",\"OS\":\"iOS\"}");
    std::string ticket = text(path(login.get(), { "data", "SessionTicket" }));
    if (ticket.empty() || stopping) {
        return;
    }

    std::unique_ptr<json::Value> session = postJson(authUri + "/api/v1.0/session/start", {},
        "{\"user\":{\"language\":\"en\",\"languageCode\":\"en-US\",\"regionCode\":\"US\",\"token\":\"" + json::escape(ticket) + "\",\"tokenType\":\"PlayFab\"},"
        "\"device\":{\"applicationType\":\"MinecraftPE\",\"memory\":\"8589934592\",\"id\":\"" + randomUuid() + "\",\"gameVersion\":\"" + GameVersion + "\","
        "\"platform\":\"Windows10\",\"playFabTitleId\":\"" + json::escape(titleId) + "\",\"storePlatform\":\"uwp.store\",\"type\":\"Windows10\"}}");
    std::string authorization = text(path(session.get(), { "result", "authorizationHeader" }));
    if (authorization.empty() || stopping) {
        return;
    }

    std::unique_ptr<json::Value> found = postJson(gatheringsUri + "/api/v2.0/discovery/blob/client", { { "Authorization", authorization } },
        "{\"count\":true,\"filter\":\"" + json::escape(ServerFilter) + "\",\"orderBy\":\"startDate desc\",\"scid\":\"" + ServerScid + "\",\"select\":\"images\",\"top\":75}");
    const json::Value* items = path(found.get(), { "data", "Items" });
    if (!items) {
        return;
    }

    std::vector<FeaturedServer> servers;
    for (const std::unique_ptr<json::Value>& item : items->mArray) {
        const json::Value* display = item->get("DisplayProperties");
        FeaturedServer server;
        server.id = text(item->get("Id"));
        server.name = localized(item->get("Title"), language);
        server.description = localized(item->get("Description"), language);
        server.creator = text(path(display, { "creatorName" }));
        server.newsTitle = text(path(display, { "newsTitle" }));
        server.news = text(path(display, { "news" }));
        std::string host = text(path(display, { "url" }));
        if (!host.empty()) {
            const json::Value* port = path(display, { "port" });
            server.address = host + ":" + std::to_string(port ? static_cast<int>(port->number(19132)) : 19132);
        }
        readImages(item->get("Images"), server);
        if (!server.id.empty() && !server.name.empty()) {
            servers.push_back(std::move(server));
        }
    }
    debugLog("featured servers: " + std::to_string(servers.size()) + " listed");
    std::lock_guard<std::mutex> guard(mutex);
    list = std::move(servers);
}

void FeaturedServers::downloadImage(const ImageJob& job)
{
    HttpResponse response;
    std::string error;
    if (!HttpClient::get(escapeUrl(job.url), {}, response, error, 30000) || response.mStatus != 200) {
        debugLog("featured servers: image " + job.url + " failed");
        return;
    }
    ui::Bitmap bitmap;
    if (job.showcase) {
        std::vector<uint8_t> rgba;
        uint32_t width = 0;
        uint32_t height = 0;
        if (!ui::decodeImage(response.mBody, width, height, rgba) || width == 0 || height == 0) {
            return;
        }
        bitmap = cover(rgba, width, height, ShowcaseWidth, ShowcaseHeight);
    } else {
        bitmap = { IconSize, IconSize, {} };
        if (!ui::decodeSquareImage(response.mBody, IconSize, bitmap.rgba)) {
            return;
        }
    }
    std::lock_guard<std::mutex> guard(mutex);
    images[job.url] = std::move(bitmap);
}

}
