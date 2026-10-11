#include "client/Social.h"

#include "Network/Auth/MinecraftAuthentication.h"
#include "Network/Http/HttpClient.h"
#include "Network/Session/MultiplayerSessionDirectory.h"
#include "Network/Session/RealmsService.h"
#include "Network/Session/XboxSocialService.h"
#include "client/DebugLog.h"
#include "client/Session.h"
#include "ui/Font.h"
#include "ui/Image.h"

#include <algorithm>
#include <cctype>
#include <iterator>

namespace kestrel {

namespace {

constexpr size_t MaxAvatars = 48;
constexpr int AvatarRequestSize = 64;
constexpr int AvatarTimeoutMs = 10000;
constexpr size_t MaxAvatarBytes = 2 * 1024 * 1024;
constexpr auto MinimumRetryDelay = std::chrono::seconds(5);

using Clock = std::chrono::steady_clock;

OnlineError toOnlineError(const ServiceError& error)
{
    switch (error.mKind) {
    case ServiceErrorKind::Unauthorized:
        return OnlineError::Unauthorized;
    case ServiceErrorKind::Forbidden:
        return OnlineError::Forbidden;
    case ServiceErrorKind::NotFound:
        return OnlineError::NotFound;
    case ServiceErrorKind::Conflict:
        return OnlineError::Conflict;
    case ServiceErrorKind::RateLimited:
        return OnlineError::RateLimited;
    case ServiceErrorKind::Unavailable:
        return OnlineError::Unavailable;
    case ServiceErrorKind::Timeout:
        return OnlineError::Timeout;
    case ServiceErrorKind::Network:
        return OnlineError::Network;
    case ServiceErrorKind::Cancelled:
    case ServiceErrorKind::None:
    case ServiceErrorKind::InvalidResponse:
    case ServiceErrorKind::Failed:
        return OnlineError::Failed;
    }
    return OnlineError::Failed;
}

std::string lowered(std::string text)
{
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string trimmed(const std::string& text)
{
    size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

long long millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

void logOutcome(const char* operation, const ServiceError& error, bool succeeded, Clock::time_point start)
{
    debugLog(std::string("social: ") + operation + " " + (succeeded ? "ok" : ServiceError::name(error.mKind)) + " in " + std::to_string(millisecondsSince(start)) + " ms");
}

SocialPerson toPerson(const XboxPerson& person)
{
    SocialPerson result;
    result.xuid = person.mXuid;
    result.gamertag = person.mGamertag.empty() ? person.mDisplayName : person.mGamertag;
    result.online = person.mPresenceState == "Online";
    result.isFriend = person.mFriend;
    result.requestReceived = person.mFriendRequestReceived;
    result.requestSent = person.mFriendRequestSent;
    result.canBeFriended = person.mCanBeFriended;
    result.hasPicture = !person.mDisplayPicture.empty();
    for (const XboxTitlePresence& title : person.mTitles) {
        if (title.mPrimary && title.mState == "Active") {
            result.activity = !title.mRichPresenceText.empty() ? title.mRichPresenceText : title.mPresenceText;
        }
    }
    if (result.activity.empty()) {
        result.activity = person.mPresenceText;
    }
    return result;
}

void sortPeople(std::vector<SocialPerson>& people)
{
    std::stable_sort(people.begin(), people.end(), [](const SocialPerson& a, const SocialPerson& b) {
        if (joinable(a) != joinable(b)) {
            return joinable(a);
        }
        if (a.online != b.online) {
            return a.online;
        }
        std::string left = lowered(a.gamertag);
        std::string right = lowered(b.gamertag);
        return left != right ? left < right : a.xuid < b.xuid;
    });
}

std::vector<SocialPerson> toPeople(const std::vector<XboxPerson>& people, std::vector<std::pair<std::string, std::string>>& pictures)
{
    std::vector<SocialPerson> result;
    result.reserve(people.size());
    for (const XboxPerson& person : people) {
        result.push_back(toPerson(person));
        if (!person.mDisplayPicture.empty()) {
            pictures.emplace_back(person.mXuid, person.mDisplayPicture);
        }
    }
    return result;
}

void removePerson(std::vector<SocialPerson>& people, const std::string& xuid)
{
    people.erase(std::remove_if(people.begin(), people.end(), [&](const SocialPerson& person) {
        return person.xuid == xuid;
    }), people.end());
}

SocialPerson* findPerson(std::vector<SocialPerson>& people, const std::string& xuid)
{
    auto found = std::find_if(people.begin(), people.end(), [&](const SocialPerson& person) {
        return person.xuid == xuid;
    });
    return found == people.end() ? nullptr : &*found;
}

int secondsUntil(Clock::time_point when)
{
    auto left = std::chrono::duration_cast<std::chrono::seconds>(when - Clock::now()).count();
    return left > 0 ? static_cast<int>(left) : 0;
}

Clock::time_point retryTime(const ServiceError& error)
{
    auto wait = std::max<std::chrono::milliseconds>(std::chrono::milliseconds(error.mRetryAfterMs), MinimumRetryDelay);
    return Clock::now() + wait;
}

}

Social::Social()
    : cancel(std::make_shared<std::atomic<bool>>(false))
{
    worker = std::thread([this] {
        run();
    });
}

Social::~Social()
{
    {
        std::lock_guard<std::mutex> guard(mutex);
        stopping = true;
        cancel->store(true);
        jobs.clear();
        backgroundJobs.clear();
    }
    wake.notify_all();
    if (worker.joinable()) {
        worker.join();
    }
}

void Social::setAccount(std::shared_ptr<MinecraftAuthentication> signedIn, const std::string& xuid)
{
    std::unique_lock<std::mutex> lock(mutex);
    std::shared_ptr<MinecraftAuthentication> next = xuid.empty() ? nullptr : std::move(signedIn);
    if (xuid == state.xuid && next == authentication) {
        return;
    }
    cancel->store(true);
    jobs.clear();
    backgroundJobs.clear();
    ++generation;
    authentication = std::move(next);
    cancel = std::make_shared<std::atomic<bool>>(false);
    uint64_t revision = state.revision;
    state = SocialSnapshot {};
    state.revision = revision;
    state.signedIn = authentication != nullptr;
    state.xuid = xuid;
    friendsLoadedAt = {};
    friendsRetryAt = {};
    requestsRetryAt = {};
    searchRetryAt = {};
    invitesRetryAt = {};
    realmCode.clear();
    ++realmCodeRequest;
    avatarsRequested.clear();
    avatars.clear();
    publish();
    if (xuid.empty()) {
        idle.wait_for(lock, std::chrono::seconds(20), [this] {
            return !busy;
        });
    }
}

void Social::setLanguage(std::string code)
{
    std::replace(code.begin(), code.end(), '_', '-');
    std::lock_guard<std::mutex> guard(mutex);
    language = std::move(code);
}

uint64_t Social::revision() const
{
    return changes.load();
}

SocialSnapshot Social::snapshot() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return state;
}

std::vector<SocialAvatar> Social::takeAvatars()
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<SocialAvatar> taken = std::move(avatars);
    avatars.clear();
    return taken;
}

Social::Ticket Social::ticket() const
{
    return { generation, authentication, cancel };
}

bool Social::current(const Ticket& ticket) const
{
    return ticket.generation == generation && !stopping;
}

void Social::publish()
{
    ++state.revision;
    ++changes;
}

void Social::setOperation(const std::string& key, bool pending, OnlineError error)
{
    if (!pending && error == OnlineError::None) {
        state.operations.erase(key);
    } else {
        state.operations[key] = { pending, error };
    }
    publish();
}

void Social::post(Job job, bool background)
{
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (stopping) {
            return;
        }
        (background ? backgroundJobs : jobs).push_back(std::move(job));
    }
    wake.notify_one();
}

