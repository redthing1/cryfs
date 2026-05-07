#include <gtest/gtest.h>

#include <array>
#include <stdexcept>

#include <blobstore/implementations/onblocks/BlobStoreOnBlocks.h>
#include <blockstore/implementations/testfake/FakeBlockStore.h>

namespace {

class RecordingBlockStore final: public blockstore::BlockStore {
public:
  blockstore::BlockId createBlockId() override {
    return _base.createBlockId();
  }

  boost::optional<cpputils::unique_ref<blockstore::Block>> tryCreate(const blockstore::BlockId &blockId, cpputils::Data data) override {
    return _base.tryCreate(blockId, std::move(data));
  }

  boost::optional<cpputils::unique_ref<blockstore::Block>> load(const blockstore::BlockId &blockId) override {
    return _base.load(blockId);
  }

  cpputils::unique_ref<blockstore::Block> overwrite(const blockstore::BlockId &blockId, cpputils::Data data) override {
    return _base.overwrite(blockId, std::move(data));
  }

  void remove(const blockstore::BlockId &blockId) override {
    return _base.remove(blockId);
  }

  uint64_t numBlocks() const override {
    return _base.numBlocks();
  }

  uint64_t estimateNumFreeBytes() const override {
    return _base.estimateNumFreeBytes();
  }

  uint64_t blockSizeFromPhysicalBlockSize(uint64_t blockSize) const override {
    return _base.blockSizeFromPhysicalBlockSize(blockSize);
  }

  void forEachBlock(std::function<void (const blockstore::BlockId &)> callback) const override {
    return _base.forEachBlock(std::move(callback));
  }

  void flush() override {
    ++_flushCount;
    if (_failFlush) {
      throw std::runtime_error("injected flush failure");
    }
    return _base.flush();
  }

  void sync() override {
    ++_syncCount;
    if (_failSync) {
      throw std::runtime_error("injected sync failure");
    }
    return _base.sync();
  }

  void resetCounts() {
    _flushCount = 0;
    _syncCount = 0;
  }

  uint32_t flushCount() const {
    return _flushCount;
  }

  uint32_t syncCount() const {
    return _syncCount;
  }

  void failFlushes() {
    _failFlush = true;
  }

  void failSyncs() {
    _failSync = true;
  }

private:
  blockstore::testfake::FakeBlockStore _base;
  uint32_t _flushCount = 0;
  uint32_t _syncCount = 0;
  bool _failFlush = false;
  bool _failSync = false;
};

TEST(BlobStoreOnBlocksSyncTest, FlushAndSyncPropagateToUnderlyingBlockStore) {
  auto blockStore = cpputils::make_unique_ref<RecordingBlockStore>();
  RecordingBlockStore *recordingBlockStore = blockStore.get();
  blobstore::onblocks::BlobStoreOnBlocks blobStore(std::move(blockStore), 4096);

  auto blob = blobStore.create();
  const std::array<uint8_t, 3> data{{1, 2, 3}};
  blob->write(data.data(), 0, data.size());
  blob->flush();

  recordingBlockStore->resetCounts();
  blobStore.flush();
  EXPECT_EQ(1u, recordingBlockStore->flushCount());
  EXPECT_EQ(0u, recordingBlockStore->syncCount());

  recordingBlockStore->resetCounts();
  blobStore.sync();
  EXPECT_EQ(1u, recordingBlockStore->flushCount());
  EXPECT_EQ(1u, recordingBlockStore->syncCount());
}

TEST(BlobStoreOnBlocksSyncTest, FlushPropagatesUnderlyingFlushFailure) {
  auto blockStore = cpputils::make_unique_ref<RecordingBlockStore>();
  RecordingBlockStore *recordingBlockStore = blockStore.get();
  blobstore::onblocks::BlobStoreOnBlocks blobStore(std::move(blockStore), 4096);

  recordingBlockStore->failFlushes();

  EXPECT_THROW(blobStore.flush(), std::runtime_error);
  EXPECT_EQ(1u, recordingBlockStore->flushCount());
  EXPECT_EQ(0u, recordingBlockStore->syncCount());
}

TEST(BlobStoreOnBlocksSyncTest, SyncPropagatesUnderlyingFlushFailure) {
  auto blockStore = cpputils::make_unique_ref<RecordingBlockStore>();
  RecordingBlockStore *recordingBlockStore = blockStore.get();
  blobstore::onblocks::BlobStoreOnBlocks blobStore(std::move(blockStore), 4096);

  recordingBlockStore->failFlushes();

  EXPECT_THROW(blobStore.sync(), std::runtime_error);
  EXPECT_EQ(1u, recordingBlockStore->flushCount());
  EXPECT_EQ(0u, recordingBlockStore->syncCount());
}

TEST(BlobStoreOnBlocksSyncTest, SyncPropagatesUnderlyingSyncFailure) {
  auto blockStore = cpputils::make_unique_ref<RecordingBlockStore>();
  RecordingBlockStore *recordingBlockStore = blockStore.get();
  blobstore::onblocks::BlobStoreOnBlocks blobStore(std::move(blockStore), 4096);

  recordingBlockStore->failSyncs();

  EXPECT_THROW(blobStore.sync(), std::runtime_error);
  EXPECT_EQ(1u, recordingBlockStore->flushCount());
  EXPECT_EQ(1u, recordingBlockStore->syncCount());
}

}
