#include "world/ChunkStore.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace kestrel::world;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

/**
 * The session republishes the world mods read only when the store's revision
 * moved, so every change a mod must see has to move it, the sub-chunk handed
 * out before must keep its old blocks, and a change that changes nothing must
 * leave the revision alone.
 */
void blockChangeIsVisibleAtOnce()
{
    ChunkStore store;
    const SubChunkKey key { 0, 0, 4, 0 };
    store.markLoaded(key.chunk());
    SubChunk section;
    require(section.apply({ BlockUpdate { 1, 2, 3, 0, 7 } }), "Could not create the fixture");
    store.commit(key, std::move(section));

    uint64_t published = store.revision();
    std::shared_ptr<const SubChunk> before = store.subChunk(key);
    require(before && before->runtimeId(0, 1, 2, 3) == 7, "The committed block must read back");

    require(store.updateBlocks(key, { BlockUpdate { 1, 2, 3, 0, 9 } }), "The server's block update must apply");
    require(store.revision() != published, "A block update must move the revision the same tick");
    std::shared_ptr<const SubChunk> after = store.subChunk(key);
    require(after && after != before, "A block update must share a new sub-chunk");
    require(after->runtimeId(0, 1, 2, 3) == 9, "The new sub-chunk must hold the new block");
    require(before->runtimeId(0, 1, 2, 3) == 7, "The sub-chunk already shared must keep its blocks");

    published = store.revision();
    require(!store.updateBlocks(key, { BlockUpdate { 1, 2, 3, 0, 9 } }), "An update to the same block must change nothing");
    require(store.revision() == published, "An update that changes nothing must keep the published world");

    require(store.updateBlocks(key, { BlockUpdate { 1, 2, 3, 0, ImplicitAir }, BlockUpdate { 1, 2, 3, 1, ImplicitAir } }), "A predicted break must apply");
    require(store.revision() != published, "A predicted break must move the revision");
}

void columnsMoveTheRevision()
{
    ChunkStore store;
    uint64_t published = store.revision();
    store.markLoaded({ 0, 2, 3 });
    require(store.revision() != published, "A column arriving must move the revision");
    published = store.revision();
    store.markLoaded({ 0, 2, 3 });
    require(store.revision() == published, "A column already loaded must keep the revision");
    store.evict(ChunkKey { 0, 2, 3 });
    require(store.revision() != published, "A column leaving must move the revision");
}

}

int main()
{
    blockChangeIsVisibleAtOnce();
    columnsMoveTheRevision();
}