void Social::run()
{
    std::unique_lock<std::mutex> lock(mutex);
    for (;;) {
        wake.wait(lock, [this] {
            return stopping || !jobs.empty() || !backgroundJobs.empty();
        });
        if (stopping) {
            return;
        }
        std::deque<Job>& queue = !jobs.empty() ? jobs : backgroundJobs;
        Job job = std::move(queue.front());
        queue.pop_front();
        busy = true;
        lock.unlock();
        job();
        lock.lock();
        busy = false;
        idle.notify_all();
    }
}

void Social::queueRefresh(bool friends, bool requests)
{
    Ticket queued = ticket();
    if (!queued.authentication) {
        return;
    }
    if (friends && !state.friends.loading) {
        state.friends.loading = true;
        jobs.push_back([this, queued] {
            refreshFriends(queued);
        });
    }
    if (requests && !state.received.loading) {
        state.received.loading = true;
        state.sent.loading = true;
        jobs.push_back([this, queued] {
            refreshRequests(queued);
        });
    }
    publish();
    wake.notify_one();
}

void Social::refreshFriendsIfStale(std::chrono::seconds maxAge)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (!authentication || state.friends.loading || Clock::now() < friendsRetryAt) {
        return;
    }
    if (state.friends.loaded && Clock::now() - friendsLoadedAt < maxAge) {
        return;
    }
    if (!state.friends.loaded && state.friends.error != OnlineError::None) {
        return;
    }
    queueRefresh(true, !state.received.loaded && state.received.error == OnlineError::None);
}

