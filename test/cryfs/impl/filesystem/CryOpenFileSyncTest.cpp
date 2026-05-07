#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

#include <blockstore/implementations/inmemory/InMemoryBlockStore2.h>
#include <cpp-utils/crypto/symmetric/ciphers.h>
#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>
#include <cpp-utils/tempfile/TempFile.h>
#include <cryfs/impl/config/CryConfigFile.h>
#include <cryfs/impl/config/crypto/CryConfigEncryptor.h>
#include <cryfs/impl/filesystem/CryDevice.h>
#include <cryfs/impl/localstate/LocalStateDir.h>
#include <fspp/fs_interface/Dir.h>
#include <fspp/fs_interface/OpenFile.h>

#include "../testutils/TestWithFakeHomeDirectory.h"

namespace {

class RecordingBlockStore2 final: public blockstore::BlockStore2 {
public:
  bool tryCreate(const blockstore::BlockId &blockId, const cpputils::Data &data) override {
    return _base.tryCreate(blockId, data);
  }

  bool remove(const blockstore::BlockId &blockId) override {
    return _base.remove(blockId);
  }

  boost::optional<cpputils::Data> load(const blockstore::BlockId &blockId) const override {
    return _base.load(blockId);
  }

  void store(const blockstore::BlockId &blockId, const cpputils::Data &data) override {
    return _base.store(blockId, data);
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
  blockstore::inmemory::InMemoryBlockStore2 _base;
  uint32_t _flushCount = 0;
  uint32_t _syncCount = 0;
  bool _failFlush = false;
  bool _failSync = false;
};

auto failOnIntegrityViolation() {
  return [] {
    EXPECT_TRUE(false);
  };
}

fspp::mode_t publicMode() {
  return fspp::mode_t()
    .addUserReadFlag().addUserWriteFlag().addUserExecFlag()
    .addGroupReadFlag().addGroupWriteFlag().addGroupExecFlag()
    .addOtherReadFlag().addOtherWriteFlag().addOtherExecFlag();
}

class CryOpenFileSyncTest: public ::testing::Test, public TestWithFakeHomeDirectory {
public:
  CryOpenFileSyncTest()
    : _tempLocalStateDir(),
      _localStateDir(_tempLocalStateDir.path()),
      _configFile(false) {
    auto blockStore = cpputils::make_unique_ref<RecordingBlockStore2>();
    _recordingBlockStore = blockStore.get();
    _device = std::make_unique<cryfs::CryDevice>(configFile(), std::move(blockStore), _localStateDir, 0x12345678, failOnIntegrityViolation());
    _device->setContext(fspp::Context { fspp::relatime() });
    _recordingBlockStore->resetCounts();
  }

  cpputils::unique_ref<fspp::OpenFile> createAndOpenFile(const std::string &name) {
    auto rootDir = loadRootDir();
    return rootDir->createAndOpenFile(name, publicMode(), fspp::uid_t(0), fspp::gid_t(0));
  }

  cpputils::unique_ref<fspp::Dir> loadRootDir() {
    return _device->LoadDir(boost::filesystem::path("/")).value();
  }

  RecordingBlockStore2 &recordingBlockStore() {
    return *_recordingBlockStore;
  }

private:
  std::shared_ptr<cryfs::CryConfigFile> configFile() {
    cryfs::CryConfig config;
    config.SetCipher("aes-256-gcm");
    config.SetEncryptionKey(cpputils::AES256_GCM::EncryptionKey::FromString(cpputils::DataFixture::generate(cpputils::AES256_GCM::KEYSIZE).ToString()));
    config.SetBlocksizeBytes(10240);
    auto encryptor = cpputils::make_unique_ref<cryfs::CryConfigEncryptor>(
      cpputils::EncryptionKey::FromString(cpputils::DataFixture::generate(cryfs::CryConfigEncryptor::MaxTotalKeySize).ToString()),
      cpputils::DataFixture::generate(32));
    return std::make_shared<cryfs::CryConfigFile>(_configFile.path(), std::move(config), std::move(encryptor), cryfs::CryConfigFile::Access::ReadWrite);
  }

