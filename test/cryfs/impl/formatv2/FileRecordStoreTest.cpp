#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/FileRecordStore.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>

#include <boost/filesystem.hpp>
#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstdint>
#include <stdexcept>
#include <string>

using cryfs::formatv2::FileDataRecord;
using cryfs::formatv2::FileExtent;
using cryfs::formatv2::FileMetadata;
using cryfs::formatv2::FileRecord;
using cryfs::formatv2::FileRecordStore;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::ObjectId;
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

FileMetadata metadata() {
  return FileMetadata{
    0644,
    1000,
    1001,
    timestamp(10, 11),
    timestamp(12, 13),
    timestamp(14, 15)
  };
}

FileExtent extent(uint64_t offset, uint64_t size, unsigned int dataIdSeed, uint64_t generation) {
  return FileExtent{offset, size, objectId(dataIdSeed), generation};
}

FileRecord fileRecord() {
  return FileRecord{
    filesystemId(),
    objectId(1),
    7,
    metadata(),
    16,
    {
      extent(0, 4, 2, 3),
      extent(8, 8, 3, 4)
    }
  };
}

FileDataRecord fileDataRecord(unsigned int payloadSeed = 1) {
  return FileDataRecord{
    filesystemId(),
    objectId(2),
    3,
    cpputils::DataFixture::generate(32, payloadSeed)
  };
}

boost::filesystem::path fileRecordPath(
  const boost::filesystem::path &directory,
  const ObjectId &fileId,
  uint64_t generation) {
  return directory / ("file." + fileId.ToString() + "." + std::to_string(generation));
}

boost::filesystem::path fileDataRecordPath(
  const boost::filesystem::path &directory,
  const ObjectId &dataId,
  uint64_t generation) {
  return directory / ("file-data." + dataId.ToString() + "." + std::to_string(generation));
}

void flipByte(cpputils::Data *data, size_t offset) {
  auto *byte = static_cast<uint8_t*>(data->dataOffset(offset));
  *byte ^= 1;
}

}

TEST(FileRecordStoreTest, ConstructorCreatesDirectory) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "files";

  const FileRecordStore store(directory, filesystemId(), objectEncryptionKey());

  EXPECT_TRUE(boost::filesystem::is_directory(directory));
}

TEST(FileRecordStoreTest, ConstructorRejectsNullFilesystemId) {
  const cpputils::TempDir tempDir;

  EXPECT_THROW(
    FileRecordStore(tempDir.path() / "files", FilesystemId::Null(), objectEncryptionKey()),
    std::runtime_error);
}

TEST(FileRecordStoreTest, StoreWritesFileReadableByIdentity) {
  const cpputils::TempDir tempDir;
  const FileRecordStore store(tempDir.path() / "files", filesystemId(), objectEncryptionKey());
  const FileRecord record = fileRecord();

  store.store(record);

  const auto loaded = store.load(record.fileId, record.generation);
  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(record, *loaded);
}

TEST(FileRecordStoreTest, StoreIsIdempotentForSameFileRecord) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "files";
  const FileRecordStore store(directory, filesystemId(), objectEncryptionKey());
  const FileRecord record = fileRecord();
  store.store(record);
  const cpputils::Data before = cpputils::Data::LoadFromFile(
    fileRecordPath(directory, record.fileId, record.generation)).value();

  store.store(record);

  const cpputils::Data after = cpputils::Data::LoadFromFile(
    fileRecordPath(directory, record.fileId, record.generation)).value();
  EXPECT_EQ(before, after);
  EXPECT_EQ(record, *store.load(record.fileId, record.generation));
}

TEST(FileRecordStoreTest, StoreRejectsConflictingFileRecordWithSameIdentity) {
  const cpputils::TempDir tempDir;
  const FileRecordStore store(tempDir.path() / "files", filesystemId(), objectEncryptionKey());
  const FileRecord record = fileRecord();
  FileRecord conflicting = record;
  conflicting.metadata.permissions = 0600;
  store.store(record);

  EXPECT_THROW(store.store(conflicting), std::runtime_error);
  EXPECT_EQ(record, *store.load(record.fileId, record.generation));
}

TEST(FileRecordStoreTest, LoadReturnsNoneWhenFileRecordIsMissing) {
  const cpputils::TempDir tempDir;
  const FileRecordStore store(tempDir.path() / "files", filesystemId(), objectEncryptionKey());
  const FileRecord record = fileRecord();

  EXPECT_FALSE(store.load(record.fileId, record.generation).is_initialized());
}

TEST(FileRecordStoreTest, StoreRejectsWrongFilesystemId) {
  const cpputils::TempDir tempDir;
  const FileRecordStore store(tempDir.path() / "files", filesystemId(), objectEncryptionKey());
  FileRecord record = fileRecord();
  record.filesystemId = otherFilesystemId();

  EXPECT_THROW(store.store(record), std::runtime_error);
}

TEST(FileRecordStoreTest, LoadRejectsTamperedFileRecord) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "files";
  const FileRecordStore store(directory, filesystemId(), objectEncryptionKey());
  const FileRecord record = fileRecord();
  store.store(record);

  cpputils::Data data = cpputils::Data::LoadFromFile(fileRecordPath(directory, record.fileId, record.generation)).value();
  flipByte(&data, data.size() - 1);
  data.StoreToFile(fileRecordPath(directory, record.fileId, record.generation));

  EXPECT_FALSE(store.load(record.fileId, record.generation).is_initialized());
}

