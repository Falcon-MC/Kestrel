#include "client/Account.h"

#include "Network/Auth/MinecraftAuthentication.h"
#include "Network/Auth/XboxLiveAuthentication.h"
#include "Network/Auth/XboxLiveConfig.h"
#include "Network/Session/RealmsService.h"
#include "Network/Session/XboxSocialService.h"
#include "Network/Http/HttpClient.h"
#include "Core/Json/Json.h"
#include "client/DebugLog.h"
#include "ui/Font.h"
#include "ui/Image.h"
#include "ui/Localization.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>

namespace kestrel {

namespace {

constexpr const char* GameVersion = "1.26.51";
constexpr const char* XboxLiveRelyingParty = "http://xboxlive.com";
constexpr const char* GameTitleId = "896928775";
constexpr uint32_t IconWidth = 176;
constexpr const char* GameServiceConfigId = "4fc10100-5f7a-4470-899b-280835760c07";
constexpr const char* ProfileSettingsUrl ="https://profile.xboxlive.com/users/me/profile/settings?settings=GameDisplayPicRaw";

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

std::shared_ptr<MinecraftAuthentication> Account::sharedAuthentication() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return current.state == AccountState::SignedIn ? authentication : nullptr;
}

void Account::refreshRealms()
{
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (current.state != AccountState::SignedIn || realmsRunning) {
            return;
        }
        current.realmsLoading = true;
        realmsRunning = true;
    }
    if (realmsWorker.joinable()) {
        realmsWorker.join();
    }
    realmsWorker = std::thread([this] {
        fetchRealms();
        realmsRunning = false;
    });
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

    authentication = std::make_shared<MinecraftAuthentication>(XboxLiveConfig::android(), cacheFile.string(), GameVersion);
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
    if (warmer.joinable()) {
        warmer.join();
    }
    if (realmsWorker.joinable()) {
        realmsWorker.join();
    }
    realmsRunning = false;
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
    // Every join needs the Minecraft service token, and getting one takes a
    // handful of round trips, so fetch it now instead of on the join screen.
    warmer = std::thread([this] {
        std::string authorization;
        std::string failure;
        if (!authentication->requestServiceToken(authorization, failure)) {
            debugLog("service token warm up failed: " + failure);
        }
    });
    if (!profile.mToken.empty()) {
        fetchAvatar(profile.getAuthorizationHeader());
    }
    fetchRealms();
    if (!profile.mToken.empty() && !cancelled) {
        fetchProfile(profile.getAuthorizationHeader(), !profile.mXuid.empty() ? profile.mXuid : token.mXuid);
    }
}

/**
 * An Xbox Live authorization for the user alone, without the device and
 * title the sign-in token carries: the achievements service scopes a titled
 * token to its own title, which hides the Windows edition's progress.
 */
std::string Account::userAuthorization()
{
    LiveToken live;
    std::string error;
    if (!authentication->getLiveAuthentication().getToken(live, error)) {
        return {};
    }
    auto post = [](const std::string& url, const std::string& body) -> std::unique_ptr<json::Value> {
        HttpClient::Headers headers;
        headers.emplace_back("Content-Type", "application/json");
        headers.emplace_back("Accept", "application/json");
        headers.emplace_back("x-xbl-contract-version", "1");
        HttpResponse response;
        std::string failure;
        if (!HttpClient::post(url, headers, body, response, failure) || response.mStatus != 200) {
            return nullptr;
        }
        return json::parse(response.mBody);
    };
    std::unique_ptr<json::Value> user = post("https://user.auth.xboxlive.com/user/authenticate",
        "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\",\"RpsTicket\":\"t=" + live.mAccessToken + "\"},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}");
    const json::Value* userToken = user ? user->get("Token") : nullptr;
    if (!userToken) {
        return {};
    }
    std::unique_ptr<json::Value> xsts = post("https://xsts.auth.xboxlive.com/xsts/authorize",
        "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"" + userToken->string() + "\"]},\"RelyingParty\":\"http://xboxlive.com\",\"TokenType\":\"JWT\"}");
    const json::Value* token = xsts ? xsts->get("Token") : nullptr;
    const json::Value* claims = xsts ? xsts->get("DisplayClaims") : nullptr;
    const json::Value* users = claims ? claims->get("xui") : nullptr;
    if (!token || !users || !users->isArray() || users->mArray.empty()) {
        return {};
    }
    const json::Value* hash = users->mArray.front()->get("uhs");
    return hash ? "XBL3.0 x=" + hash->string() + ";" + token->string() : std::string();
}

