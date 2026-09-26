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

    std::filesystem::path cacheFile;
    std::unique_ptr<MinecraftAuthentication> authentication;
    std::thread worker;
    std::atomic<bool> cancelled { false };
    mutable std::mutex mutex;
    AccountSnapshot current;
    uint64_t avatarRevision = 0;
};

}
