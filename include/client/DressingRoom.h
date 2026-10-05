#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MinecraftAuthentication;

namespace kestrel {

/**
 * One persona piece or emote a dressing room page offers: its offer, title,
 * rarity, whether the player owns it, its creator and its thumbnail as RGBA.
 */
struct DressingItem {
    std::string id;
    std::string title;
    std::string rarity;
    std::string ownership;
    std::string creator;
    std::string pieceType;
    std::string thumbnailUrl;
    std::string packType;
    int coins = 0;
    int bonus = 0;
    std::string header;
    std::string coinText;
    std::string footer;
    std::vector<uint8_t> thumbnail;
    uint32_t thumbnailWidth = 0;
    uint32_t thumbnailHeight = 0;
};

/**
 * A dressing room page as the store lays it out: the pieces the player owns
 * and every other piece of the category.
 */
struct DressingPage {
    bool loading = false;
    bool loaded = false;
    std::string error;
    std::vector<DressingItem> owned;
    std::vector<DressingItem> others;
    int balance = -1;
    uint64_t revision = 0;
};

/**
 * The page id asking for the player's Minecoin balance instead of a layout.
 */
inline constexpr const char* DressingBalancePage = "__balance";

/**
 * Loads the store's dressing room pages in the background, one request at a
 * time, with the service token of the signed in account.
 */
class DressingRoomCatalog {
public:
    DressingRoomCatalog();
    ~DressingRoomCatalog();

    DressingRoomCatalog(const DressingRoomCatalog&) = delete;
    DressingRoomCatalog& operator=(const DressingRoomCatalog&) = delete;

    /**
     * Asks for a page once; asking again for a page already loaded or on its
     * way does nothing.
     */
    void request(const std::string& pageId, MinecraftAuthentication* authentication);
    /** Returns all pages when a revision changed, otherwise an empty map. */
    std::map<std::string, DressingPage> snapshot(const std::map<std::string, uint64_t>& seen) const;

private:
    void run();
    void load(const std::string& pageId, MinecraftAuthentication* authentication);

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::pair<std::string, MinecraftAuthentication*>> queue;
    std::map<std::string, DressingPage> pages;
    std::atomic<bool> stopping { false };
    uint64_t revision = 0;
    std::thread worker;
};

}
