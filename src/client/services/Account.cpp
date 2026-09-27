#include "client/Account.h"

#include "Network/Auth/MinecraftAuthentication.h"
#include "Network/Auth/XboxLiveAuthentication.h"
#include "Network/Auth/XboxLiveConfig.h"
#include "Network/Session/RealmsService.h"
#include "Network/Http/HttpClient.h"
#include "Core/Json/Json.h"
#include "ui/Font.h"
#include "ui/Image.h"

#include <fstream>
#include <iterator>

namespace kestrel {

namespace {

constexpr const char* GameVersion = "1.26.51";
constexpr const char* XboxLiveRelyingParty = "http://xboxlive.com";
constexpr const char* ProfileSettingsUrl = "https://profile.xboxlive.com/users/me/profile/settings?settings=GameDisplayPicRaw";

}

Account::Account(std::filesystem::path cacheFile)
    : cacheFile(std::move(cacheFile))
{
}

Account::~Account()
{
    stop();
}

void Account::restore()
{
    std::error_code error;
    if (!std::filesystem::exists(cacheFile, error)) {
        return;
    }
    start(false);

    std::ifstream profile(profileFile());
    std::string gamertag;
    std::string xuid;
    std::getline(profile, gamertag);
    std::getline(profile, xuid);

    std::ifstream avatarStream(avatarFile(), std::ios::binary);
    std::vector<uint8_t> avatar((std::istreambuf_iterator<char>(avatarStream)), std::istreambuf_iterator<char>());

    std::lock_guard<std::mutex> guard(mutex);
    if (current.state != AccountState::Connecting) {
        return;
    }
    current.gamertag = gamertag;
    current.xuid = xuid;
    if (avatar.size() == size_t(ui::Font::ImageSlotSize) * ui::Font::ImageSlotSize * 4) {
        current.avatar = std::move(avatar);
        current.avatarRevision = ++avatarRevision;
    }
}

std::filesystem::path Account::profileFile() const
{
    return cacheFile.parent_path() / "profile.txt";
}

std::filesystem::path Account::avatarFile() const
{
    return cacheFile.parent_path() / "avatar.rgba";
}

void Account::signIn()
{
    start(true);
}

void Account::cancel()
{
    stop();
    std::lock_guard<std::mutex> guard(mutex);
    if (current.state != AccountState::SignedIn) {
        current = AccountSnapshot {};
    }
}

void Account::signOut()
{
    stop();
    std::error_code error;
    std::filesystem::remove(cacheFile, error);
    std::filesystem::remove(profileFile(), error);
    std::filesystem::remove(avatarFile(), error);
    authentication.reset();
    std::lock_guard<std::mutex> guard(mutex);
    current = AccountSnapshot {};
}

AccountSnapshot Account::snapshot() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return current;
}

MinecraftAuthentication* Account::signedInAuthentication() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return current.state == AccountState::SignedIn ? authentication.get() : nullptr;
}

void Account::start(bool interactive)
{
    stop();
    cancelled = false;
    {
        std::lock_guard<std::mutex> guard(mutex);
        current = AccountSnapshot {};
        current.state = AccountState::Connecting;
    }

    authentication = std::make_unique<MinecraftAuthentication>(XboxLiveConfig::android(), cacheFile.string(), GameVersion);
    LiveAuthentication& live = authentication->getLiveAuthentication();
    live.setCancelFlag(&cancelled);
    live.setDeviceCodeCallback([this, interactive](const std::string& uri, const std::string& code) {
        if (!interactive) {
            cancelled = true;
            return;
        }
        std::lock_guard<std::mutex> guard(mutex);
        current.state = AccountState::AwaitingCode;
        current.verificationUri = uri;
        current.userCode = code;
    });

    worker = std::thread([this, interactive] {
        run(interactive);
    });
}

void Account::stop()
{
    cancelled = true;
    if (worker.joinable()) {
        worker.join();
    }
}

