#pragma once
#ifndef MESSMER_BLOCKSTORE_IMPLEMENTATIONS_CACHING_CACHINGBLOCKSTORE2_H_
#define MESSMER_BLOCKSTORE_IMPLEMENTATIONS_CACHING_CACHINGBLOCKSTORE2_H_

#include "../../interface/BlockStore2.h"
#include <cpp-utils/macros.h>
#include "../caching/cache/Cache.h"

namespace blockstore {
namespace caching {

class CachingBlockStore2 final: public BlockStore2 {
public:
  CachingBlockStore2(cpputils::unique_ref<BlockStore2> baseBlockStore);

  bool tryCreate(const BlockId &blockId, const cpputils::Data &data) override;
  bool remove(const BlockId &blockId) override;
  boost::optional<cpputils::Data> load(const BlockId &blockId) const override;
  void store(const BlockId &blockId, const cpputils::Data &data) override;
  uint64_t numBlocks() const override;
  uint64_t estimateNumFreeBytes() const override;
  uint64_t blockSizeFromPhysicalBlockSize(uint64_t blockSize) const override;
  void forEachBlock(std::function<void (const BlockId &)> callback) const override;
  void flush() override;
  void sync() override;

private:
  class CachedBlock final {
  public:
    explicit CachedBlock(cpputils::Data data);

    const cpputils::Data& read() const;
    void write(cpputils::Data data);
  private:
    cpputils::Data _data;

    DISALLOW_COPY_AND_ASSIGN(CachedBlock);
  };

  boost::optional<cpputils::unique_ref<CachedBlock>> _loadFromCacheOrBaseStore(const BlockId &blockId) const;

  cpputils::unique_ref<BlockStore2> _baseBlockStore;

  // TODO Store CachedBlock directly, without unique_ref
  mutable Cache<BlockId, cpputils::unique_ref<CachedBlock>, 1000> _cache;

public:
  static constexpr double MAX_LIFETIME_SEC = decltype(_cache)::MAX_LIFETIME_SEC;

private:

  DISALLOW_COPY_AND_ASSIGN(CachingBlockStore2);
};

}
}

#endif