void Social::handle(const SocialRequest& request)
{
    std::unique_lock<std::mutex> lock(mutex);
    Ticket queued = ticket();
    bool signedIn = queued.authentication != nullptr;
    auto refuse = [&](const std::string& key) {
        if (!signedIn) {
            setOperation(key, false, OnlineError::SignedOut);
            return true;
        }
        auto found = state.operations.find(key);
        return found != state.operations.end() && found->second.pending;
    };

    switch (request.action) {
    case SocialAction::RefreshFriends:
        if (!signedIn) {
            state.friends.error = OnlineError::SignedOut;
            publish();
        } else if (Clock::now() < friendsRetryAt) {
            state.friends.error = OnlineError::RateLimited;
            state.friends.retryAfterSeconds = secondsUntil(friendsRetryAt);
            publish();
        } else {
            queueRefresh(true, false);
        }
        break;
    case SocialAction::RefreshRequests:
        if (!signedIn) {
            state.received.error = OnlineError::SignedOut;
            publish();
        } else if (Clock::now() < requestsRetryAt) {
            state.received.error = OnlineError::RateLimited;
            state.received.retryAfterSeconds = secondsUntil(requestsRetryAt);
            publish();
        } else {
            queueRefresh(false, true);
        }
        break;
    case SocialAction::Search: {
        std::string query = trimmed(request.target);
        if (query.empty()) {
            state.search = {};
            state.searchQuery.clear();
            publish();
            break;
        }
        state.searchQuery = query;
        if (!signedIn) {
            state.search = {};
            state.search.error = OnlineError::SignedOut;
        } else if (Clock::now() < searchRetryAt) {
            state.search.error = OnlineError::RateLimited;
            state.search.retryAfterSeconds = secondsUntil(searchRetryAt);
        } else {
            state.search.loading = true;
            state.search.error = OnlineError::None;
            jobs.push_back([this, queued, query] {
                search(queued, query);
            });
            wake.notify_one();
        }
        publish();
        break;
    }
    case SocialAction::ClearSearch:
        state.search = {};
        state.searchQuery.clear();
        publish();
        break;
    case SocialAction::SendRequest:
    case SocialAction::AcceptRequest:
    case SocialAction::DeclineRequest:
    case SocialAction::CancelRequest:
    case SocialAction::RemoveFriend: {
        std::string key = personOperation(request.target);
        if (refuse(key)) {
            break;
        }
        if (!XboxSocialService::isValidXuid(request.target) || request.target == state.xuid) {
            setOperation(key, false, OnlineError::Invalid);
            break;
        }
        setOperation(key, true, OnlineError::None);
        SocialAction action = request.action;
        std::string xuid = request.target;
        jobs.push_back([this, queued, action, xuid] {
            mutateFriend(queued, action, xuid);
        });
        wake.notify_one();
        break;
    }
    case SocialAction::RefreshInvites:
        if (!signedIn) {
            state.invitesError = OnlineError::SignedOut;
            publish();
        } else if (Clock::now() < invitesRetryAt) {
            state.invitesError = OnlineError::RateLimited;
            state.invitesRetryAfterSeconds = secondsUntil(invitesRetryAt);
            publish();
        } else if (!state.invitesLoading) {
            state.invitesLoading = true;
            publish();
            jobs.push_back([this, queued] {
                refreshInvites(queued);
            });
            wake.notify_one();
        }
        break;
    case SocialAction::AcceptInvite:
    case SocialAction::DeclineInvite: {
        std::string key = inviteOperation(request.target);
        if (refuse(key)) {
            break;
        }
        setOperation(key, true, OnlineError::None);
        bool accept = request.action == SocialAction::AcceptInvite;
        std::string id = request.target;
        jobs.push_back([this, queued, id, accept] {
            answerInvite(queued, id, accept);
        });
        wake.notify_one();
        break;
    }
    case SocialAction::CheckRealmCode: {
        uint64_t lookup = ++realmCodeRequest;
        state.realmCode = {};
        realmCode = RealmsService::parseInvite(request.target, true);
        if (!signedIn) {
            state.realmCode.state = RealmCodeState::Failed;
            state.realmCode.error = OnlineError::SignedOut;
        } else if (realmCode.empty()) {
            state.realmCode.state = RealmCodeState::Failed;
            state.realmCode.error = OnlineError::Invalid;
        } else {
            state.realmCode.state = RealmCodeState::Checking;
            std::string code = realmCode;
            jobs.push_back([this, queued, lookup, code] {
                lookUpRealmCode(queued, lookup, code);
            });
            wake.notify_one();
        }
        publish();
        break;
    }
    case SocialAction::JoinRealmCode: {
        if (state.realmCode.state != RealmCodeState::Found || realmCode.empty()) {
            break;
        }
        if (state.realmCode.member) {
            state.realmCode.state = RealmCodeState::Ready;
            publish();
            break;
        }
        uint64_t lookup = realmCodeRequest;
        state.realmCode.state = RealmCodeState::Joining;
        std::string code = realmCode;
        jobs.push_back([this, queued, lookup, code] {
            acceptRealmCode(queued, lookup, code);
        });
        wake.notify_one();
        publish();
        break;
    }
    case SocialAction::CancelRealmCode:
        ++realmCodeRequest;
        realmCode.clear();
        state.realmCode = {};
        publish();
        break;
    }
}

