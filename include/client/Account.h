#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MinecraftAuthentication;

namespace kestrel {

enum class AccountState {
    SignedOut,
    Connecting,
    AwaitingCode,
    SignedIn,
    Failed,
};

struct Realm {
    int64_t id = 0;
    std::string name;
    std::string owner;
    bool open = false;
    bool expired = false;
};

/**
 * One Xbox achievement of the game as the profile page shows it; the icon is
 * RGBA, greyed out while the achievement is still locked.
 */
struct Achievement {
    std::string name;
    std::string description;
    int gamerscore = 0;
    bool achieved = false;
    std::vector<uint8_t> icon;
    uint32_t iconWidth = 0;
    uint32_t iconHeight = 0;
};

/**
 * The player's progress in the game: achievement and gamerscore totals, a few
 * suggested and recently earned achievements, and the play statistics.
 */
struct PlayerProfile {
    bool achievementsLoaded = false;
    int achieved = 0;
    int total = 0;
    int gamerscore = 0;
    int totalGamerscore = 0;
    std::vector<Achievement> suggested;
    std::vector<Achievement> recent;
    bool statsLoaded = false;
    int64_t minutesPlayed = 0;
    int64_t blocksBroken = 0;
    int64_t mobsDefeated = 0;
    int64_t distanceTravelled = 0;
    uint64_t revision = 0;
};

struct AccountSnapshot {
    AccountState state = AccountState::SignedOut;
    std::string verificationUri;
    std::string userCode;
    std::string gamertag;
    std::string xuid;
    std::string error;
    std::vector<Realm> realms;
    bool realmsLoading = false;
    std::string realmsError;
    std::vector<uint8_t> avatar;
    uint64_t avatarRevision = 0;
    PlayerProfile profile;
};

class Account {
public:
    explicit Account(std::filesystem::path cacheFile);
    ~Account();

    Account(const Account&) = delete;
    Account& operator=(const Account&) = delete;

    void restore();
    void signIn();
    void cancel();
    void signOut();

    AccountSnapshot snapshot() const;
    MinecraftAuthentication* signedInAuthentication() const;

private:
    void start(bool interactive);
    void stop();
    void run(bool interactive);
    void fetchRealms();
    std::filesystem::path profileFile() const;
    std::filesystem::path avatarFile() const;
    void fetchAvatar(const std::string& authorization);
    void fetchProfile(const std::string& titleAuthorization, const std::string& xuid);
    std::string userAuthorization();

    std::filesystem::path cacheFile;
    std::unique_ptr<MinecraftAuthentication> authentication;
    std::thread worker;
    std::thread warmer;
    std::atomic<bool> cancelled { false };
    mutable std::mutex mutex;
    AccountSnapshot current;
    uint64_t avatarRevision = 0;
    uint64_t profileRevision = 0;
};

}
