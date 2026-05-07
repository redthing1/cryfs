#include <gtest/gtest.h>
#include "blockstore/implementations/integrity/IntegrityBlockStore2.h"
#include "blockstore/implementations/integrity/KnownBlockVersions.h"
#include "blockstore/implementations/inmemory/InMemoryBlockStore2.h"
#include "blockstore/utils/BlockStoreUtils.h"
#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>
#include <cpp-utils/tempfile/TempFile.h>
#include <boost/filesystem.hpp>

#include "../../testutils/gtest_printers.h"
#include <cstddef>
#include <stdexcept>

using ::testing::Test;

using cpputils::DataFixture;
using cpputils::Data;
using cpputils::unique_ref;
using cpputils::make_unique_ref;
using cpputils::TempDir;
using cpputils::TempFile;
using cpputils::serialize;
using cpputils::deserialize;
using std::unique_ptr;

using blockstore::inmemory::InMemoryBlockStore2;

using namespace blockstore::integrity;

namespace {
constexpr uint32_t TEST_CLIENT_ID = 0x12345678;

class FakeCallback final {
public:
  FakeCallback(): wasCalled_(false) {}

  bool wasCalled() const {
    return wasCalled_;
  }

  std::function<void ()> callback() {
    return [this] () {
      wasCalled_ = true;
    };
  }

private:
  bool wasCalled_;
};

class FailableBlockStore final : public blockstore::BlockStore2 {
public:
  bool throwOnStore = false;
  bool throwOnRemove = false;

  bool tryCreate(const blockstore::BlockId &blockId, const Data &data) override {
    return base.tryCreate(blockId, data);
  }

  bool remove(const blockstore::BlockId &blockId) override {
    if (throwOnRemove) {
      throw std::runtime_error("injected remove failure");
    }
    return base.remove(blockId);
  }

  boost::optional<Data> load(const blockstore::BlockId &blockId) const override {
    return base.load(blockId);
  }

  void store(const blockstore::BlockId &blockId, const Data &data) override {
    if (throwOnStore) {
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

  void forEachBlock(std::function<void (const blockstore::BlockId &)> callback) const override {
    base.forEachBlock(std::move(callback));
  }

  void flush() override {
    base.flush();
  }

  void sync() override {
    base.sync();
  }

private:
  InMemoryBlockStore2 base;
};

unique_ref<IntegrityBlockStore2> makeIntegrityBlockStore(FailableBlockStore **baseBlockStore, const TempFile &stateFile) {
  *baseBlockStore = new FailableBlockStore;
  return cpputils::make_unique_ref<IntegrityBlockStore2>(
    std::move(cpputils::nullcheck(std::unique_ptr<FailableBlockStore>(*baseBlockStore)).value()),
    stateFile.path(),
    TEST_CLIENT_ID,
    [] {});
}

void decreaseStoredBlockVersion(InMemoryBlockStore2 *baseBlockStore, const blockstore::BlockId &blockId) {
  auto baseBlock = baseBlockStore->load(blockId).value();
  void* versionPtr = static_cast<uint8_t*>(baseBlock.data()) + IntegrityBlockStore2::VERSION_HEADER_OFFSET;
  const uint64_t version = deserialize<uint64_t>(versionPtr);
  ASSERT(version > 1, "Can't decrease the lowest allowed version number");
  serialize<uint64_t>(versionPtr, version-1);
  baseBlockStore->store(blockId, baseBlock);
}
}

class IntegrityBlockStoreTest: public Test {
public:
  static constexpr unsigned int BLOCKSIZE = 1024;
  IntegrityBlockStoreTest():
    stateFile(false),
    onIntegrityViolation(),
    baseBlockStore(new InMemoryBlockStore2),
    blockStore(make_unique_ref<IntegrityBlockStore2>(std::move(cpputils::nullcheck(std::unique_ptr<InMemoryBlockStore2>(baseBlockStore)).value()), stateFile.path(), myClientId, onIntegrityViolation.callback())),
    data(DataFixture::generate(BLOCKSIZE)) {
  }
  static constexpr uint32_t myClientId = TEST_CLIENT_ID;
  TempFile stateFile;
  FakeCallback onIntegrityViolation;
  InMemoryBlockStore2 *baseBlockStore;
  unique_ref<IntegrityBlockStore2> blockStore;
  Data data;

