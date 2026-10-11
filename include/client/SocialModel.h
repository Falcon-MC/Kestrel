#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kestrel {

/**
 * Why an online request failed, in terms the interface can explain: the
 * player must sign in, is not allowed, the thing is gone, it changed in the
 * meantime, too many requests, the service or the network is down.
 */
enum class OnlineError {
    None,
    SignedOut,
    Unauthorized,
    Forbidden,
    NotFound,
    Conflict,
    RateLimited,
    Unavailable,
    Timeout,
    Network,
    Invalid,
    Failed,
};

/**
 * Someone on Xbox Live as the social drawer lists them. The XUID is the key;
 * the gamertag can change. A session handle is set only when the friend is
 * in a Minecraft world the service lists as joinable.
 */
struct SocialPerson {
    std::string xuid;
    std::string gamertag;
    std::string activity;
    bool online = false;
    bool isFriend = false;
    bool requestReceived = false;
    bool requestSent = false;
    bool canBeFriended = false;
    bool hasPicture = false;
    std::string sessionHandle;
    std::string worldName;
    int members = 0;
    int maxMembers = 0;
    bool versionMismatch = false;
    bool unreachable = false;
};

struct PeopleList {
    bool loading = false;
    bool loaded = false;
    OnlineError error = OnlineError::None;
    int retryAfterSeconds = 0;
    std::vector<SocialPerson> people;
};

struct RealmInvitation {
    std::string id;
    std::string worldName;
    std::string description;
    std::string ownerName;
    int64_t date = 0;
};

enum class RealmCodeState {
    Idle,
    Checking,
    Found,
    Joining,
    Ready,
    Failed,
};

/**
 * Where joining a Realm from an invite code or link stands. The code itself
 * stays inside the service.
 */
struct RealmCodeView {
    RealmCodeState state = RealmCodeState::Idle;
    OnlineError error = OnlineError::None;
    std::string realmName;
    std::string owner;
    int64_t realmId = 0;
    bool member = false;
    bool open = false;
    bool expired = false;
};

struct SocialOperation {
    bool pending = false;
    OnlineError error = OnlineError::None;
};

struct SocialSnapshot {
    uint64_t revision = 0;
    bool signedIn = false;
    std::string xuid;
    PeopleList friends;
    PeopleList received;
    PeopleList sent;
    PeopleList search;
    std::string searchQuery;
    bool invitesLoading = false;
    bool invitesLoaded = false;
    OnlineError invitesError = OnlineError::None;
    int invitesRetryAfterSeconds = 0;
    std::vector<RealmInvitation> invites;
    RealmCodeView realmCode;
    std::map<std::string, SocialOperation> operations;
    uint64_t realmsChanged = 0;
};

enum class SocialAction {
    RefreshFriends,
    RefreshRequests,
    Search,
    ClearSearch,
    SendRequest,
    AcceptRequest,
    DeclineRequest,
    CancelRequest,
    RemoveFriend,
    RefreshInvites,
    AcceptInvite,
    DeclineInvite,
    CheckRealmCode,
    JoinRealmCode,
    CancelRealmCode,
};

struct SocialRequest {
    SocialAction action = SocialAction::RefreshFriends;
    std::string target;
};

inline size_t onlineCount(const PeopleList& list)
{
    size_t count = 0;
    for (const SocialPerson& person : list.people) {
        count += person.online ? 1 : 0;
    }
    return count;
}

/**
 * The key of the pending state of an action on a person or an invitation.
 */
inline std::string personOperation(const std::string& xuid)
{
    return "person:" + xuid;
}

inline std::string inviteOperation(const std::string& id)
{
    return "invite:" + id;
}

}
