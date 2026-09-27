#pragma once

#include "ui/GameAssets.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kestrel {

/**
 * One partner server as the game's discovery service lists it. Featured ones
 * have an address to dial; creator experiences have none and are joined
 * through their experience id instead.
 */
struct FeaturedServer {
    std::string id;
    std::string name;
    std::string creator;
    std::string description;
    std::string newsTitle;
    std::string news;
    std::string address;
    std::string iconUrl;
    std::vector<std::string> showcaseUrls;

    bool creatorExperience() const
    {
        return address.empty();
    }
};

/**
 * Fetches the partner servers the play screen shows, and their icons and
 * showcase screenshots, on a background thread. Needs no Microsoft account,
 * the listing is readable with an anonymous PlayFab session.
 */
class FeaturedServers {
public:
    static constexpr uint32_t IconSize = 64;
    static constexpr uint32_t ShowcaseWidth = 384;
    static constexpr uint32_t ShowcaseHeight = 216;

    explicit FeaturedServers(std::string language);
    ~FeaturedServers();

    FeaturedServers(const FeaturedServers&) = delete;
    FeaturedServers& operator=(const FeaturedServers&) = delete;

    bool loading() const;
    std::vector<FeaturedServer> servers() const;

    /**
     * Queues an image download once; the decoded bitmap shows up in
     * takeImages() keyed by its url.
     */
    void requestImage(const std::string& url, bool showcase);
    std::map<std::string, ui::Bitmap> takeImages();

private:
    struct ImageJob {
        std::string url;
        bool showcase = false;
    };

    void run(std::string language);
    void fetchList(const std::string& language);
    void downloadImage(const ImageJob& job);

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<ImageJob> queue;
    std::map<std::string, bool> requested;
    std::map<std::string, ui::Bitmap> images;
    std::vector<FeaturedServer> list;
    bool listLoading = true;
    std::atomic<bool> stopping { false };
    std::thread worker;
};

}