void Social::refreshFriends(const Ticket& ticket)
{
    if (ticket.cancel->load() || !ticket.authentication) {
        return;
    }
    Clock::time_point start = Clock::now();
    std::string languageCode;
    {
        std::lock_guard<std::mutex> guard(mutex);
        languageCode = language;
    }
    XboxSocialService service(*ticket.authentication);
    service.setCancelFlag(ticket.cancel.get());
    service.setLanguage(languageCode);
    std::vector<XboxPerson> people;
    std::string error;
    bool loaded = service.requestPeople(XboxPeopleList::Friends, people, error);
    logOutcome("friends", service.getLastError(), loaded, start);

    std::vector<MultiplayerWorld> worlds;
    if (loaded && !ticket.cancel->load()) {
        Clock::time_point worldsStart = Clock::now();
        MultiplayerSessionDirectory directory(*ticket.authentication);
        std::string worldsError;
        bool listed = directory.queryWorlds(worlds, worldsError);
        debugLog(std::string("social: joinable worlds ") + (listed ? "ok" : "failed") + " in " + std::to_string(millisecondsSince(worldsStart)) + " ms");
    }

    std::vector<std::pair<std::string, std::string>> pictures;
    std::vector<SocialPerson> friends = toPeople(people, pictures);
    for (SocialPerson& person : friends) {
        person.isFriend = true;
        for (const MultiplayerWorld& world : worlds) {
            if (world.mOwnerXuid != person.xuid || world.mHandleId.empty()) {
                continue;
            }
            SessionConnectionTarget target;
            std::string ignored;
            person.sessionHandle = world.mHandleId;
            person.worldName = !world.mWorldName.empty() ? world.mWorldName : world.mHostName;
            person.members = world.mMemberCount;
            person.maxMembers = world.mMaxMemberCount;
            person.versionMismatch = world.mProtocol != 0 && world.mProtocol != Session::protocolVersion();
            person.unreachable = !world.selectConnection(target, ignored);
            break;
        }
    }
    sortPeople(friends);

    std::lock_guard<std::mutex> guard(mutex);
    if (!current(ticket)) {
        return;
    }
    state.friends.loading = false;
    if (loaded) {
        state.friends.loaded = true;
        state.friends.error = OnlineError::None;
        state.friends.retryAfterSeconds = 0;
        state.friends.people = std::move(friends);
        friendsLoadedAt = Clock::now();
    } else if (service.getLastError().mKind != ServiceErrorKind::Cancelled) {
        state.friends.error = toOnlineError(service.getLastError());
        if (state.friends.error == OnlineError::RateLimited) {
            friendsRetryAt = retryTime(service.getLastError());
            state.friends.retryAfterSeconds = secondsUntil(friendsRetryAt);
        }
    }
    publish();
    queueAvatars(ticket, pictures);
}