/**
 * Loads the player's achievements for the game in the interface language, the
 * icons of the few the profile page lists, and the play statistics.
 */
void Account::fetchProfile(const std::string& titleAuthorization, const std::string& xuid)
{
    if (xuid.empty()) {
        return;
    }
    std::string authorization = userAuthorization();
    if (authorization.empty()) {
        authorization = titleAuthorization;
    }
    std::string language = ui::Localization::shared().code();
    std::replace(language.begin(), language.end(), '_', '-');
    auto request = [&](const std::string& url, const char* contract, HttpResponse& response) {
        HttpClient::Headers headers;
        headers.emplace_back("Authorization", authorization);
        headers.emplace_back("x-xbl-contract-version", contract);
        headers.emplace_back("Accept", "application/json");
        headers.emplace_back("Accept-Language", language);
        std::string error;
        return HttpClient::get(url, headers, response, error) && response.mStatus == 200;
    };

    PlayerProfile loaded;
    struct Entry {
        Achievement achievement;
        std::string unlocked;
        std::string iconUrl;
    };
    std::vector<Entry> entries;
    HttpResponse response;
    if (request("https://achievements.xboxlive.com/users/xuid(" + xuid + ")/achievements?titleId=" + GameTitleId + "&maxItems=1000", "2", response)) {
        std::unique_ptr<json::Value> root = json::parse(response.mBody);
        const json::Value* list = root ? root->get("achievements") : nullptr;
        if (list && list->isArray()) {
            for (const std::unique_ptr<json::Value>& item : list->mArray) {
                Entry entry;
                const json::Value* name = item->get("name");
                const json::Value* description = item->get("description");
                const json::Value* locked = item->get("lockedDescription");
                const json::Value* state = item->get("progressState");
                entry.achievement.name = name ? name->string() : std::string();
                entry.achievement.achieved = state && state->string() == "Achieved";
                entry.achievement.description = description ? description->string() : std::string();
                if (!entry.achievement.achieved && locked && !locked->string().empty()) {
                    entry.achievement.description = locked->string();
                }
                if (const json::Value* rewards = item->get("rewards"); rewards && rewards->isArray()) {
                    for (const std::unique_ptr<json::Value>& reward : rewards->mArray) {
                        const json::Value* type = reward->get("type");
                        const json::Value* value = reward->get("value");
                        if (type && value && type->string() == "Gamerscore") {
                            entry.achievement.gamerscore = std::atoi(value->string().c_str());
                        }
                    }
                }
                if (const json::Value* progression = item->get("progression")) {
                    if (const json::Value* time = progression->get("timeUnlocked")) {
                        entry.unlocked = time->string();
                    }
                }
                if (const json::Value* media = item->get("mediaAssets"); media && media->isArray() && !media->mArray.empty()) {
                    if (const json::Value* url = media->mArray.front()->get("url")) {
                        entry.iconUrl = url->string();
                    }
                }
                ++loaded.total;
                loaded.totalGamerscore += entry.achievement.gamerscore;
                if (entry.achievement.achieved) {
                    ++loaded.achieved;
                    loaded.gamerscore += entry.achievement.gamerscore;
                }
                entries.push_back(std::move(entry));
            }
            loaded.achievementsLoaded = true;
        }
        debugLog("profile: " + std::to_string(entries.size()) + " achievements");
    } else {
        debugLog("profile: achievements request failed, status " + std::to_string(response.mStatus));
    }

    std::vector<const Entry*> earned;
    std::vector<const Entry*> open;
    for (const Entry& entry : entries) {
        (entry.achievement.achieved ? earned : open).push_back(&entry);
    }
    std::sort(earned.begin(), earned.end(), [](const Entry* a, const Entry* b) {
        return a->unlocked > b->unlocked;
    });
    std::shuffle(open.begin(), open.end(), std::mt19937(std::random_device {}()));
    auto withIcon = [&](const Entry& entry) {
        Achievement achievement = entry.achievement;
        HttpResponse image;
        std::string error;
        if (!entry.iconUrl.empty() && HttpClient::get(entry.iconUrl, {}, image, error) && image.mStatus == 200) {
            uint32_t width = 0;
            uint32_t height = 0;
            std::vector<uint8_t> rgba;
            if (ui::decodeImage(image.mBody, width, height, rgba) && width > 0 && height > 0) {
                if (!achievement.achieved) {
                    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
                        uint8_t grey = static_cast<uint8_t>((rgba[i] * 30 + rgba[i + 1] * 59 + rgba[i + 2] * 11) / 100);
                        rgba[i] = grey;
                        rgba[i + 1] = grey;
                        rgba[i + 2] = grey;
                    }
                }
                achievement.iconWidth = std::min(width, IconWidth);
                achievement.iconHeight = std::max<uint32_t>(1, height * achievement.iconWidth / width);
                achievement.icon.assign(size_t(achievement.iconWidth) * achievement.iconHeight * 4, 0);
                for (uint32_t y = 0; y < achievement.iconHeight; ++y) {
                    uint32_t y0 = y * height / achievement.iconHeight;
                    uint32_t y1 = std::max(y0 + 1, (y + 1) * height / achievement.iconHeight);
                    for (uint32_t x = 0; x < achievement.iconWidth; ++x) {
                        uint32_t x0 = x * width / achievement.iconWidth;
                        uint32_t x1 = std::max(x0 + 1, (x + 1) * width / achievement.iconWidth);
                        uint64_t sum[4] {};
                        for (uint32_t sy = y0; sy < y1; ++sy) {
                            for (uint32_t sx = x0; sx < x1; ++sx) {
                                for (int c = 0; c < 4; ++c) {
                                    sum[c] += rgba[(size_t(sy) * width + sx) * 4 + c];
                                }
                            }
                        }
                        uint64_t count = uint64_t(x1 - x0) * (y1 - y0);
                        for (int c = 0; c < 4; ++c) {
                            achievement.icon[(size_t(y) * achievement.iconWidth + x) * 4 + c] = static_cast<uint8_t>(sum[c] / count);
                        }
                    }
                }
            }
        }
        return achievement;
    };
    for (size_t i = 0; i < open.size() && i < 3 && !cancelled; ++i) {
        loaded.suggested.push_back(withIcon(*open[i]));
    }
    for (size_t i = 0; i < earned.size() && i < 3 && !cancelled; ++i) {
        loaded.recent.push_back(withIcon(*earned[i]));
    }

    HttpResponse stats;
    if (request("https://userstats.xboxlive.com/users/xuid(" + xuid + ")/scids/" + GameServiceConfigId + "/stats/MinutesPlayed,BlockBrokenTotal,MobKilled.IsMonster.1,DistanceTravelled", "1", stats)) {
        std::unique_ptr<json::Value> root = json::parse(stats.mBody);
        const json::Value* list = root ? root->get("stats") : nullptr;
        if (list && list->isArray()) {
            for (const std::unique_ptr<json::Value>& stat : list->mArray) {
                const json::Value* name = stat->get("statname");
                const json::Value* value = stat->get("value");
                if (!name || !value) {
                    continue;
                }
                int64_t number = static_cast<int64_t>(std::strtod(value->string().c_str(), nullptr));
                if (name->string() == "MinutesPlayed") {
                    loaded.minutesPlayed = number;
                } else if (name->string() == "BlockBrokenTotal") {
                    loaded.blocksBroken = number;
                } else if (name->string() == "MobKilled.IsMonster.1") {
                    loaded.mobsDefeated = number;
                } else if (name->string() == "DistanceTravelled") {
                    loaded.distanceTravelled = number;
                }
            }
            loaded.statsLoaded = true;
        }
    }

    std::lock_guard<std::mutex> guard(mutex);
    if (cancelled || current.state != AccountState::SignedIn) {
        return;
    }
    loaded.revision = ++profileRevision;
    current.profile = std::move(loaded);
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
    url = XboxSocialService::sizedPicture(url, 128);
    if (url.empty()) {
        return;
    }

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
    service.setCancelFlag(&cancelled);
    bool loaded = service.requestRealms(descriptions, error);

    std::vector<Realm> realms;
    for (const RealmDescription& description : descriptions) {
        Realm realm;
        realm.id = description.mId;
        realm.name = description.mName;
        realm.owner = description.mOwner;
        realm.ownerXuid = description.mOwnerUuid;
        realm.description = description.mMotd;
        realm.open = description.mState == "OPEN";
        realm.expired = description.mExpired;
        realm.onlinePlayers = static_cast<int>(std::count_if(description.mPlayers.begin(), description.mPlayers.end(), [](const RealmPlayer& player) {
            return player.mOnline;
        }));
        realm.maxPlayers = description.mMaxPlayers;
        realms.push_back(std::move(realm));
    }

    std::lock_guard<std::mutex> guard(mutex);
    if (cancelled || current.state != AccountState::SignedIn) {
        return;
    }
    if (loaded) {
        current.realms = std::move(realms);
    }
    current.realmsError = loaded ? std::string() : "Realms: " + error;
    current.realmsRateLimited = !loaded && service.getLastError().mKind == ServiceErrorKind::RateLimited;
    current.realmsLoading = false;
}

}
