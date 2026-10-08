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

struct Owner {
    modding::LocalEffects& effects;
    size_t owner = 1;

    bool request(Request::Kind kind, Request::Action action, uint64_t handle = 0, mod::Vec3 position = {}, float volume = 1.0f) const
    {
        Request request;
        request.kind = kind;
        request.action = action;
        request.handle = handle;
        request.position = position;
        request.volume = volume;
        effects.process(owner, request);
        return request.result;
    }

    uint64_t create(Request& request) const
    {
        request.action = Request::Action::Create;
        effects.process(owner, request);
        return request.result ? request.handle : 0;
    }
};

class Particles final : public mod::Particles {
public:
    explicit Particles(Owner& host) : host(host) { }

    bool supported() const override
    {
        return host.request(Request::Kind::Particle, Request::Action::Supported);
    }

    mod::ParticleHandle spawn(mod::ParticleOptions options) override
    {
        Request request;
        request.kind = Request::Kind::Particle;
        request.particle = std::move(options);
        return host.create(request);
    }

    bool active(mod::ParticleHandle handle) const override
    {
        return host.request(Request::Kind::Particle, Request::Action::Active, handle);
    }

    bool move(mod::ParticleHandle handle, mod::Vec3 position) override
    {
        return host.request(Request::Kind::Particle, Request::Action::Move, handle, position);
    }

    bool remove(mod::ParticleHandle handle) override
    {
        return host.request(Request::Kind::Particle, Request::Action::Remove, handle);
    }

    void clear() override
    {
        host.request(Request::Kind::Particle, Request::Action::Clear);
    }

private:
    Owner& host;
};

class Audio final : public mod::Audio {
public:
    explicit Audio(Owner& host) : host(host) { }

    bool supported() const override
    {
        return host.request(Request::Kind::Sound, Request::Action::Supported);
    }

    mod::SoundHandle play(mod::SoundOptions options) override
    {
        Request request;
        request.kind = Request::Kind::Sound;
        request.sound = std::move(options);
        return host.create(request);
    }

    bool playing(mod::SoundHandle handle) const override
    {
        return host.request(Request::Kind::Sound, Request::Action::Active, handle);
    }

    bool stop(mod::SoundHandle handle) override
    {
        return host.request(Request::Kind::Sound, Request::Action::Remove, handle);
    }

    bool setVolume(mod::SoundHandle handle, float volume) override
    {
        return host.request(Request::Kind::Sound, Request::Action::Volume, handle, {}, volume);
    }

    bool setPosition(mod::SoundHandle handle, mod::Vec3 position) override
    {
        return host.request(Request::Kind::Sound, Request::Action::Move, handle, position);
    }

    void stopAll() override
    {
        host.request(Request::Kind::Sound, Request::Action::Clear);
    }

private:
    Owner& host;
};

}

int main()
{
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
    Owner bus { effects };
    Particles particles(bus);
    Audio audio(bus);
    check(particles.supported() && audio.supported(), "effects supported");
    mod::detail::EffectRequest legacy;
    legacy.kind = Request::Kind::Sound;
    legacy.action = Request::Action::Create;
    legacy.sound.name = "test:sound";
    mod::Event& posted = legacy;
    check(posted.type() == "kestrel:local_effect_request/v1", "legacy request keeps its event type");
    effects.process(1, static_cast<Request&>(posted));
    check(legacy.result && legacy.handle && audio.playing(legacy.handle) && audio.stop(legacy.handle), "legacy request shares the API 4 store");
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
    std::puts("PASS local effects ownership, lifetime, validation");
}