void Social::refreshRequests(const Ticket& ticket)
{
    if (ticket.cancel->load() || !ticket.authentication) {
        return;
    }
    Clock::time_point start = Clock::now();
    XboxSocialService service(*ticket.authentication);
    service.setCancelFlag(ticket.cancel.get());
    std::vector<XboxPerson> received;
    std::vector<XboxPerson> sent;
    std::string error;
    bool receivedLoaded = service.requestPeople(XboxPeopleList::ReceivedRequests, received, error);
    ServiceError receivedError = service.getLastError();
    bool sentLoaded = receivedLoaded && service.requestPeople(XboxPeopleList::SentRequests, sent, error);
    ServiceError sentError = receivedLoaded ? service.getLastError() : receivedError;
    logOutcome("friend requests", sentError, receivedLoaded && sentLoaded, start);

    std::vector<std::pair<std::string, std::string>> pictures;
    std::vector<SocialPerson> receivedPeople = toPeople(received, pictures);
    std::vector<SocialPerson> sentPeople = toPeople(sent, pictures);
    for (SocialPerson& person : receivedPeople) {
        person.requestReceived = true;
    }
    for (SocialPerson& person : sentPeople) {
        person.requestSent = true;
    }
    sortPeople(receivedPeople);
    sortPeople(sentPeople);

    std::lock_guard<std::mutex> guard(mutex);
    if (!current(ticket)) {
        return;
    }
    auto apply = [&](PeopleList& list, bool loaded, const ServiceError& failure, std::vector<SocialPerson>& people) {
        list.loading = false;
        if (loaded) {
            list.loaded = true;
            list.error = OnlineError::None;
            list.retryAfterSeconds = 0;
            list.people = std::move(people);
        } else if (failure.mKind != ServiceErrorKind::Cancelled) {
            list.error = toOnlineError(failure);
            if (list.error == OnlineError::RateLimited) {
                requestsRetryAt = retryTime(failure);
                list.retryAfterSeconds = secondsUntil(requestsRetryAt);
            }
        }
    };
    apply(state.received, receivedLoaded, receivedError, receivedPeople);
    apply(state.sent, sentLoaded, sentError, sentPeople);
    publish();
    queueAvatars(ticket, pictures);
}

