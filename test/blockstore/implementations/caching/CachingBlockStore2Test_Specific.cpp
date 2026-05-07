#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "blockstore/implementations/caching/CachingBlockStore2.h"
#include "blockstore/implementations/inmemory/InMemoryBlockStore2.h"
#include <cpp-utils/data/DataFixture.h>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using ::testing::ElementsAre;
using ::testing::Test;

using cpputils::Data;
using cpputils::DataFixture;

using blockstore::BlockId;
using blockstore::BlockStore2;
using blockstore::inmemory::InMemoryBlockStore2;

using namespace blockstore::caching;

namespace {

class RecordingBlockStore2 final: public BlockStore2 {
public:
  bool tryCreate(const BlockId &blockId, const Data &data) override {
    events.push_back("tryCreate");
    if (failTryCreate) {
      throw std::runtime_error("injected tryCreate failure");
    }
    return base.tryCreate(blockId, data);
  }

  bool remove(const BlockId &blockId) override {
    events.push_back("remove");
    return base.remove(blockId);
  }

  boost::optional<Data> load(const BlockId &blockId) const override {
    events.push_back("load");
    return base.load(blockId);
  }

  void store(const BlockId &blockId, const Data &data) override {
    events.push_back("store");
    if (failStore) {
      throw std::runtime_error("injected store failure");
    }
    base.store(blockId, data);
  }

  uint64_t numBlocks() const override {
    return base.numBlocks();
  }

  uint64_t estimateNumFreeBytes() const override {
    return base.estimateNumFreeBytes();
  }

  uint64_t blockSizeFromPhysicalBlockSize(uint64_t blockSize) const override {
    return base.blockSizeFromPhysicalBlockSize(blockSize);
  }

  void forEachBlock(std::function<void (const BlockId &)> callback) const override {
    return base.forEachBlock(std::move(callback));
  }

  void flush() override {
    events.push_back("flush");
    if (failFlush) {
      throw std::runtime_error("injected flush failure");
    }
    base.flush();
  }

  void sync() override {
    events.push_back("sync");
    if (failSync) {
      throw std::runtime_error("injected sync failure");
    }
    base.sync();
  }

  boost::optional<Data> stored(const BlockId &blockId) const {
    return base.load(blockId);
  }

  mutable std::vector<std::string> events;
  bool failTryCreate = false;
  bool failStore = false;
  bool failFlush = false;
  bool failSync = false;

private:
  InMemoryBlockStore2 base;
};

}

class CachingBlockStore2Test: public Test {
public:
  CachingBlockStore2Test():
      baseBlockStore(new InMemoryBlockStore2),
      blockStore(std::move(cpputils::nullcheck(std::unique_ptr<InMemoryBlockStore2>(baseBlockStore)).value()))  {
  }
    InMemoryBlockStore2 *baseBlockStore;
  CachingBlockStore2 blockStore;
};

TEST_F(CachingBlockStore2Test, PhysicalBlockSize_zerophysical) {
  EXPECT_EQ(0u, blockStore.blockSizeFromPhysicalBlockSize(0));
}

TEST_F(CachingBlockStore2Test, PhysicalBlockSize_zerovirtual) {
  auto blockId = blockStore.create(Data(0));
  blockStore.flush();
  auto base = baseBlockStore->load(blockId).value();
  EXPECT_EQ(0u, blockStore.blockSizeFromPhysicalBlockSize(base.size()));
}

TEST_F(CachingBlockStore2Test, PhysicalBlockSize_negativeboundaries) {
  // This tests that a potential if/else in blockSizeFromPhysicalBlockSize that catches negative values has the
  // correct boundary set. We test the highest value that is negative and the smallest value that is positive.
  auto blockId = blockStore.create(Data(0));
  blockStore.flush();
  auto physicalSizeForVirtualSizeZero = baseBlockStore->load(blockId).value().size();
  if (physicalSizeForVirtualSizeZero > 0) {
    EXPECT_EQ(0u, blockStore.blockSizeFromPhysicalBlockSize(physicalSizeForVirtualSizeZero - 1));
  }
  EXPECT_EQ(0u, blockStore.blockSizeFromPhysicalBlockSize(physicalSizeForVirtualSizeZero));
  EXPECT_EQ(1u, blockStore.blockSizeFromPhysicalBlockSize(physicalSizeForVirtualSizeZero + 1));
}

TEST_F(CachingBlockStore2Test, PhysicalBlockSize_positive) {
  auto blockId = blockStore.create(Data(10*1024u));
  blockStore.flush();
  auto base = baseBlockStore->load(blockId).value();
  EXPECT_EQ(10*1024u, blockStore.blockSizeFromPhysicalBlockSize(base.size()));
}

TEST(CachingBlockStore2SyncTest, TryCreateWritesThroughImmediately) {
  auto *baseBlockStore = new RecordingBlockStore2;
  CachingBlockStore2 blockStore(std::move(cpputils::nullcheck(std::unique_ptr<RecordingBlockStore2>(baseBlockStore)).value()));
  const BlockId blockId = BlockId::FromString("1491BB4932A389EE14BC7090AC772972");
  const Data data = DataFixture::generate(1024);

  ASSERT_TRUE(blockStore.tryCreate(blockId, data));

  EXPECT_EQ(data, baseBlockStore->stored(blockId).value());
  EXPECT_THAT(baseBlockStore->events, ElementsAre("tryCreate"));
}

