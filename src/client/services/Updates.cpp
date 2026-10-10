#include "client/Updates.h"
#include "client/UpdatePackage.h"
#include "Core/Json/Json.h"
#include "Network/Http/HttpClient.h"
#include "platform/Paths.h"
#include "platform/Update.h"

#include <chrono>
#include <fstream>
#include <openssl/sha.h>

namespace kestrel {
namespace {

bool trustedUrl(const std::string& url)
{
    return url.starts_with("https://api.github.com/")
        || url.starts_with("https://github.com/Falcon-MC/Kestrel/releases/download/")
        || url.starts_with("https://release-assets.githubusercontent.com/")
        || url.starts_with("https://objects.githubusercontent.com/");
}

std::string text(const json::Value* root, const char* key)
{
    const auto* value = root->get(key);
    return value ? value->string() : std::string();
}

std::string digest(const std::string& data)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
    constexpr char Hex[] = "0123456789abcdef";
    std::string output;
    for (unsigned char byte : hash) {
        output += Hex[byte >> 4];
        output += Hex[byte & 15];
    }
    return output;
}

}

Updates::Updates(std::string version)
    : currentVersion(std::move(version))
{
}

Updates::~Updates()
{
    cancel();
    if (worker.joinable()) worker.join();
}

void Updates::cancel()
{
    stopping = true;
    std::lock_guard lock(mutex);
    if (state.status == UpdateStatus::Downloading || state.status == UpdateStatus::Ready) {
        state.status = UpdateStatus::Failed;
        state.error = "Update cancelled.";
    }
}

UpdateView Updates::view() const
{
    std::lock_guard lock(mutex);
    auto result = state;
    result.downloaded = transferred;
    return result;
}

void Updates::fail(std::string error)
{
    std::lock_guard lock(mutex);
    state.status = UpdateStatus::Failed;
    state.error = std::move(error);
}

bool Updates::get(std::string url, size_t limit, std::string& body)
{
    for (int redirects = 0; redirects < 6 && !stopping; ++redirects) {
        if (!trustedUrl(url)) return false;
        HttpResponse response;
        std::string error;
        if (!HttpClient::get(url, { { "Accept", "application/vnd.github+json" }, { "User-Agent", "Kestrel-Updater" } },
            response, error, 60000, limit + 65536, &stopping, [this](size_t bytes) { transferred = bytes; })) return false;
        if (response.mStatus >= 300 && response.mStatus < 400) {
            url = response.getHeader("Location");
            continue;
        }
        if (response.mStatus != 200 || response.mBody.size() > limit) return false;
        body = std::move(response.mBody);
        return true;
    }
    return false;
}

void Updates::check()
{
    if (worker.joinable()) return;
    if (view().status != UpdateStatus::Idle) return;
    {
        std::lock_guard lock(mutex);
        state.status = UpdateStatus::Checking;
    }
    worker = std::thread([this] {
        try {
            std::string body;
            if (!get("https://api.github.com/repos/Falcon-MC/Kestrel/releases/latest", 1024 * 1024, body)) {
                fail("Could not check for updates.");
                return;
            }
            auto document = json::parse(body);
            if (!document || !document->isObject()) { fail("Invalid release information."); return; }
            std::string tag = text(document.get(), "tag_name");
            if (!newerRelease(tag, currentVersion)) {
                std::lock_guard lock(mutex);
                state.status = UpdateStatus::Current;
                return;
            }
            std::string platform = platform::updatePlatform();
            if (platform.empty()) { fail("This platform has no automatic update package."); return; }
            assetName = "Kestrel-" + tag.substr(1) + "-" + platform;
            const auto* assets = document->get("assets");
            if (assets) for (const auto& asset : assets->mArray) {
                if (!asset->isObject()) continue;
                std::string name = text(asset.get(), "name"), url = text(asset.get(), "browser_download_url");
                if (!trustedUrl(url)) continue;
                if (name == assetName) archiveUrl = url;
                if (name == assetName + ".sha256") checksumUrl = url;
            }
            if (archiveUrl.empty() || checksumUrl.empty()) { fail("The release has no verified package for this platform."); return; }
            std::lock_guard lock(mutex);
            state.version = tag;
            state.status = UpdateStatus::Available;
        } catch (const std::exception&) { fail("Could not check for updates."); }
    });
}

void Updates::download()
{
    auto current = view();
    if (current.status != UpdateStatus::Available && current.status != UpdateStatus::Failed) return;
    if (archiveUrl.empty() || checksumUrl.empty()) return;
    if (worker.joinable()) worker.join();
    stopping = false;
    transferred = 0;
    {
        std::lock_guard lock(mutex);
        state.status = UpdateStatus::Downloading;
        state.error.clear();
    }
    worker = std::thread([this] {
        try {
            std::string checksum, archive;
            if (!get(checksumUrl, 4096, checksum) || !get(archiveUrl, 128 * 1024 * 1024, archive)) {
                fail(stopping ? "Update cancelled." : "Update download failed."); return;
            }
            if (checksum.size() < 65 || checksum[64] != ' ' || checksum.substr(0, 64) != digest(archive)) {
                fail("Update checksum verification failed."); return;
            }
            bool zip = assetName.ends_with(".zip");
            std::string root = assetName.substr(0, assetName.size() - (zip ? 4 : 7));
            std::string binaryName = zip ? "Kestrel.exe" : "Kestrel";
            std::string binary = updateBinary(archive, root + "/" + binaryName, zip);
            if (binary.empty()) { fail("The update archive contains no valid executable."); return; }
            if (stopping) { fail("Update cancelled."); return; }
            auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
            auto directory = platform::dataDirectory() / "updates" / std::to_string(nonce);
            std::filesystem::create_directories(directory);
            staged = directory / binaryName;
            std::ofstream file(staged, std::ios::binary | std::ios::trunc);
            file.write(binary.data(), binary.size());
            file.close();
            if (!file) { fail("Could not save the downloaded update."); return; }
            std::filesystem::permissions(staged, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec);
            std::lock_guard lock(mutex);
            if (stopping) {
                state.status = UpdateStatus::Failed;
                state.error = "Update cancelled.";
            } else {
                state.status = UpdateStatus::Ready;
            }
        } catch (const std::exception&) { fail("Could not prepare the update."); }
    });
}

bool Updates::install()
{
    if (stopping || view().status != UpdateStatus::Ready) return false;
    if (worker.joinable()) worker.join();
    std::string error;
    if (platform::launchUpdate(staged, error)) return true;
    fail(std::move(error));
    return false;
}

}