void Social::search(const Ticket& ticket, const std::string& query)
{
    if (ticket.cancel->load() || !ticket.authentication) {
        return;
    }
    Clock::time_point start = Clock::now();
    XboxSocialService service(*ticket.authentication);
    service.setCancelFlag(ticket.cancel.get());
    std::vector<XboxPerson> found;
    std::string error;
    bool loaded = service.search(query, found, error);
    logOutcome("search", service.getLastError(), loaded, start);

    std::vector<std::pair<std::string, std::string>> pictures;
    std::vector<SocialPerson> people = toPeople(found, pictures);

    std::lock_guard<std::mutex> guard(mutex);
    if (!current(ticket) || state.searchQuery != query) {
        return;
    }
    state.search.loading = false;
    if (loaded) {
        state.search.loaded = true;
        state.search.error = OnlineError::None;
        state.search.people = std::move(people);
    } else if (service.getLastError().mKind != ServiceErrorKind::Cancelled) {
        state.search.loaded = false;
        state.search.people.clear();
        state.search.error = toOnlineError(service.getLastError());
        if (state.search.error == OnlineError::RateLimited) {
            searchRetryAt = retryTime(service.getLastError());
            state.search.retryAfterSeconds = secondsUntil(searchRetryAt);
        }
    }
    publish();
    queueAvatars(ticket, pictures);
}

void Social::mutateFriend(const Ticket& ticket, SocialAction action, const std::string& xuid)
{
    std::string key = personOperation(xuid);
    if (ticket.cancel->load() || !ticket.authentication) {
        return;
    }
    Clock::time_point start = Clock::now();
    XboxSocialService service(*ticket.authentication);
    service.setCancelFlag(ticket.cancel.get());
    std::string error;
    bool adding = action == SocialAction::SendRequest || action == SocialAction::AcceptRequest;
    bool done = adding ? service.addFriend(xuid, error) : service.removeFriend(xuid, error);
    logOutcome(adding ? "add friend" : "remove friend", service.getLastError(), done, start);

    std::lock_guard<std::mutex> guard(mutex);
    if (!current(ticket)) {
        return;
    }
    if (!done) {
        OnlineError failure = toOnlineError(service.getLastError());
        setOperation(key, false, failure);
        if (failure == OnlineError::NotFound || failure == OnlineError::Conflict) {
            queueRefresh(true, true);
        }
        return;
    }
    switch (action) {
    case SocialAction::SendRequest:
        if (SocialPerson* person = findPerson(state.search.people, xuid)) {
            person->requestSent = true;
            if (!findPerson(state.sent.people, xuid)) {
                state.sent.people.push_back(*person);
            }
        }
        break;
    case SocialAction::AcceptRequest:
        if (SocialPerson* person = findPerson(state.received.people, xuid)) {
            SocialPerson accepted = *person;
            accepted.requestReceived = false;
            accepted.isFriend = true;
            removePerson(state.received.people, xuid);
            if (!findPerson(state.friends.people, xuid)) {
                state.friends.people.push_back(std::move(accepted));
                sortPeople(state.friends.people);
            }
        }
        if (SocialPerson* person = findPerson(state.search.people, xuid)) {
            person->requestReceived = false;
            person->isFriend = true;
        }
        break;
    case SocialAction::DeclineRequest:
        removePerson(state.received.people, xuid);
        if (SocialPerson* person = findPerson(state.search.people, xuid)) {
            person->requestReceived = false;
        }
        break;
    case SocialAction::CancelRequest:
        removePerson(state.sent.people, xuid);
        if (SocialPerson* person = findPerson(state.search.people, xuid)) {
            person->requestSent = false;
        }
        break;
    case SocialAction::RemoveFriend:
        removePerson(state.friends.people, xuid);
        if (SocialPerson* person = findPerson(state.search.people, xuid)) {
            person->isFriend = false;
        }
        break;
    default:
        break;
    }
    setOperation(key, false, OnlineError::None);
    queueRefresh(action == SocialAction::AcceptRequest || action == SocialAction::RemoveFriend, true);
}

