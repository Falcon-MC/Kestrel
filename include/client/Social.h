#pragma once

#include "client/SocialModel.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

class MinecraftAuthentication;

namespace kestrel {

struct SocialAvatar {
    std::string xuid;
    std::vector<uint8_t> pixels;
};

/**
 * The player's friends, friend requests, player search and Realm invitations,
 * fetched on one worker thread so the interface never waits on the network.
 * Each change of account starts a new generation: its queued work is dropped,
 * its requests are cancelled and whatever they still return is ignored.
 */
class Social {
public:
    Social();
    ~Social();

    Social(const Social&) = delete;
    Social& operator=(const Social&) = delete;

    /**
     * Follows the signed in account; an empty XUID means signed out. Signing
     * out waits for the request in flight to give up, so one still refreshing
     * its token cannot write the removed sign in back to disk.
     */
    void setAccount(std::shared_ptr<MinecraftAuthentication> authentication, const std::string& xuid);

    void setLanguage(std::string language);

    void handle(const SocialRequest& request);

    /**
     * Refreshes the friends list when it is older than maxAge, for screens
     * that show it and poll now and then.
     */
    void refreshFriendsIfStale(std::chrono::seconds maxAge);

    uint64_t revision() const;
    SocialSnapshot snapshot() const;

    /**
     * Profile pictures decoded since the last call, ready for the skin.
     */
    std::vector<SocialAvatar> takeAvatars();

private:
    using Job = std::function<void()>;

    /**
     * What a job was queued for: the account generation, its sign in and the
     * flag that cancels its requests when the account changes.
     */
    struct Ticket {
        uint64_t generation = 0;
        std::shared_ptr<MinecraftAuthentication> authentication;
        std::shared_ptr<std::atomic<bool>> cancel;
    };

    Ticket ticket() const;
    void post(Job job, bool background = false);
    void run();
    void publish();
    void setOperation(const std::string& key, bool pending, OnlineError error);
    bool current(const Ticket& ticket) const;
    void queueRefresh(bool friends, bool requests);

    void refreshFriends(const Ticket& ticket);
    void refreshRequests(const Ticket& ticket);
    void search(const Ticket& ticket, const std::string& query);
    void mutateFriend(const Ticket& ticket, SocialAction action, const std::string& xuid);
    void refreshInvites(const Ticket& ticket);
    void answerInvite(const Ticket& ticket, const std::string& id, bool accept);
    void lookUpRealmCode(const Ticket& ticket, uint64_t request, const std::string& code);
    void acceptRealmCode(const Ticket& ticket, uint64_t request, const std::string& code);
    void fetchAvatar(const Ticket& ticket, const std::string& xuid, const std::string& url);
    void queueAvatars(const Ticket& ticket, const std::vector<std::pair<std::string, std::string>>& pictures);

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable idle;
    std::deque<Job> jobs;
    std::deque<Job> backgroundJobs;
    bool busy = false;
    bool stopping = false;
    std::thread worker;

    std::shared_ptr<MinecraftAuthentication> authentication;
    std::shared_ptr<std::atomic<bool>> cancel;
    uint64_t generation = 0;
    std::string language = "en-US";
    std::atomic<uint64_t> changes { 0 };
    SocialSnapshot state;
    std::chrono::steady_clock::time_point friendsLoadedAt {};
    std::chrono::steady_clock::time_point friendsRetryAt {};
    std::chrono::steady_clock::time_point requestsRetryAt {};
    std::chrono::steady_clock::time_point searchRetryAt {};
    std::chrono::steady_clock::time_point invitesRetryAt {};
    std::string realmCode;
    uint64_t realmCodeRequest = 0;
    std::set<std::string> avatarsRequested;
    std::vector<SocialAvatar> avatars;
};

}
