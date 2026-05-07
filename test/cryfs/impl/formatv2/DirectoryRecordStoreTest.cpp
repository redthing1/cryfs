#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/DirectoryRecordStore.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>

#include <boost/filesystem.hpp>
#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstdint>
#include <stdexcept>
#include <string>

using cryfs::formatv2::DirectoryEntry;
using cryfs::formatv2::DirectoryMetadata;
using cryfs::formatv2::DirectoryRecord;
using cryfs::formatv2::DirectoryRecordStore;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::ObjectType;
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

DirectoryMetadata metadata() {
  return DirectoryMetadata{
    0755,
    1000,
    1001,
    timestamp(10, 11),
    timestamp(12, 13),
    timestamp(14, 15)
  };
}

DirectoryRecord directoryRecord() {
  return DirectoryRecord{
    filesystemId(),
    objectId(1),
    7,
    metadata(),
    {
      DirectoryEntry{"alpha", ObjectType::File, objectId(2), 3},
      DirectoryEntry{"beta", ObjectType::Directory, objectId(3), 4}
    }
  };
}

boost::filesystem::path directoryRecordPath(
  const boost::filesystem::path &directory,
  const ObjectId &directoryId,
  uint64_t generation) {
  return directory / ("directory." + directoryId.ToString() + "." + std::to_string(generation));
}

void flipByte(cpputils::Data *data, size_t offset) {
  auto *byte = static_cast<uint8_t*>(data->dataOffset(offset));
  *byte ^= 1;
}

}

TEST(DirectoryRecordStoreTest, ConstructorCreatesDirectory) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "directories";

  const DirectoryRecordStore store(directory, filesystemId(), objectEncryptionKey());

  EXPECT_TRUE(boost::filesystem::is_directory(directory));
}

TEST(DirectoryRecordStoreTest, ConstructorRejectsNullFilesystemId) {
  const cpputils::TempDir tempDir;

  EXPECT_THROW(
    DirectoryRecordStore(tempDir.path() / "directories", FilesystemId::Null(), objectEncryptionKey()),
    std::runtime_error);
}

TEST(DirectoryRecordStoreTest, StoreWritesDirectoryReadableByIdentity) {
  const cpputils::TempDir tempDir;
  const DirectoryRecordStore store(tempDir.path() / "directories", filesystemId(), objectEncryptionKey());
  const DirectoryRecord record = directoryRecord();

  store.store(record);

  const auto loaded = store.load(record.directoryId, record.generation);
  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(record, *loaded);
}

TEST(DirectoryRecordStoreTest, StoreIsIdempotentForSameDirectoryRecord) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "directories";
  const DirectoryRecordStore store(directory, filesystemId(), objectEncryptionKey());
  const DirectoryRecord record = directoryRecord();
  store.store(record);
  const cpputils::Data before =
    cpputils::Data::LoadFromFile(directoryRecordPath(directory, record.directoryId, record.generation)).value();

  store.store(record);

  const cpputils::Data after =
    cpputils::Data::LoadFromFile(directoryRecordPath(directory, record.directoryId, record.generation)).value();
  EXPECT_EQ(before, after);
  EXPECT_EQ(record, *store.load(record.directoryId, record.generation));
}

TEST(DirectoryRecordStoreTest, StoreRejectsConflictingDirectoryRecordWithSameIdentity) {
  const cpputils::TempDir tempDir;
  const DirectoryRecordStore store(tempDir.path() / "directories", filesystemId(), objectEncryptionKey());
  const DirectoryRecord record = directoryRecord();
  DirectoryRecord conflicting = record;
  conflicting.metadata.permissions = 0700;
  store.store(record);

  EXPECT_THROW(store.store(conflicting), std::runtime_error);
  EXPECT_EQ(record, *store.load(record.directoryId, record.generation));
}

TEST(DirectoryRecordStoreTest, LoadReturnsNoneWhenDirectoryRecordIsMissing) {
  const cpputils::TempDir tempDir;
  const DirectoryRecordStore store(tempDir.path() / "directories", filesystemId(), objectEncryptionKey());
  const DirectoryRecord record = directoryRecord();

  EXPECT_FALSE(store.load(record.directoryId, record.generation).is_initialized());
}

TEST(DirectoryRecordStoreTest, StoreRejectsWrongFilesystemId) {
  const cpputils::TempDir tempDir;
  const DirectoryRecordStore store(tempDir.path() / "directories", filesystemId(), objectEncryptionKey());
  DirectoryRecord record = directoryRecord();
  record.filesystemId = otherFilesystemId();

  EXPECT_THROW(store.store(record), std::runtime_error);
}

TEST(DirectoryRecordStoreTest, LoadRejectsTamperedDirectoryRecord) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "directories";
  const DirectoryRecordStore store(directory, filesystemId(), objectEncryptionKey());
  const DirectoryRecord record = directoryRecord();
  store.store(record);

  cpputils::Data data = cpputils::Data::LoadFromFile(
    directoryRecordPath(directory, record.directoryId, record.generation)).value();
  flipByte(&data, data.size() - 1);
  data.StoreToFile(directoryRecordPath(directory, record.directoryId, record.generation));

  EXPECT_FALSE(store.load(record.directoryId, record.generation).is_initialized());
}

TEST(DirectoryRecordStoreTest, LoadRejectsWrongObjectEncryptionKey) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "directories";
  const DirectoryRecord record = directoryRecord();
  DirectoryRecordStore(directory, filesystemId(), objectEncryptionKey(1)).store(record);

  const DirectoryRecordStore store(directory, filesystemId(), objectEncryptionKey(2));

  EXPECT_FALSE(store.load(record.directoryId, record.generation).is_initialized());
}

TEST(DirectoryRecordStoreTest, LoadRejectsWrongDirectoryId) {
  const cpputils::TempDir tempDir;
  const DirectoryRecordStore store(tempDir.path() / "directories", filesystemId(), objectEncryptionKey());
  const DirectoryRecord record = directoryRecord();
  store.store(record);

  EXPECT_FALSE(store.load(objectId(99), record.generation).is_initialized());
}

TEST(DirectoryRecordStoreTest, LoadRejectsWrongGeneration) {
  const cpputils::TempDir tempDir;
  const DirectoryRecordStore store(tempDir.path() / "directories", filesystemId(), objectEncryptionKey());
  const DirectoryRecord record = directoryRecord();
  store.store(record);

  EXPECT_FALSE(store.load(record.directoryId, record.generation + 1).is_initialized());
}
