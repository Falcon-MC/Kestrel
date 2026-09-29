#include "client/DressingRoom.h"

#include "Core/Json/Json.h"
#include "Network/Auth/MinecraftAuthentication.h"
#include "Network/Http/HttpClient.h"
#include "client/DebugLog.h"
#include "ui/Image.h"

#include <algorithm>

namespace kestrel {

namespace {

constexpr const char* PageUrl = "https://store.mktpl.minecraft-services.net/api/v2.0/layout/pages/";
constexpr const char* BalanceUrl = "https://entitlements.mktpl.minecraft-services.net/api/v1.0/currencies/virtual/balances";
constexpr uint32_t ThumbnailSize = 96;

std::string field(const json::Value& object, const char* key)
{
    const json::Value* value = object.get(key);
    return value && value->isString() ? value->mString : std::string();
}

/**
 * Shrinks a thumbnail to a square the grid tiles draw, keeping the middle
 * of a wide picture.
 */
void shrink(DressingItem& item, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba)
{
    uint32_t side = std::min(width, height);
    uint32_t left = (width - side) / 2;
    uint32_t top = (height - side) / 2;
    item.thumbnail.assign(size_t(ThumbnailSize) * ThumbnailSize * 4, 0);
    for (uint32_t y = 0; y < ThumbnailSize; ++y) {
        for (uint32_t x = 0; x < ThumbnailSize; ++x) {
            uint32_t sx = left + x * side / ThumbnailSize;
            uint32_t sy = top + y * side / ThumbnailSize;
            std::copy_n(rgba.data() + (size_t(sy) * width + sx) * 4, 4, item.thumbnail.data() + (size_t(y) * ThumbnailSize + x) * 4);
        }
    }
    item.thumbnailWidth = ThumbnailSize;
    item.thumbnailHeight = ThumbnailSize;
}

void collectItems(const json::Value& value, std::vector<DressingItem>& out)
{
    if (value.isObject()) {
        const json::Value* type = value.get("$type");
        const json::Value* items = value.get("items");
        if (type && type->isString() && type->mString == "ItemListComponent" && items && items->isArray()) {
            for (const std::unique_ptr<json::Value>& entry : items->mArray) {
                DressingItem item;
                item.id = field(*entry, "id");
                item.title = field(*entry, "title");
                item.rarity = field(*entry, "rarity");
                item.ownership = field(*entry, "ownership");
                item.creator = field(*entry, "creatorName");
                item.pieceType = field(*entry, "pieceType");
                item.packType = field(*entry, "packType");
                const json::Value* coins = entry->get("coinCount");
                const json::Value* bonus = entry->get("bonusCoinCount");
                item.coins = coins && coins->isNumber() ? static_cast<int>(coins->number()) : 0;
                item.bonus = bonus && bonus->isNumber() ? static_cast<int>(bonus->number()) : 0;
                auto text = [&](const char* key) {
                    const json::Value* holder = entry->get(key);
                    return holder && holder->isObject() ? field(*holder, "value") : std::string();
                };
                item.header = text("descriptionHeader");
                item.coinText = text("descriptionCoin");
                item.footer = text("descriptionFooter");
                if (const json::Value* thumbnail = entry->get("thumbnail")) {
                    item.thumbnailUrl = field(*thumbnail, "url");
                }
                if (!item.id.empty() && std::none_of(out.begin(), out.end(), [&](const DressingItem& seen) { return seen.id == item.id; })) {
                    out.push_back(std::move(item));
                }
            }
            return;
        }
        for (const std::string& key : value.mKeys) {
            collectItems(*value.get(key), out);
        }
    } else if (value.isArray()) {
        for (const std::unique_ptr<json::Value>& element : value.mArray) {
            collectItems(*element, out);
        }
    }
}

}

DressingRoomCatalog::DressingRoomCatalog()
    : worker([this] { run(); })
{
}

DressingRoomCatalog::~DressingRoomCatalog()
{
    stopping = true;
    wake.notify_all();
    if (worker.joinable()) {
        worker.join();
    }
}

void DressingRoomCatalog::request(const std::string& pageId, MinecraftAuthentication* authentication)
{
    if (!authentication) {
        return;
    }
    std::lock_guard<std::mutex> guard(mutex);
    DressingPage& page = pages[pageId];
    if (page.loading || (page.loaded && page.error.empty())) {
        return;
    }
    page.loading = true;
    page.error.clear();
    page.revision = ++revision;
    queue.emplace_back(pageId, authentication);
    wake.notify_one();
}

std::map<std::string, DressingPage> DressingRoomCatalog::snapshot() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return pages;
}

