#include "world/WorldStream.h"
#include "Protocol/Packets/LevelChunkPacket.h"
#include "Protocol/Packets/NetworkChunkPublisherUpdatePacket.h"
#include "Protocol/Packets/SubChunkPacket.h"
#include "Protocol/Packets/SubChunkRequestPacket.h"
#include "Protocol/Packets/UpdateBlockPacket.h"
#include "Protocol/Packets/UpdateSubChunkBlocksPacket.h"
#include "Protocol/Packets/BlockActorDataPacket.h"
#include "Core/NBT/NbtIo.h"

#include <cstdio>
#include <cstdlib>
#include <future>

namespace kestrel {
void debugLog(const std::string&) {}
}

namespace {

using namespace kestrel::world;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

template<class Condition, class Poll>
void waitUntil(Condition condition, Poll poll)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!condition()) {
        require(std::chrono::steady_clock::now() < deadline, "Timed out waiting for decode worker");
        poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void orderedWorker()
{
    ChunkDecodeQueue queue;
    std::promise<void> started, release;
    auto gate = release.get_future().share();
    auto entered = started.get_future();
    const auto owner = std::this_thread::get_id();
    std::vector<int> applied;
    queue.submit(100, [&] {
        require(std::this_thread::get_id() != owner, "Decode must run off the calling thread");
        started.set_value();
        gate.wait();
        return [&] {
            require(std::this_thread::get_id() == owner, "Application must stay on the calling thread");
            applied.push_back(1);
        };
    });
    require(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Worker did not start");
    queue.append(100, [&] { applied.push_back(2); });
    queue.submit(100, [&] { return [&] { applied.push_back(3); }; });
    queue.drain();
    require(applied.empty(), "Ready updates must not overtake a decoding chunk");
    release.set_value();
    waitUntil([&] { return queue.empty(); }, [&] { queue.drain(); });
    require(applied == std::vector<int>({ 1, 2, 3 }), "Application order differs from receive order");
}

void cancellationAndErrors()
{
    ChunkDecodeQueue queue;
    std::promise<void> started, release;
    auto gate = release.get_future().share();
    auto entered = started.get_future();
    bool staleApplied = false, freshApplied = false;
    queue.submit(100, [&] {
        started.set_value();
        gate.wait();
        return [&] { staleApplied = true; };
    });
    require(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Worker did not start");
    queue.append(100, [&] { staleApplied = true; });
    queue.clear();
    queue.append(100, [&] { freshApplied = true; });
    queue.drain();
    require(freshApplied && !staleApplied, "Reset must discard both decoding chunks and deferred updates");
    release.set_value();
    queue.submit(100, []() -> ChunkDecodeQueue::Apply { throw std::runtime_error("decode failure"); });
    bool caught = false;
    waitUntil([&] { return caught; }, [&] {
        try { queue.drain(); }
        catch (const std::runtime_error&) { caught = true; }
    });
    require(!staleApplied, "An old in-flight result survived reset");
    try {
        queue.append(65 * 1024 * 1024, [] {});
        require(false, "Oversized queue reservation accepted");
    } catch (const std::runtime_error&) {}
    for (int i = 0; i < 512; ++i) queue.append(0, [] {});
    require(queue.backlogged(), "Item backlog must trigger receive backpressure");
    queue.clear();
    require(!queue.backlogged() && queue.empty(), "Reset must release queue capacity");
}

std::shared_ptr<LevelChunkPacket> column(int32_t x = 0)
{
    auto packet = std::make_shared<LevelChunkPacket>();
    packet->mDimension = 0;
    packet->mChunkX = x;
    packet->mChunkZ = 0;
    packet->mRequestSubChunks = false;
    packet->mSubChunkLimit = 0;
    packet->mSubChunksLength = 1;
    packet->mData = std::string("\x08\x01\x01\x54", 4);
    return packet;
}

std::string persistentStorage()
{
    Tag state = Tag::ofCompound();
    state.putString("name", "minecraft:stone");
    state.put("states", Tag::ofCompound());
    BinaryStream stream;
    NbtIo::writeTag(stream, state, NbtVariant::Network);
    return std::string("\x08\x01\x00", 3) + stream.getBuffer();
}

UpdateBlockPacket update()
{
    UpdateBlockPacket packet;
    packet.mBlockPosition = Vector3i(0, -64, 0);
    packet.mDataLayer = 0;
    packet.mRuntimeId = 99;
    return packet;
}

void drain(WorldStream& stream, uint64_t expected)
{
    waitUntil([&] { return stream.applyDecoded(); }, [] {});
    require(stream.stats().levelChunks + stream.stats().subChunkReplies == expected, "Receive counters changed");
}

void streamedWorldOrder()
{
    WorldStream stream;
    stream.reset(0, 0, 0);
    stream.handle(column());
    stream.handle(update());
    auto blockActor = std::make_shared<BlockActorDataPacket>();
    blockActor->mBlockPosition = Vector3i(0, -64, 0);
    blockActor->mData = Tag::ofCompound();
    blockActor->mData.putInt("test", 123);
    stream.handle(blockActor, 100);
    require(!stream.store().isLoaded({ 0, 0, 0 }), "Receiving a chunk must not synchronously apply it");
    drain(stream, 1);
    auto chunk = stream.store().subChunk({ 0, 0, -4, 0 });
    require(chunk && chunk->runtimeId(0, 0, 0, 0) == 99, "Late decode overwrote a following block update");
    auto entities = stream.store().blockEntities({ 0, 0, -4, 0 });
    require(entities && entities->at(0).getInt("test") == 123, "Late decode lost a following block entity update");

    stream.handle(column(30));
    NetworkChunkPublisherUpdatePacket publisher;
    publisher.mPosition = Vector3i(0, 0, 0);
    publisher.mRadius = 16;
    stream.handle(publisher);
    drain(stream, 2);
    require(!stream.store().isLoaded({ 0, 30, 0 }), "Late decode resurrected a publisher-evicted column");

    stream.handle(column(2));
    stream.changeDimension(1, 0, 0);
    stream.changeDimension(0, 0, 0);
    auto invalid = column();
    invalid->mData = std::string("\x66", 1);
    stream.handle(invalid);
    drain(stream, 4);
    require(stream.stats().decodeErrors == 1, "Malformed chunk must keep its decode error");
    require(!stream.store().isLoaded({ 0, 2, 0 }), "Old dimension decode survived a round trip");
}

void requestedSubChunks()
{
    WorldStream stream;
    stream.reset(0, 0, 0);
    auto announcement = column();
    announcement->mRequestSubChunks = true;
    announcement->mSubChunkLimit = 1;
    announcement->mData.clear();
    stream.handle(announcement);
    auto reply = std::make_shared<SubChunkPacket>();
    reply->mDimension = 0;
    reply->mCenterPosition = Vector3i(0, -4, 0);
    SubChunkData entry;
    entry.mPosition = Vector3i(0, 0, 0);
    entry.mResult = SubChunkRequestResult::Success;
    entry.mHasData = true;
    entry.mData = column()->mData;
    reply->mSubChunks.push_back(entry);
    stream.handle(reply);
    stream.handle(update());
    auto changes = std::make_shared<UpdateSubChunkBlocksPacket>();
    BlockChangeEntry change;
    change.mPosition = Vector3i(0, -64, 0);
    change.mRuntimeId = 123;
    changes->mStandardBlocks.push_back(change);
    stream.handle(changes);
    drain(stream, 2);
    auto chunk = stream.store().subChunk({ 0, 0, -4, 0 });
    require(chunk && chunk->runtimeId(0, 0, 0, 0) == 123, "Sub-chunk application lost its following updates");
    require(stream.stats().pendingSubChunks == 0, "Decoded reply did not clear the pending request");
}

void lateColumnAfterPublisher()
{
    WorldStream stream;
    stream.reset(0, 0, 0);
    NetworkChunkPublisherUpdatePacket publisher;
    publisher.mPosition = Vector3i(0, 0, 0);
    publisher.mRadius = 16;
    stream.handle(publisher);
    stream.handle(column(30));
    drain(stream, 1);
    require(!stream.store().isLoaded({ 0, 30, 0 }), "Late out-of-view metadata must not recreate an evicted column");
    stream.handle(column());
    drain(stream, 2);
    require(stream.store().isLoaded({ 0, 0, 0 }), "Current publisher terrain must still load");
    publisher.mPosition = Vector3i(480, 0, 0);
    stream.handle(publisher);
    stream.handle(column());
    stream.handle(column(30));
    drain(stream, 4);
    require(stream.store().isLoaded({ 0, 30, 0 }) && !stream.store().isLoaded({ 0, 0, 0 }),
        "Publisher movement must keep current columns without resurrecting old terrain");
}

void resolverSnapshot()
{
    WorldStream stream;
    stream.reset(0, 0, 0);
    std::promise<void> started, release;
    auto gate = release.get_future().share();
    auto entered = started.get_future();
    const auto owner = std::this_thread::get_id();
    stream.setBlockPaletteResolver([&](const Tag&) -> std::optional<uint32_t> {
        require(std::this_thread::get_id() != owner, "Persistent palette decoding ran on the receiving thread");
        started.set_value();
        gate.wait();
        return 7;
    });
    auto first = column();
    first->mData = persistentStorage();
    stream.handle(first);
    require(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Palette resolver did not run");
    stream.setBlockPaletteResolver([](const Tag&) -> std::optional<uint32_t> { return 8; });
    auto second = column(1);
    second->mData = first->mData;
    stream.handle(second);
    release.set_value();
    drain(stream, 2);
    require(stream.store().subChunk({ 0, 0, -4, 0 })->runtimeId(0, 0, 0, 0) == 7,
        "Reload changed the resolver of an in-flight chunk");
    require(stream.store().subChunk({ 0, 1, -4, 0 })->runtimeId(0, 0, 0, 0) == 8,
        "New chunk did not capture the reloaded resolver");
}

void queuedReplyTimeout()
{
    WorldStream stream;
    stream.reset(0, 0, 0);
    auto announcement = column();
    announcement->mRequestSubChunks = true;
    announcement->mSubChunkLimit = 1;
    announcement->mData.clear();
    stream.handle(announcement);
    auto other = std::make_shared<LevelChunkPacket>(*announcement);
    other->mChunkX = 1;
    stream.handle(other);
    drain(stream, 2);
    auto now = WorldStream::Clock::now();
    require(stream.takeRequests(now).size() == 2, "Initial requests must be sent");
    std::promise<void> started, release;
    auto gate = release.get_future().share();
    auto entered = started.get_future();
    stream.setBlockPaletteResolver([&](const Tag&) -> std::optional<uint32_t> {
        started.set_value();
        gate.wait();
        return 7;
    });
    auto reply = std::make_shared<SubChunkPacket>();
    reply->mDimension = 0;
    reply->mCenterPosition = Vector3i(0, -4, 0);
    SubChunkData entry;
    entry.mPosition = Vector3i(0, 0, 0);
    entry.mResult = SubChunkRequestResult::Success;
    entry.mHasData = true;
    entry.mData = persistentStorage();
    reply->mSubChunks.push_back(entry);
    stream.handle(reply);
    require(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Reply did not start decoding");
    auto retries = stream.takeRequests(now + std::chrono::seconds(3));
    require(retries.size() == 1 && retries.front()->mSubChunkPosition.x == 1,
        "Only the unrelated missing reply should be retried");
    require(stream.stats().retries == 1, "Queued reply incorrectly triggered a retry");
    release.set_value();
    drain(stream, 3);
    require(!stream.subChunkPending({ 0, 0, -4, 0 }) && stream.stats().pendingSubChunks == 1,
        "Applied reply did not clear only its own request");
}

void boundedRequests()
{
    WorldStream stream;
    stream.reset(0, 40, 0);
    for (int x = 0; x < 64; ++x) {
        auto announcement = column(x);
        announcement->mRequestSubChunks = true;
        announcement->mSubChunkLimit = 1;
        announcement->mData.clear();
        stream.handle(announcement);
    }
    drain(stream, 64);
    auto now = WorldStream::Clock::now();
    MeshViewPriority priority({ 648, 8, 8 }, { 1, 0, 0 }, true);
    auto initial = stream.takeRequests(now, priority);
    require(initial.size() == 32 && initial.front()->mSubChunkPosition.x == 40,
        "Request window must be bounded and start at the player rather than map order");
    require(stream.takeRequests(now + std::chrono::milliseconds(100), priority).empty(),
        "A full request window must not admit more columns");
    auto reply = std::make_shared<SubChunkPacket>();
    reply->mDimension = 0;
    reply->mCenterPosition = initial.front()->mSubChunkPosition;
    SubChunkData entry;
    entry.mPosition = Vector3i(0, 0, 0);
    entry.mResult = SubChunkRequestResult::SuccessAllAir;
    reply->mSubChunks.push_back(entry);
    stream.handle(reply);
    waitUntil([&] { return !stream.subChunkPending({ 0, 40, -4, 0 }); }, [&] { stream.applyDecoded(); });
    require(stream.takeRequests(now + std::chrono::milliseconds(200), priority).size() == 1,
        "An applied reply must immediately free one request slot");
    require(stream.takeRequests(now + std::chrono::seconds(3), priority).size() == 32,
        "Missing replies must still retry within the bounded window");
}

void backpressureTimeout()
{
    WorldStream stream;
    stream.reset(0, 0, 0);
    auto announcement = column();
    announcement->mRequestSubChunks = true;
    announcement->mSubChunkLimit = 1;
    announcement->mData.clear();
    stream.handle(announcement);
    drain(stream, 1);
    auto now = WorldStream::Clock::now();
    require(stream.takeRequests(now).size() == 1, "Initial request missing");
    std::promise<void> started, release;
    auto gate = release.get_future().share();
    auto entered = started.get_future();
    stream.setBlockPaletteResolver([&](const Tag&) -> std::optional<uint32_t> {
        started.set_value();
        gate.wait();
        return 7;
    });
    auto blocker = column(1);
    blocker->mData = persistentStorage();
    stream.handle(blocker);
    require(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Decode did not block");
    for (int i = 0; i < 512; ++i) stream.handle(update());
    require(stream.decodeBacklogged(), "Expected decode backpressure");
    require(stream.takeRequests(now + std::chrono::seconds(1)).empty(), "Backpressure must pause requests");
    require(stream.takeRequests(now + std::chrono::seconds(5)).empty(), "Backpressure must pause retries");
    release.set_value();
    waitUntil([&] { return stream.applyDecoded(); }, [] {});
    require(stream.takeRequests(now + std::chrono::seconds(6)).empty(), "Receive pause must extend outstanding deadlines");
    require(stream.takeRequests(now + std::chrono::seconds(8)).size() == 1,
        "A genuinely missing reply must retry after the adjusted deadline");
}

}

int main()
{
    orderedWorker();
    cancellationAndErrors();
    streamedWorldOrder();
    requestedSubChunks();
    lateColumnAfterPublisher();
    resolverSnapshot();
    queuedReplyTimeout();
    boundedRequests();
    backpressureTimeout();
}