void Social::refreshInvites(const Ticket& ticket)
{
    if (ticket.cancel->load() || !ticket.authentication) {
        return;
    }
    Clock::time_point start = Clock::now();
    RealmsService service(*ticket.authentication);
    service.setCancelFlag(ticket.cancel.get());
    std::vector<RealmInvite> invites;
    std::string error;
    bool loaded = service.requestPendingInvites(invites, error);
    logOutcome("realm invites", service.getLastError(), loaded, start);

    std::vector<RealmInvitation> list;
    for (const RealmInvite& invite : invites) {
        list.push_back({ invite.mInvitationId, invite.mWorldName, invite.mWorldDescription, invite.mOwnerName, invite.mDate });
    }
    std::stable_sort(list.begin(), list.end(), [](const RealmInvitation& a, const RealmInvitation& b) {
        return a.date != b.date ? a.date > b.date : a.id < b.id;
    });

    std::lock_guard<std::mutex> guard(mutex);
    if (!current(ticket)) {
        return;
    }
    state.invitesLoading = false;
    if (loaded) {
        state.invitesLoaded = true;
        state.invitesError = OnlineError::None;
        state.invitesRetryAfterSeconds = 0;
        state.invites = std::move(list);
        for (auto it = state.operations.begin(); it != state.operations.end();) {
            bool stale = it->first.rfind("invite:", 0) == 0 && !it->second.pending && std::none_of(state.invites.begin(), state.invites.end(), [&](const RealmInvitation& invite) {
                return inviteOperation(invite.id) == it->first;
            });
            it = stale ? state.operations.erase(it) : std::next(it);
        }
    } else if (service.getLastError().mKind != ServiceErrorKind::Cancelled) {
        state.invitesError = toOnlineError(service.getLastError());
        if (state.invitesError == OnlineError::RateLimited) {
            invitesRetryAt = retryTime(service.getLastError());
            state.invitesRetryAfterSeconds = secondsUntil(invitesRetryAt);
        }
    }
    publish();
}

void Social::answerInvite(const Ticket& ticket, const std::string& id, bool accept)
{
    std::string key = inviteOperation(id);
    if (ticket.cancel->load() || !ticket.authentication) {
        return;
    }
    Clock::time_point start = Clock::now();
    RealmsService service(*ticket.authentication);
    service.setCancelFlag(ticket.cancel.get());
    std::string error;
    bool done = accept ? service.acceptInvite(id, error) : service.rejectInvite(id, error);
    logOutcome(accept ? "accept realm invite" : "decline realm invite", service.getLastError(), done, start);

    std::lock_guard<std::mutex> guard(mutex);
    if (!current(ticket)) {
        return;
    }
    if (!done) {
        OnlineError failure = toOnlineError(service.getLastError());
        setOperation(key, false, failure);
        if ((failure == OnlineError::NotFound || failure == OnlineError::Conflict) && !state.invitesLoading) {
            state.invitesLoading = true;
            Ticket queued = ticket;
            jobs.push_back([this, queued] {
                refreshInvites(queued);
            });
            wake.notify_one();
        }
        return;
    }
    state.invites.erase(std::remove_if(state.invites.begin(), state.invites.end(), [&](const RealmInvitation& invite) {
        return invite.id == id;
    }), state.invites.end());
    if (accept) {
        ++state.realmsChanged;
    }
    setOperation(key, false, OnlineError::None);
}