void Account::run(bool interactive)
{
    XboxLiveToken token;
    std::string error;
    bool signedIn = authentication->getXboxLiveAuthentication().requestToken(XboxLiveAuthentication::MULTIPLAYER_RELYING_PARTY, token, error);

    XboxLiveToken profile;
    std::string profileError;
    if (signedIn && !cancelled) {
        authentication->getXboxLiveAuthentication().requestToken(XboxLiveRelyingParty, profile, profileError);
    }

    {
        std::lock_guard<std::mutex> guard(mutex);
        if (cancelled) {
            current = AccountSnapshot {};
            return;
        }
        if (!signedIn) {
            current = AccountSnapshot {};
            if (interactive) {
                current.state = AccountState::Failed;
                current.error = error;
            }
            return;
        }
        current.state = AccountState::SignedIn;
        if (!profile.mGamerTag.empty() || !token.mGamerTag.empty()) {
            current.gamertag = !profile.mGamerTag.empty() ? profile.mGamerTag : token.mGamerTag;
        }
        if (!profile.mXuid.empty() || !token.mXuid.empty()) {
            current.xuid = !profile.mXuid.empty() ? profile.mXuid : token.mXuid;
        }
        std::ofstream profileOut(profileFile(), std::ios::trunc);
        profileOut << current.gamertag << '\n' << current.xuid << '\n';
        current.userCode.clear();
        current.verificationUri.clear();
        current.realmsLoading = true;
    }
    if (!profile.mToken.empty()) {
        fetchAvatar(profile.getAuthorizationHeader());
    }
    fetchRealms();
}

void Account::fetchAvatar(const std::string& authorization)
{
    HttpClient::Headers headers;
    headers.emplace_back("Authorization", authorization);
    headers.emplace_back("x-xbl-contract-version", "2");
    headers.emplace_back("Accept", "application/json");

    HttpResponse response;
    std::string error;
    if (!HttpClient::get(ProfileSettingsUrl, headers, response, error) || response.mStatus != 200) {
        return;
    }

    std::unique_ptr<json::Value> root = json::parse(response.mBody);
    const json::Value* users = root ? root->get("profileUsers") : nullptr;
    if (!users || !users->isArray() || users->mArray.empty()) {
        return;
    }
    const json::Value* settings = users->mArray.front()->get("settings");
    if (!settings || !settings->isArray()) {
        return;
    }

    std::string url;
    for (const std::unique_ptr<json::Value>& setting : settings->mArray) {
        const json::Value* id = setting->get("id");
        const json::Value* value = setting->get("value");
        if (id && value && id->string() == "GameDisplayPicRaw") {
            url = value->string();
        }
    }
    if (url.empty()) {
        return;
    }
    if (url.rfind("http://", 0) == 0) {
        url.replace(0, 7, "https://");
    }
    const std::string plainHost = "https://images-eds.xboxlive.com";
    if (url.rfind(plainHost, 0) == 0) {
        url.replace(0, plainHost.size(), "https://images-eds-ssl.xboxlive.com");
    }
    url += url.find('?') == std::string::npos ? "?w=128&h=128" : "&w=128&h=128";

    HttpResponse image;
    if (!HttpClient::get(url, {}, image, error)) {
        return;
    }
    if (image.mStatus >= 300 && image.mStatus < 400 && !image.getHeader("Location").empty()) {
        std::string location = image.getHeader("Location");
        if (!HttpClient::get(location, {}, image, error)) {
            return;
        }
    }
    std::vector<uint8_t> pixels;
    if (image.mStatus != 200 || !ui::decodeSquareImage(image.mBody, ui::Font::ImageSlotSize, pixels)) {
        return;
    }

    std::ofstream avatarOut(avatarFile(), std::ios::binary | std::ios::trunc);
    avatarOut.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));

    std::lock_guard<std::mutex> guard(mutex);
    if (cancelled || current.state != AccountState::SignedIn) {
        return;
    }
    current.avatar = std::move(pixels);
    current.avatarRevision = ++avatarRevision;
}

void Account::fetchRealms()
{
    std::vector<RealmDescription> descriptions;
    std::string error;
    RealmsService service(*authentication);
    bool loaded = service.requestRealms(descriptions, error);

    std::vector<Realm> realms;
    for (const RealmDescription& description : descriptions) {
        Realm realm;
        realm.id = description.mId;
        realm.name = description.mName;
        realm.owner = description.mOwner;
        realm.open = description.mState == "OPEN";
        realm.expired = description.mExpired;
        realms.push_back(std::move(realm));
    }

    std::lock_guard<std::mutex> guard(mutex);
    if (cancelled || current.state != AccountState::SignedIn) {
        return;
    }
    current.realms = std::move(realms);
    current.realmsError = loaded ? std::string() : "Realms: " + error;
    current.realmsLoading = false;
}

}
