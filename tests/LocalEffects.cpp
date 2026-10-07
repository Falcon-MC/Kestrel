#include "modding/LocalEffects.h"
#include "mod/Api.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>

using namespace kestrel;
using Request = modding::LocalEffects::Request;

namespace {

void check(bool value, const char* description)
{
    if (!value) { std::fprintf(stderr, "FAIL %s\n", description); std::exit(1); }
}

class Bus : public mod::EventBus {
public:
    modding::LocalEffects* effects = nullptr;
    size_t owner = 1;
    mod::Subscription subscribe(std::string_view, Handler, mod::ListenOptions) override { return {}; }
    void post(mod::Event& event) override
    {
        if (effects && event.type() == Request::Type) effects->process(owner, static_cast<Request&>(event));
    }
};

}

int main()
{
    Bus oldHost;
    check(!mod::Particles(oldHost).supported() && !mod::Audio(oldHost).supported(), "older host has no effects extension");
    check(mod::Audio(oldHost).play({ .name = "test:sound" }) == 0, "older host rejects creation safely");

    uint64_t next = 0;
    std::set<uint64_t> active;
    size_t creates = 0;
    modding::LocalEffects effects([&](Request& request) {
        request.result = true;
        switch (request.action) {
        case Request::Action::Create: ++creates; request.handle = ++next; active.insert(next); break;
        case Request::Action::Active: request.result = active.contains(request.handle); break;
        case Request::Action::Remove: request.result = active.erase(request.handle) != 0; break;
        case Request::Action::Move:
        case Request::Action::Volume: request.result = active.contains(request.handle); break;
        default: break;
        }
    });
    Bus bus;
    bus.effects = &effects;
    mod::Particles particles(bus);
    mod::Audio audio(bus);
    check(particles.supported() && audio.supported(), "extension supported");
    auto particle = particles.spawn({ .identifier = "test:particle" });
    auto sound = audio.play({ .name = "test:sound", .position = mod::Vec3 { 1, 2, 3 }, .loop = true });
    check(particle && sound && particle != sound, "distinct particle and sound handles");
    check(particles.active(particle) && audio.playing(sound), "query live effects");
    check(particles.move(particle, { 4, 5, 6 }) && audio.setPosition(sound, { 7, 8, 9 }) && audio.setVolume(sound, 0.25f), "update live effects");
    check(!audio.stop(particle) && !particles.remove(sound), "handle kinds cannot be confused");
    bus.owner = 2;
    check(!particles.active(particle) && !audio.stop(sound) && !audio.setVolume(sound, 1), "other owner cannot access effects");
    auto second = audio.play({ .name = "test:sound" });
    audio.stopAll();
    check(!audio.playing(second), "stopAll removes own sounds");
    bus.owner = 1;
    check(audio.playing(sound) && particles.active(particle), "stopAll leaves other owner untouched");
    particles.clear();
    check(!particles.active(particle) && audio.playing(sound), "particle clear leaves sounds untouched");
    size_t before = creates;
    float nan = std::numeric_limits<float>::quiet_NaN();
    check(!audio.setVolume(sound, nan) && !audio.setVolume(sound, -1), "reject invalid volume updates");
    check(audio.play({ .name = "test:sound", .volume = nan }) == 0, "reject invalid sound volume");
    check(audio.play({ .name = "test:sound", .pitch = 0 }) == 0, "reject invalid pitch");
    check(particles.spawn({ .identifier = "test:particle", .position = { nan, 0, 0 } }) == 0, "reject invalid particle position");
    check(!particles.move(particle, { nan, 0, 0 }) && creates == before, "invalid requests do not reach creation backend");
    active.clear();
    check(!audio.playing(sound), "finished sound expires");
    auto replacement = audio.play({ .name = "test:sound" });
    check(replacement != sound && !audio.stop(sound), "stale handle cannot stop replacement");
    effects.release(1);
    check(active.empty() && !audio.playing(replacement), "unloading releases owner effects");
    for (int i = 0; i < 128; ++i) check(audio.play({ .name = "test:sound" }) != 0, "owner capacity available");
    check(audio.play({ .name = "test:sound" }) == 0, "owner capacity enforced");
    active.clear();
    check(audio.play({ .name = "test:sound" }) != 0, "finished effects reclaim capacity");
    effects.clear();
    check(active.empty(), "world reset clears all effects");
    std::puts("PASS local effects ownership, lifetime, validation and API 3 fallback");
}