void DressingRoomCatalog::run()
{
    while (!stopping) {
        std::pair<std::string, MinecraftAuthentication*> next;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return stopping || !queue.empty(); });
            if (stopping) {
                return;
            }
            next = std::move(queue.front());
            queue.pop_front();
        }
        load(next.first, next.second);
    }
}

/**
 * One page: the layout from the store, split between owned pieces and the
 * rest, then every thumbnail fetched and shrunk.
 */
void DressingRoomCatalog::load(const std::string& pageId, MinecraftAuthentication* authentication)
{
    auto fail = [&](const std::string& error) {
        debugLog("dressing room page " + pageId + " failed: " + error);
        std::lock_guard<std::mutex> guard(mutex);
        DressingPage& page = pages[pageId];
        page.loading = false;
        page.loaded = true;
        page.error = error;
        page.revision = ++revision;
    };
    std::string authorization;
    std::string error;
    if (!authentication->requestServiceToken(authorization, error)) {
        fail(error);
        return;
    }
    HttpClient::Headers headers;
    headers.emplace_back("Authorization", authorization);
    headers.emplace_back("Content-Type", "application/json");
    headers.emplace_back("Accept", "application/json");
    HttpResponse response;
    if (pageId == DressingBalancePage) {
        if (!HttpClient::post(BalanceUrl, headers, "{}", response, error) || response.mStatus != 200) {
            fail(error.empty() ? "status " + std::to_string(response.mStatus) : error);
            return;
        }
        std::unique_ptr<json::Value> document = json::parse(response.mBody);
        const json::Value* result = document ? document->get("result") : nullptr;
        const json::Value* balances = result ? result->get("virtualCurrencyBalances") : nullptr;
        int amount = 0;
        if (balances && balances->isArray()) {
            for (const std::unique_ptr<json::Value>& entry : balances->mArray) {
                const json::Value* value = entry->get("amount");
                if (field(*entry, "type") == "Minecoin" && value && value->isNumber()) {
                    amount = static_cast<int>(value->number());
                }
            }
        }
        std::lock_guard<std::mutex> guard(mutex);
        DressingPage& page = pages[pageId];
        page.loading = false;
        page.loaded = true;
        page.balance = amount;
        page.revision = ++revision;
        return;
    }
    std::string path = pageId.find('_') == std::string::npos ? "DressingRoom_" + pageId : pageId;
    if (!HttpClient::post(PageUrl + path, headers, "{}", response, error) || response.mStatus != 200) {
        fail(error.empty() ? "status " + std::to_string(response.mStatus) + " " + response.mBody.substr(0, 300) : error);
        return;
    }
    std::unique_ptr<json::Value> document = json::parse(response.mBody);
    if (!document) {
        fail("unreadable page");
        return;
    }
    std::vector<DressingItem> items;
    collectItems(*document, items);

    DressingPage page;
    for (DressingItem& item : items) {
        if (stopping) {
            return;
        }
        HttpResponse image;
        std::string imageError;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
        if (!item.thumbnailUrl.empty() && HttpClient::get(item.thumbnailUrl, {}, image, imageError) && image.mStatus == 200
            && ui::decodeImage(image.mBody, width, height, rgba) && width > 0 && height > 0) {
            shrink(item, width, height, rgba);
        }
        (item.ownership == "NotOwned" ? page.others : page.owned).push_back(std::move(item));
    }
    std::lock_guard<std::mutex> guard(mutex);
    page.loaded = true;
    page.revision = ++revision;
    pages[pageId] = std::move(page);
}

}
