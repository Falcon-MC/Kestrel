#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace kestrel {

inline constexpr const char* KestrelVersion = "1.0.1";

enum class UpdateStatus { Idle, Checking, Available, Downloading, Ready, Failed, Current };

struct UpdateView {
    UpdateStatus status = UpdateStatus::Idle;
    std::string version;
    std::string error;
    size_t downloaded = 0;
};

class Updates {
public:
    explicit Updates(std::string version = KestrelVersion);
    ~Updates();
    void check();
    void download();
    void cancel();
    UpdateView view() const;
    bool install();

private:
    bool get(std::string url, size_t limit, std::string& body);
    void fail(std::string error);
    mutable std::mutex mutex;
    UpdateView state;
    std::string currentVersion;
    std::string archiveUrl, checksumUrl, assetName;
    std::filesystem::path staged;
    std::atomic<bool> stopping { false };
    std::atomic<size_t> transferred { 0 };
    std::thread worker;
};

}