  blockstore::BlockId CreateBlockReturnKey() {
    return CreateBlockReturnKey(data);
  }

  blockstore::BlockId CreateBlockReturnKey(const Data &initData) {
    return blockStore->create(initData.copy());
  }

  Data loadBaseBlock(const blockstore::BlockId &blockId) {
    return baseBlockStore->load(blockId).value();
  }

  Data loadBlock(const blockstore::BlockId &blockId) {
    return blockStore->load(blockId).value();
  }

  void modifyBlock(const blockstore::BlockId &blockId) {
    auto block = blockStore->load(blockId).value();
    CryptoPP::byte* first_byte = static_cast<CryptoPP::byte*>(block.data());
    *first_byte = *first_byte + 1;
    blockStore->store(blockId, block);
  }

  void rollbackBaseBlock(const blockstore::BlockId &blockId, const Data &data) {
    baseBlockStore->store(blockId, data);
  }

  void decreaseVersionNumber(const blockstore::BlockId &blockId) {
    decreaseStoredBlockVersion(baseBlockStore, blockId);
  }

  void increaseVersionNumber(const blockstore::BlockId &blockId) {
    auto baseBlock = baseBlockStore->load(blockId).value();
    void* versionPtr = static_cast<uint8_t*>(baseBlock.data()) + IntegrityBlockStore2::VERSION_HEADER_OFFSET;
    const uint64_t version = deserialize<uint64_t>(versionPtr);
    serialize<uint64_t>(versionPtr, version+1);
    baseBlockStore->store(blockId, baseBlock);
  }

  void changeClientId(const blockstore::BlockId &blockId) {
    auto baseBlock = baseBlockStore->load(blockId).value();
    void* clientIdPtr = static_cast<uint8_t*>(baseBlock.data()) + IntegrityBlockStore2::CLIENTID_HEADER_OFFSET;
    const uint64_t clientId = deserialize<uint64_t>(clientIdPtr);
    serialize<uint64_t>(clientIdPtr, clientId+1);
    baseBlockStore->store(blockId, baseBlock);
  }

  void setUnsupportedFormatHeader(const blockstore::BlockId &blockId) {
    auto baseBlock = baseBlockStore->load(blockId).value();
    serialize<uint16_t>(baseBlock.data(), 0);
    baseBlockStore->store(blockId, baseBlock);
  }

  void deleteBlock(const blockstore::BlockId &blockId) {
    blockStore->remove(blockId);
  }

