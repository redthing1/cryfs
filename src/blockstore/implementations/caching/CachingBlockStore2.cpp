#include "CachingBlockStore2.h"
#include <utility>

using cpputils::Data;
using cpputils::unique_ref;
using cpputils::make_unique_ref;
using boost::optional;

namespace blockstore {
namespace caching {

constexpr double CachingBlockStore2::MAX_LIFETIME_SEC;

CachingBlockStore2::CachedBlock::CachedBlock(cpputils::Data data)
    : _data(std::move(data)) {
}

const Data& CachingBlockStore2::CachedBlock::read() const {
  return _data;
}

void CachingBlockStore2::CachedBlock::write(Data data) {
  _data = std::move(data);
}

CachingBlockStore2::CachingBlockStore2(cpputils::unique_ref<BlockStore2> baseBlockStore)
: _baseBlockStore(std::move(baseBlockStore)), _cache("blockstore") {
}

bool CachingBlockStore2::tryCreate(const BlockId &blockId, const Data &data) {
  auto popped = _cache.pop(blockId);
  if (popped != boost::none) {
    // entry already exists in cache
    _cache.push(blockId, std::move(*popped)); // push the just popped element back to the cache
    return false;
  } else {
    if (!_baseBlockStore->tryCreate(blockId, data)) {
      return false;
    }
    _cache.push(blockId, make_unique_ref<CachingBlockStore2::CachedBlock>(data.copy()));
    return true;
  }
}

bool CachingBlockStore2::remove(const BlockId &blockId) {
  _cache.pop(blockId);
  return _baseBlockStore->remove(blockId);
}

optional<unique_ref<CachingBlockStore2::CachedBlock>> CachingBlockStore2::_loadFromCacheOrBaseStore(const BlockId &blockId) const {
  auto popped = _cache.pop(blockId);
  if (popped != boost::none) {
    return std::move(*popped);
  } else {
    auto loaded = _baseBlockStore->load(blockId);
    if (loaded == boost::none) {
      return boost::none;
    }
    return make_unique_ref<CachingBlockStore2::CachedBlock>(std::move(*loaded));
  }
}

optional<Data> CachingBlockStore2::load(const BlockId &blockId) const {
  auto loaded = _loadFromCacheOrBaseStore(blockId);
  if (loaded == boost::none) {
    // TODO Cache non-existence?
    return boost::none;
  }
  optional<Data> result = (*loaded)->read().copy();
  _cache.push(blockId, std::move(*loaded));
  return result;
}

void CachingBlockStore2::store(const BlockId &blockId, const Data &data) {
  _baseBlockStore->store(blockId, data);
  auto popped = _cache.pop(blockId);
  if (popped != boost::none) {
    (*popped)->write(data.copy());
  } else {
    popped = make_unique_ref<CachingBlockStore2::CachedBlock>(data.copy());
  }
  _cache.push(blockId, std::move(*popped));
}

uint64_t CachingBlockStore2::numBlocks() const {
  return _baseBlockStore->numBlocks();
}

uint64_t CachingBlockStore2::estimateNumFreeBytes() const {
  return _baseBlockStore->estimateNumFreeBytes();
}

uint64_t CachingBlockStore2::blockSizeFromPhysicalBlockSize(uint64_t blockSize) const {
  return _baseBlockStore->blockSizeFromPhysicalBlockSize(blockSize);
}

void CachingBlockStore2::forEachBlock(std::function<void (const BlockId &)> callback) const {
  _baseBlockStore->forEachBlock(std::move(callback));
}

void CachingBlockStore2::flush() {
  _baseBlockStore->flush();
}

void CachingBlockStore2::sync() {
  flush();
  _baseBlockStore->sync();
}

}
}