TEST(FileRecordStoreTest, LoadRejectsWrongFileRecordKey) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "files";
  const FileRecord record = fileRecord();
  FileRecordStore(directory, filesystemId(), objectEncryptionKey(1)).store(record);

  const FileRecordStore store(directory, filesystemId(), objectEncryptionKey(2));

  EXPECT_FALSE(store.load(record.fileId, record.generation).is_initialized());
}

TEST(FileRecordStoreTest, LoadRejectsWrongFileIdentity) {
  const cpputils::TempDir tempDir;
  const FileRecordStore store(tempDir.path() / "files", filesystemId(), objectEncryptionKey());
  const FileRecord record = fileRecord();
  store.store(record);

  EXPECT_FALSE(store.load(objectId(99), record.generation).is_initialized());
  EXPECT_FALSE(store.load(record.fileId, record.generation + 1).is_initialized());
}

TEST(FileRecordStoreTest, StoreWritesFileDataReadableByIdentity) {
  const cpputils::TempDir tempDir;
  const FileRecordStore store(tempDir.path() / "files", filesystemId(), objectEncryptionKey());
  const FileDataRecord record = fileDataRecord();

  store.storeData(record);

  const auto loaded = store.loadData(record.dataId, record.generation);
  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(record, *loaded);
}

TEST(FileRecordStoreTest, StoreDataIsIdempotentForSameFileDataRecord) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "files";
  const FileRecordStore store(directory, filesystemId(), objectEncryptionKey());
  const FileDataRecord record = fileDataRecord();
  store.storeData(record);
  const cpputils::Data before = cpputils::Data::LoadFromFile(
    fileDataRecordPath(directory, record.dataId, record.generation)).value();

  store.storeData(record);

  const cpputils::Data after = cpputils::Data::LoadFromFile(
    fileDataRecordPath(directory, record.dataId, record.generation)).value();
  EXPECT_EQ(before, after);
  EXPECT_EQ(record, *store.loadData(record.dataId, record.generation));
}

TEST(FileRecordStoreTest, StoreDataRejectsConflictingFileDataRecordWithSameIdentity) {
  const cpputils::TempDir tempDir;
  const FileRecordStore store(tempDir.path() / "files", filesystemId(), objectEncryptionKey());
  const FileDataRecord record = fileDataRecord(1);
  const FileDataRecord conflicting = fileDataRecord(2);
  store.storeData(record);

  EXPECT_THROW(store.storeData(conflicting), std::runtime_error);
  EXPECT_EQ(record, *store.loadData(record.dataId, record.generation));
}

TEST(FileRecordStoreTest, LoadReturnsNoneWhenFileDataRecordIsMissing) {
  const cpputils::TempDir tempDir;
  const FileRecordStore store(tempDir.path() / "files", filesystemId(), objectEncryptionKey());
  const FileDataRecord record = fileDataRecord();

  EXPECT_FALSE(store.loadData(record.dataId, record.generation).is_initialized());
}

TEST(FileRecordStoreTest, StoreRejectsWrongFileDataFilesystemId) {
  const cpputils::TempDir tempDir;
  const FileRecordStore store(tempDir.path() / "files", filesystemId(), objectEncryptionKey());
  FileDataRecord record = fileDataRecord();
  record.filesystemId = otherFilesystemId();

  EXPECT_THROW(store.storeData(record), std::runtime_error);
}

TEST(FileRecordStoreTest, LoadRejectsTamperedFileDataRecord) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "files";
  const FileRecordStore store(directory, filesystemId(), objectEncryptionKey());
  const FileDataRecord record = fileDataRecord();
  store.storeData(record);

  cpputils::Data data = cpputils::Data::LoadFromFile(fileDataRecordPath(directory, record.dataId, record.generation)).value();
  flipByte(&data, data.size() - 1);
  data.StoreToFile(fileDataRecordPath(directory, record.dataId, record.generation));

  EXPECT_FALSE(store.loadData(record.dataId, record.generation).is_initialized());
}

TEST(FileRecordStoreTest, LoadRejectsWrongFileDataKey) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "files";
  FileDataRecord record = fileDataRecord();
  FileRecordStore(directory, filesystemId(), objectEncryptionKey(1)).storeData(record);

  const FileRecordStore store(directory, filesystemId(), objectEncryptionKey(2));

  EXPECT_FALSE(store.loadData(record.dataId, record.generation).is_initialized());
}

TEST(FileRecordStoreTest, LoadRejectsWrongFileDataIdentity) {
  const cpputils::TempDir tempDir;
  const FileRecordStore store(tempDir.path() / "files", filesystemId(), objectEncryptionKey());
  const FileDataRecord record = fileDataRecord();
  store.storeData(record);

  EXPECT_FALSE(store.loadData(objectId(99), record.generation).is_initialized());
  EXPECT_FALSE(store.loadData(record.dataId, record.generation + 1).is_initialized());
}
