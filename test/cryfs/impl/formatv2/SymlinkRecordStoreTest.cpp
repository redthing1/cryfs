#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/SymlinkRecordStore.h>

#include <boost/filesystem.hpp>
#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstdint>
#include <stdexcept>
#include <string>

using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::SymlinkMetadata;
using cryfs::formatv2::SymlinkRecord;
using cryfs::formatv2::SymlinkRecordStore;
using cryfs::formatv2::Timestamp;
using cryfs::formatv2::deriveObjectEncryptionKey;

namespace {

FilesystemId filesystemId(const std::string &value) {
  return FilesystemId::FromString(value);
}

FilesystemId filesystemId() {
  return filesystemId("00112233445566778899AABBCCDDEEFF");
}

FilesystemId otherFilesystemId() {
  return filesystemId("FFEEDDCCBBAA99887766554433221100");
}

ObjectId objectId(unsigned int seed) {
  return cpputils::DataFixture::generateFixedSize<ObjectId::BINARY_LENGTH>(seed);
}

cpputils::EncryptionKey masterKey(unsigned int seed = 1) {
  return cpputils::EncryptionKey::FromString(
    cpputils::DataFixture::generate(FORMAT_V2_MASTER_KEY_SIZE, seed).ToString());
}

cpputils::EncryptionKey objectEncryptionKey(unsigned int seed = 1) {
  return deriveObjectEncryptionKey(masterKey(seed), filesystemId());
}

Timestamp timestamp(int64_t seconds, uint32_t nanoseconds) {
  return Timestamp{seconds, nanoseconds};
}

SymlinkMetadata metadata() {
  return SymlinkMetadata{
    0777,
    1000,
    1001,
    timestamp(10, 11),
    timestamp(12, 13),
    timestamp(14, 15)
  };
}

SymlinkRecord symlinkRecord() {
  return SymlinkRecord{
    filesystemId(),
    objectId(1),
    7,
    metadata(),
    "../target/path"
  };
}

boost::filesystem::path symlinkRecordPath(
  const boost::filesystem::path &directory,
  const ObjectId &symlinkId,
  uint64_t generation) {
  return directory / ("symlink." + symlinkId.ToString() + "." + std::to_string(generation));
}

void flipByte(cpputils::Data *data, size_t offset) {
  auto *byte = static_cast<uint8_t*>(data->dataOffset(offset));
  *byte ^= 1;
}

}

TEST(SymlinkRecordStoreTest, ConstructorCreatesDirectory) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "symlinks";

  const SymlinkRecordStore store(directory, filesystemId(), objectEncryptionKey());

  EXPECT_TRUE(boost::filesystem::is_directory(directory));
}

TEST(SymlinkRecordStoreTest, ConstructorRejectsNullFilesystemId) {
  const cpputils::TempDir tempDir;

  EXPECT_THROW(
    SymlinkRecordStore(tempDir.path() / "symlinks", FilesystemId::Null(), objectEncryptionKey()),
    std::runtime_error);
}

TEST(SymlinkRecordStoreTest, StoreWritesSymlinkReadableByIdentity) {
  const cpputils::TempDir tempDir;
  const SymlinkRecordStore store(tempDir.path() / "symlinks", filesystemId(), objectEncryptionKey());
  const SymlinkRecord record = symlinkRecord();

  store.store(record);

  const auto loaded = store.load(record.symlinkId, record.generation);
  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(record, *loaded);
}

TEST(SymlinkRecordStoreTest, StoreIsIdempotentForSameSymlinkRecord) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "symlinks";
  const SymlinkRecordStore store(directory, filesystemId(), objectEncryptionKey());
  const SymlinkRecord record = symlinkRecord();
  store.store(record);
  const cpputils::Data before = cpputils::Data::LoadFromFile(
    symlinkRecordPath(directory, record.symlinkId, record.generation)).value();

  store.store(record);

  const cpputils::Data after = cpputils::Data::LoadFromFile(
    symlinkRecordPath(directory, record.symlinkId, record.generation)).value();
  EXPECT_EQ(before, after);
  EXPECT_EQ(record, *store.load(record.symlinkId, record.generation));
}

TEST(SymlinkRecordStoreTest, StoreRejectsConflictingSymlinkRecordWithSameIdentity) {
  const cpputils::TempDir tempDir;
  const SymlinkRecordStore store(tempDir.path() / "symlinks", filesystemId(), objectEncryptionKey());
  const SymlinkRecord record = symlinkRecord();
  SymlinkRecord conflicting = record;
  conflicting.target = "../other/target";
  store.store(record);

  EXPECT_THROW(store.store(conflicting), std::runtime_error);
  EXPECT_EQ(record, *store.load(record.symlinkId, record.generation));
}

TEST(SymlinkRecordStoreTest, LoadReturnsNoneWhenSymlinkRecordIsMissing) {
  const cpputils::TempDir tempDir;
  const SymlinkRecordStore store(tempDir.path() / "symlinks", filesystemId(), objectEncryptionKey());
  const SymlinkRecord record = symlinkRecord();

  EXPECT_FALSE(store.load(record.symlinkId, record.generation).is_initialized());
}

TEST(SymlinkRecordStoreTest, StoreRejectsWrongFilesystemId) {
  const cpputils::TempDir tempDir;
  const SymlinkRecordStore store(tempDir.path() / "symlinks", filesystemId(), objectEncryptionKey());
  SymlinkRecord record = symlinkRecord();
  record.filesystemId = otherFilesystemId();

  EXPECT_THROW(store.store(record), std::runtime_error);
}

TEST(SymlinkRecordStoreTest, LoadRejectsTamperedSymlinkRecord) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "symlinks";
  const SymlinkRecordStore store(directory, filesystemId(), objectEncryptionKey());
  const SymlinkRecord record = symlinkRecord();
  store.store(record);

  cpputils::Data data = cpputils::Data::LoadFromFile(
    symlinkRecordPath(directory, record.symlinkId, record.generation)).value();
  flipByte(&data, data.size() - 1);
  data.StoreToFile(symlinkRecordPath(directory, record.symlinkId, record.generation));

  EXPECT_FALSE(store.load(record.symlinkId, record.generation).is_initialized());
}

TEST(SymlinkRecordStoreTest, LoadRejectsWrongObjectEncryptionKey) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "symlinks";
  const SymlinkRecord record = symlinkRecord();
  SymlinkRecordStore(directory, filesystemId(), objectEncryptionKey(1)).store(record);

  const SymlinkRecordStore store(directory, filesystemId(), objectEncryptionKey(2));

  EXPECT_FALSE(store.load(record.symlinkId, record.generation).is_initialized());
}

TEST(SymlinkRecordStoreTest, LoadRejectsWrongSymlinkId) {
  const cpputils::TempDir tempDir;
  const SymlinkRecordStore store(tempDir.path() / "symlinks", filesystemId(), objectEncryptionKey());
  const SymlinkRecord record = symlinkRecord();
  store.store(record);

  EXPECT_FALSE(store.load(objectId(99), record.generation).is_initialized());
}

TEST(SymlinkRecordStoreTest, LoadRejectsWrongGeneration) {
  const cpputils::TempDir tempDir;
  const SymlinkRecordStore store(tempDir.path() / "symlinks", filesystemId(), objectEncryptionKey());
  const SymlinkRecord record = symlinkRecord();
  store.store(record);

  EXPECT_FALSE(store.load(record.symlinkId, record.generation + 1).is_initialized());
}