TEST(CachingBlockStore2SyncTest, StoreWritesThroughImmediately) {
  auto *baseBlockStore = new RecordingBlockStore2;
  CachingBlockStore2 blockStore(std::move(cpputils::nullcheck(std::unique_ptr<RecordingBlockStore2>(baseBlockStore)).value()));
  const BlockId blockId = BlockId::FromString("1491BB4932A389EE14BC7090AC772972");
  const Data data = DataFixture::generate(1024);
  const Data updatedData = DataFixture::generate(2048);

  ASSERT_TRUE(blockStore.tryCreate(blockId, data));
  baseBlockStore->events.clear();

  blockStore.store(blockId, updatedData);

  EXPECT_EQ(updatedData, baseBlockStore->stored(blockId).value());
  EXPECT_THAT(baseBlockStore->events, ElementsAre("store"));
}

TEST(CachingBlockStore2SyncTest, FlushFlushesBaseStore) {
  auto *baseBlockStore = new RecordingBlockStore2;
  CachingBlockStore2 blockStore(std::move(cpputils::nullcheck(std::unique_ptr<RecordingBlockStore2>(baseBlockStore)).value()));
  const BlockId blockId = BlockId::FromString("1491BB4932A389EE14BC7090AC772972");
  const Data data = DataFixture::generate(1024);

  ASSERT_TRUE(blockStore.tryCreate(blockId, data));
  baseBlockStore->events.clear();

  blockStore.flush();

  EXPECT_EQ(data, baseBlockStore->stored(blockId).value());
  EXPECT_THAT(baseBlockStore->events, ElementsAre("flush"));
}

TEST(CachingBlockStore2SyncTest, SyncFlushesThenSyncsBaseStore) {
  auto *baseBlockStore = new RecordingBlockStore2;
  CachingBlockStore2 blockStore(std::move(cpputils::nullcheck(std::unique_ptr<RecordingBlockStore2>(baseBlockStore)).value()));
  const BlockId blockId = BlockId::FromString("1491BB4932A389EE14BC7090AC772972");
  const Data data = DataFixture::generate(1024);

  ASSERT_TRUE(blockStore.tryCreate(blockId, data));
  baseBlockStore->events.clear();

  blockStore.sync();

  EXPECT_EQ(data, baseBlockStore->stored(blockId).value());
  EXPECT_THAT(baseBlockStore->events, ElementsAre("flush", "sync"));
}

TEST(CachingBlockStore2SyncTest, TryCreatePropagatesBaseFailure) {
  auto *baseBlockStore = new RecordingBlockStore2;
  CachingBlockStore2 blockStore(std::move(cpputils::nullcheck(std::unique_ptr<RecordingBlockStore2>(baseBlockStore)).value()));
  const BlockId blockId = BlockId::FromString("1491BB4932A389EE14BC7090AC772972");
  const Data data = DataFixture::generate(1024);

  baseBlockStore->failTryCreate = true;

  EXPECT_THROW(blockStore.tryCreate(blockId, data), std::runtime_error);
  EXPECT_FALSE(baseBlockStore->stored(blockId).is_initialized());
  EXPECT_THAT(baseBlockStore->events, ElementsAre("tryCreate"));
}

TEST(CachingBlockStore2SyncTest, StorePropagatesBaseFailureAndKeepsCachedDataUnchanged) {
  auto *baseBlockStore = new RecordingBlockStore2;
  CachingBlockStore2 blockStore(std::move(cpputils::nullcheck(std::unique_ptr<RecordingBlockStore2>(baseBlockStore)).value()));
  const BlockId blockId = BlockId::FromString("1491BB4932A389EE14BC7090AC772972");
  const Data data = DataFixture::generate(1024);
  const Data updatedData = DataFixture::generate(2048);

  ASSERT_TRUE(blockStore.tryCreate(blockId, data));
  ASSERT_EQ(data, blockStore.load(blockId).value());
  baseBlockStore->events.clear();
  baseBlockStore->failStore = true;

  EXPECT_THROW(blockStore.store(blockId, updatedData), std::runtime_error);
  EXPECT_EQ(data, baseBlockStore->stored(blockId).value());
  EXPECT_EQ(data, blockStore.load(blockId).value());
}

TEST(CachingBlockStore2SyncTest, FlushPropagatesBaseFlushFailure) {
  auto *baseBlockStore = new RecordingBlockStore2;
  CachingBlockStore2 blockStore(std::move(cpputils::nullcheck(std::unique_ptr<RecordingBlockStore2>(baseBlockStore)).value()));

  baseBlockStore->failFlush = true;

  EXPECT_THROW(blockStore.flush(), std::runtime_error);
  EXPECT_THAT(baseBlockStore->events, ElementsAre("flush"));
}

TEST(CachingBlockStore2SyncTest, SyncPropagatesBaseFlushFailureWithoutSyncing) {
  auto *baseBlockStore = new RecordingBlockStore2;
  CachingBlockStore2 blockStore(std::move(cpputils::nullcheck(std::unique_ptr<RecordingBlockStore2>(baseBlockStore)).value()));

  baseBlockStore->failFlush = true;

  EXPECT_THROW(blockStore.sync(), std::runtime_error);
  EXPECT_THAT(baseBlockStore->events, ElementsAre("flush"));
}

TEST(CachingBlockStore2SyncTest, SyncPropagatesBaseSyncFailure) {
  auto *baseBlockStore = new RecordingBlockStore2;
  CachingBlockStore2 blockStore(std::move(cpputils::nullcheck(std::unique_ptr<RecordingBlockStore2>(baseBlockStore)).value()));

  baseBlockStore->failSync = true;

  EXPECT_THROW(blockStore.sync(), std::runtime_error);
  EXPECT_THAT(baseBlockStore->events, ElementsAre("flush", "sync"));
}

// TODO Add test cases that flushing the block store doesn't destroy things (i.e. all test cases from BlockStoreTest, but with flushes inbetween)