  cpputils::TempDir _tempLocalStateDir;
  cryfs::LocalStateDir _localStateDir;
  cpputils::TempFile _configFile;
  RecordingBlockStore2 *_recordingBlockStore = nullptr;
  std::unique_ptr<cryfs::CryDevice> _device;
};

TEST_F(CryOpenFileSyncTest, FlushDoesNotSyncBackingStore) {
  auto file = createAndOpenFile("file");
  const std::string data = "data";
  file->write(data.data(), fspp::num_bytes_t(data.size()), fspp::num_bytes_t(0));

  recordingBlockStore().resetCounts();
  file->flush();

  EXPECT_EQ(0u, recordingBlockStore().syncCount());
}

TEST_F(CryOpenFileSyncTest, FsyncSyncsBackingStore) {
  auto file = createAndOpenFile("file");
  const std::string data = "data";
  file->write(data.data(), fspp::num_bytes_t(data.size()), fspp::num_bytes_t(0));

  recordingBlockStore().resetCounts();
  file->fsync();

  EXPECT_GT(recordingBlockStore().flushCount(), 0u);
  EXPECT_EQ(1u, recordingBlockStore().syncCount());
}

TEST_F(CryOpenFileSyncTest, FsyncPropagatesBackingSyncFailure) {
  auto file = createAndOpenFile("file");
  const std::string data = "data";
  file->write(data.data(), fspp::num_bytes_t(data.size()), fspp::num_bytes_t(0));

  recordingBlockStore().resetCounts();
  recordingBlockStore().failSyncs();

  EXPECT_THROW(file->fsync(), std::runtime_error);
  EXPECT_GT(recordingBlockStore().flushCount(), 0u);
  EXPECT_EQ(1u, recordingBlockStore().syncCount());
}

TEST_F(CryOpenFileSyncTest, FsyncPropagatesBackingFlushFailureWithoutSyncing) {
  auto file = createAndOpenFile("file");
  const std::string data = "data";
  file->write(data.data(), fspp::num_bytes_t(data.size()), fspp::num_bytes_t(0));

  recordingBlockStore().resetCounts();
  recordingBlockStore().failFlushes();

  EXPECT_THROW(file->fsync(), std::runtime_error);
  EXPECT_GT(recordingBlockStore().flushCount(), 0u);
  EXPECT_EQ(0u, recordingBlockStore().syncCount());
}

TEST_F(CryOpenFileSyncTest, FdatasyncSyncsBackingStore) {
  auto file = createAndOpenFile("file");
  const std::string data = "data";
  file->write(data.data(), fspp::num_bytes_t(data.size()), fspp::num_bytes_t(0));

  recordingBlockStore().resetCounts();
  file->fdatasync();

  EXPECT_GT(recordingBlockStore().flushCount(), 0u);
  EXPECT_EQ(1u, recordingBlockStore().syncCount());
}

TEST_F(CryOpenFileSyncTest, FdatasyncPropagatesBackingSyncFailure) {
  auto file = createAndOpenFile("file");
  const std::string data = "data";
  file->write(data.data(), fspp::num_bytes_t(data.size()), fspp::num_bytes_t(0));

  recordingBlockStore().resetCounts();
  recordingBlockStore().failSyncs();

  EXPECT_THROW(file->fdatasync(), std::runtime_error);
  EXPECT_GT(recordingBlockStore().flushCount(), 0u);
  EXPECT_EQ(1u, recordingBlockStore().syncCount());
}

TEST_F(CryOpenFileSyncTest, DirectoryFsyncSyncsBackingStore) {
  auto rootDir = loadRootDir();
  rootDir->createAndOpenFile("file", publicMode(), fspp::uid_t(0), fspp::gid_t(0));

  recordingBlockStore().resetCounts();
  rootDir->fsync();

  EXPECT_GT(recordingBlockStore().flushCount(), 0u);
  EXPECT_EQ(1u, recordingBlockStore().syncCount());
}

TEST_F(CryOpenFileSyncTest, DirectoryFsyncPropagatesBackingSyncFailure) {
  auto rootDir = loadRootDir();
  rootDir->createAndOpenFile("file", publicMode(), fspp::uid_t(0), fspp::gid_t(0));

  recordingBlockStore().resetCounts();
  recordingBlockStore().failSyncs();

  EXPECT_THROW(rootDir->fsync(), std::runtime_error);
  EXPECT_GT(recordingBlockStore().flushCount(), 0u);
  EXPECT_EQ(1u, recordingBlockStore().syncCount());
}

}