  void insertBaseBlock(const blockstore::BlockId &blockId, Data data) {
    EXPECT_TRUE(baseBlockStore->tryCreate(blockId, data));
  }

private:
  DISALLOW_COPY_AND_ASSIGN(IntegrityBlockStoreTest);
};

constexpr uint32_t IntegrityBlockStoreTest::myClientId;

// Test that a decreasing version number is not allowed
TEST_F(IntegrityBlockStoreTest, RollbackPrevention_DoesntAllowDecreasingVersionNumberForSameClient_1) {
  auto blockId = CreateBlockReturnKey();
  const Data oldBaseBlock = loadBaseBlock(blockId);
  modifyBlock(blockId);
  rollbackBaseBlock(blockId, oldBaseBlock);
  EXPECT_EQ(boost::none, blockStore->load(blockId));
  EXPECT_TRUE(onIntegrityViolation.wasCalled());
}

TEST_F(IntegrityBlockStoreTest, SyncPersistsKnownBlockVersionsBeforeDestructor) {
  const auto blockId = CreateBlockReturnKey();

  blockStore->sync();

  const KnownBlockVersions savedState(stateFile.path(), myClientId);
  EXPECT_EQ(1u, savedState.getBlockVersion(myClientId, blockId));
}

TEST_F(IntegrityBlockStoreTest, FlushDoesNotPersistKnownBlockVersions) {
  CreateBlockReturnKey();

  blockStore->flush();

  EXPECT_FALSE(boost::filesystem::exists(stateFile.path()));
}

TEST_F(IntegrityBlockStoreTest, FlushDoesNotPersistNewKnownBlockVersionOverPreviousSync) {
  const auto blockId = CreateBlockReturnKey();
  blockStore->sync();
  blockStore->store(blockId, DataFixture::generate(17));

  blockStore->flush();

  const KnownBlockVersions savedState(stateFile.path(), myClientId);
  EXPECT_EQ(1u, savedState.getBlockVersion(myClientId, blockId));
}

TEST_F(IntegrityBlockStoreTest, SyncFailureKeepsKnownBlockVersionSaveRetryable) {
  const TempDir tempDir;
  const auto stateFilePath = tempDir.path() / "missing-parent" / "known-block-versions";
  auto blockStore = make_unique_ref<IntegrityBlockStore2>(
    make_unique_ref<InMemoryBlockStore2>(),
    stateFilePath,
    myClientId,
    [] {});
  const auto blockId = blockStore->create(data.copy());

  EXPECT_THROW(blockStore->sync(), std::runtime_error);
  ASSERT_TRUE(boost::filesystem::create_directory(stateFilePath.parent_path()));
  blockStore->sync();

  const KnownBlockVersions savedState(stateFilePath, myClientId);
  EXPECT_EQ(1u, savedState.getBlockVersion(myClientId, blockId));
}

TEST_F(IntegrityBlockStoreTest, TryCreateRollsBackKnownVersionWhenBaseReturnsFalse) {
  const auto blockId = blockstore::BlockId::FromString("1491BB4932A389EE14BC7090AC772972");
  ASSERT_TRUE(blockStore->tryCreate(blockId, data));
  blockStore->sync();

  EXPECT_FALSE(blockStore->tryCreate(blockId, data));
  blockStore->sync();

  const KnownBlockVersions savedState(stateFile.path(), myClientId);
  EXPECT_EQ(1u, savedState.getBlockVersion(myClientId, blockId));
}

TEST_F(IntegrityBlockStoreTest, StoreRollsBackKnownVersionWhenBaseThrows) {
  const TempFile localStateFile(false);
  FailableBlockStore *base = nullptr;
  auto blockStore = makeIntegrityBlockStore(&base, localStateFile);
  const auto blockId = blockstore::BlockId::FromString("1491BB4932A389EE14BC7090AC772972");
  ASSERT_TRUE(blockStore->tryCreate(blockId, data));
  blockStore->sync();

  base->throwOnStore = true;
  EXPECT_THROW(blockStore->store(blockId, DataFixture::generate(17)), std::runtime_error);
  base->throwOnStore = false;
  blockStore->sync();

  const KnownBlockVersions savedState(localStateFile.path(), myClientId);
  EXPECT_EQ(1u, savedState.getBlockVersion(myClientId, blockId));
}

TEST_F(IntegrityBlockStoreTest, RemoveRollsBackDeletedStateWhenBaseReturnsFalse) {
  const auto blockId = CreateBlockReturnKey();
  blockStore->sync();
  ASSERT_TRUE(baseBlockStore->remove(blockId));

  EXPECT_FALSE(blockStore->remove(blockId));
  blockStore->sync();

  const KnownBlockVersions savedState(stateFile.path(), myClientId);
  EXPECT_TRUE(savedState.blockShouldExist(blockId));
}

TEST_F(IntegrityBlockStoreTest, RemoveRollsBackDeletedStateWhenBaseThrows) {
  const TempFile localStateFile(false);
  FailableBlockStore *base = nullptr;
  auto blockStore = makeIntegrityBlockStore(&base, localStateFile);
  const auto blockId = blockstore::BlockId::FromString("1491BB4932A389EE14BC7090AC772972");
  ASSERT_TRUE(blockStore->tryCreate(blockId, data));
  blockStore->sync();

  base->throwOnRemove = true;
  EXPECT_THROW(blockStore->remove(blockId), std::runtime_error);
  base->throwOnRemove = false;
  blockStore->sync();

  const KnownBlockVersions savedState(localStateFile.path(), myClientId);
  EXPECT_TRUE(savedState.blockShouldExist(blockId));
}

TEST_F(IntegrityBlockStoreTest, IntegrityViolationPersistsPreviousRunFlagImmediately) {
  const auto blockId = CreateBlockReturnKey();
  modifyBlock(blockId);
  decreaseVersionNumber(blockId);

  EXPECT_EQ(boost::none, blockStore->load(blockId));
  EXPECT_TRUE(onIntegrityViolation.wasCalled());
  EXPECT_THROW(
    IntegrityBlockStore2(
      make_unique_ref<InMemoryBlockStore2>(),
      stateFile.path(),
      myClientId,
      [] {}),
    IntegrityViolationOnPreviousRun);
}

TEST_F(IntegrityBlockStoreTest, IntegrityViolationSaveFailureStillRunsCallback) {
  const TempDir tempDir;
  const auto stateFilePath = tempDir.path() / "missing-parent" / "known-block-versions";
  FakeCallback onIntegrityViolation;
  auto *baseBlockStore = new InMemoryBlockStore2;
  auto blockStore = make_unique_ref<IntegrityBlockStore2>(
    std::move(cpputils::nullcheck(std::unique_ptr<InMemoryBlockStore2>(baseBlockStore)).value()),
    stateFilePath,
    myClientId,
    onIntegrityViolation.callback());
  const auto blockId = blockStore->create(data.copy());
  blockStore->store(blockId, DataFixture::generate(17));
  decreaseStoredBlockVersion(baseBlockStore, blockId);

  EXPECT_THROW(blockStore->load(blockId), std::runtime_error);

  EXPECT_TRUE(onIntegrityViolation.wasCalled());
}

TEST_F(IntegrityBlockStoreTest, RollbackPrevention_DoesntAllowDecreasingVersionNumberForSameClient_2) {
  auto blockId = CreateBlockReturnKey();
  // Increase the version number
  modifyBlock(blockId);
  // Decrease the version number again
  decreaseVersionNumber(blockId);

  EXPECT_EQ(boost::none, blockStore->load(blockId));
  EXPECT_TRUE(onIntegrityViolation.wasCalled());
}

// Test that a different client doesn't need to have a higher version number (i.e. version numbers are per client).
TEST_F(IntegrityBlockStoreTest, RollbackPrevention_DoesAllowDecreasingVersionNumberForDifferentClient) {
  auto blockId = CreateBlockReturnKey();
  // Increase the version number
  modifyBlock(blockId);
  // Fake a modification by a different client with lower version numbers
  changeClientId(blockId);
  decreaseVersionNumber(blockId);
  EXPECT_NE(boost::none, blockStore->load(blockId));
  EXPECT_FALSE(onIntegrityViolation.wasCalled());
}

// Test that it doesn't allow a rollback to the "newest" block of a client, when this block was superseded by a version of a different client
TEST_F(IntegrityBlockStoreTest, RollbackPrevention_DoesntAllowSameVersionNumberForOldClient) {
  auto blockId = CreateBlockReturnKey();
  // Increase the version number
  modifyBlock(blockId);
  const Data oldBaseBlock = loadBaseBlock(blockId);
  // Fake a modification by a different client with lower version numbers
  changeClientId(blockId);
  loadBlock(blockId); // make the block store know about this other client's modification
  // Rollback to old client
  rollbackBaseBlock(blockId, oldBaseBlock);
  EXPECT_EQ(boost::none, blockStore->load(blockId));
  EXPECT_TRUE(onIntegrityViolation.wasCalled());
}

// Test that deleted blocks cannot be re-introduced
TEST_F(IntegrityBlockStoreTest, RollbackPrevention_DoesntAllowReintroducingDeletedBlocks) {
  auto blockId = CreateBlockReturnKey();
  Data oldBaseBlock = loadBaseBlock(blockId);
  deleteBlock(blockId);
  insertBaseBlock(blockId, std::move(oldBaseBlock));
  EXPECT_EQ(boost::none, blockStore->load(blockId));
  EXPECT_TRUE(onIntegrityViolation.wasCalled());
}

// This can happen if a client synchronization is delayed. Another client might have won the conflict and pushed a new version for the deleted block.
TEST_F(IntegrityBlockStoreTest, RollbackPrevention_AllowsReintroducingDeletedBlocksWithNewVersionNumber) {
  auto blockId = CreateBlockReturnKey();
  Data oldBaseBlock = loadBaseBlock(blockId);
  deleteBlock(blockId);
  insertBaseBlock(blockId, std::move(oldBaseBlock));
  increaseVersionNumber(blockId);
  EXPECT_NE(boost::none, blockStore->load(blockId));
  EXPECT_FALSE(onIntegrityViolation.wasCalled());
}

TEST_F(IntegrityBlockStoreTest, DeletionPrevention_MissingKnownBlockTriggersIntegrityViolation) {
  auto blockId = blockStore->create(Data(0));
  baseBlockStore->remove(blockId);
  EXPECT_EQ(boost::none, blockStore->load(blockId));
  EXPECT_TRUE(onIntegrityViolation.wasCalled());
}

TEST_F(IntegrityBlockStoreTest, DeletionPrevention_InForEachBlock_MissingKnownBlockTriggersIntegrityViolation) {
  auto blockId = blockStore->create(Data(0));
  baseBlockStore->remove(blockId);
  int count = 0;
  blockStore->forEachBlock([&count] (const blockstore::BlockId &) {
      ++count;
  });
  EXPECT_EQ(0, count);
  EXPECT_TRUE(onIntegrityViolation.wasCalled());
}

TEST_F(IntegrityBlockStoreTest, LoadingWithDifferentBlockIdFails) {
  auto blockId = CreateBlockReturnKey();
  const blockstore::BlockId key2 = blockstore::BlockId::FromString("1491BB4932A389EE14BC7090AC772972");
  baseBlockStore->store(key2, baseBlockStore->load(blockId).value());
  EXPECT_EQ(boost::none, blockStore->load(key2));
  EXPECT_TRUE(onIntegrityViolation.wasCalled());
}

TEST_F(IntegrityBlockStoreTest, LoadingUnsupportedFormatFailsClosed) {
  auto blockId = CreateBlockReturnKey();
  setUnsupportedFormatHeader(blockId);
  EXPECT_THROW(blockStore->load(blockId), std::runtime_error);
}

TEST_F(IntegrityBlockStoreTest, LoadingTruncatedHeaderFailsClosed) {
  const auto blockId = CreateBlockReturnKey();

  baseBlockStore->store(blockId, Data(IntegrityBlockStore2::HEADER_LENGTH - 1));

  EXPECT_THROW(blockStore->load(blockId), std::runtime_error);
}

// TODO Test more integrity cases:
//   - RollbackPrevention_DoesntAllowReintroducingDeletedBlocks with different client id (i.e. trying to re-introduce the newest block of a different client)
//   - RollbackPrevention_AllowsReintroducingDeletedBlocksWithNewVersionNumber with different client id
//   - Think about more...

TEST_F(IntegrityBlockStoreTest, PhysicalBlockSize_zerophysical) {
  EXPECT_EQ(0u, blockStore->blockSizeFromPhysicalBlockSize(0));
}

TEST_F(IntegrityBlockStoreTest, PhysicalBlockSize_zerovirtual) {
  auto blockId = CreateBlockReturnKey(Data(0));
  auto base = baseBlockStore->load(blockId).value();
  EXPECT_EQ(0u, blockStore->blockSizeFromPhysicalBlockSize(base.size()));
}

TEST_F(IntegrityBlockStoreTest, PhysicalBlockSize_negativeboundaries) {
  // This tests that a potential if/else in blockSizeFromPhysicalBlockSize that catches negative values has the
  // correct boundary set. We test the highest value that is negative and the smallest value that is positive.
  auto physicalSizeForVirtualSizeZero = baseBlockStore->load(CreateBlockReturnKey(Data(0))).value().size();
  if (physicalSizeForVirtualSizeZero > 0) {
    EXPECT_EQ(0u, blockStore->blockSizeFromPhysicalBlockSize(physicalSizeForVirtualSizeZero - 1));
  }
  EXPECT_EQ(0u, blockStore->blockSizeFromPhysicalBlockSize(physicalSizeForVirtualSizeZero));
  EXPECT_EQ(1u, blockStore->blockSizeFromPhysicalBlockSize(physicalSizeForVirtualSizeZero + 1));
}

TEST_F(IntegrityBlockStoreTest, PhysicalBlockSize_positive) {
  auto blockId = CreateBlockReturnKey(Data(10*1024));
  auto base = baseBlockStore->load(blockId).value();
  EXPECT_EQ(10*1024u, blockStore->blockSizeFromPhysicalBlockSize(base.size()));
}