void Social::lookUpRealmCode(const Ticket& ticket, uint64_t request, const std::string& code)
{
    if (ticket.cancel->load() || !ticket.authentication) {
        return;
    }
    Clock::time_point start = Clock::now();
    RealmsService service(*ticket.authentication);
    service.setCancelFlag(ticket.cancel.get());
    RealmDescription realm;
    std::string error;
    bool found = service.requestRealmByCode(code, realm, error);
    logOutcome("realm code lookup", service.getLastError(), found, start);

    std::lock_guard<std::mutex> guard(mutex);
    if (!current(ticket) || request != realmCodeRequest) {
        return;
    }
    if (!found) {
        state.realmCode.state = RealmCodeState::Failed;
        state.realmCode.error = toOnlineError(service.getLastError());
    } else {
        state.realmCode.state = RealmCodeState::Found;
        state.realmCode.error = OnlineError::None;
        state.realmCode.realmName = realm.mName;
        state.realmCode.owner = realm.mOwner;
        state.realmCode.realmId = realm.mId;
        state.realmCode.member = realm.mMember;
        state.realmCode.open = realm.mState == "OPEN";
        state.realmCode.expired = realm.mExpired;
    }
    publish();
}

void Social::acceptRealmCode(const Ticket& ticket, uint64_t request, const std::string& code)
{
    if (ticket.cancel->load() || !ticket.authentication) {
        return;
    }
    Clock::time_point start = Clock::now();
    RealmsService service(*ticket.authentication);
    service.setCancelFlag(ticket.cancel.get());
    RealmDescription realm;
    std::string error;
    bool joined = service.acceptInviteCode(code, realm, error);
    logOutcome("realm code accept", service.getLastError(), joined, start);

    std::lock_guard<std::mutex> guard(mutex);
    if (!current(ticket)) {
        return;
    }
    if (joined) {
        ++state.realmsChanged;
    }
    if (request != realmCodeRequest) {
        publish();
        return;
    }
    if (!joined) {
        state.realmCode.state = RealmCodeState::Failed;
        state.realmCode.error = toOnlineError(service.getLastError());
    } else {
        state.realmCode.state = RealmCodeState::Ready;
        state.realmCode.member = true;
        if (realm.mId != 0) {
            state.realmCode.realmId = realm.mId;
            state.realmCode.open = realm.mState == "OPEN";
            state.realmCode.expired = realm.mExpired;
        }
    }
    publish();
}

void Social::queueAvatars(const Ticket& ticket, const std::vector<std::pair<std::string, std::string>>& pictures)
{
    for (const auto& [xuid, url] : pictures) {
        if (avatarsRequested.size() >= MaxAvatars || avatarsRequested.count(xuid)) {
            continue;
        }
        avatarsRequested.insert(xuid);
        Ticket queued = ticket;
        std::string owner = xuid;
        std::string address = url;
        backgroundJobs.push_back([this, queued, owner, address] {
            fetchAvatar(queued, owner, address);
        });
    }
    wake.notify_one();
}

void Social::fetchAvatar(const Ticket& ticket, const std::string& xuid, const std::string& url)
{
    if (ticket.cancel->load()) {
        return;
    }
    std::string address = XboxSocialService::sizedPicture(url, AvatarRequestSize);
    if (address.empty()) {
        return;
    }
    HttpResponse image;
    std::string error;
    if (!HttpClient::get(address, {}, image, error, AvatarTimeoutMs, MaxAvatarBytes, ticket.cancel.get())) {
        return;
    }
    std::string location = image.getHeader("Location");
    if (image.mStatus >= 300 && image.mStatus < 400 && location.rfind("https://", 0) == 0) {
        if (!HttpClient::get(location, {}, image, error, AvatarTimeoutMs, MaxAvatarBytes, ticket.cancel.get())) {
            return;
        }
    }
    std::vector<uint8_t> pixels;
    if (image.mStatus != 200 || !ui::decodeSquareImage(image.mBody, ui::Font::ImageSlotSize, pixels)) {
        return;
    }
    std::lock_guard<std::mutex> guard(mutex);
    if (!current(ticket)) {
        return;
    }
    avatars.push_back({ xuid, std::move(pixels) });
    ++changes;
}

}
