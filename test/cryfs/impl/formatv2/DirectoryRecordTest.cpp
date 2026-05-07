#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/DirectoryRecord.h>

#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/data/Serializer.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

using cryfs::formatv2::DirectoryEntry;
using cryfs::formatv2::DirectoryMetadata;
using cryfs::formatv2::DirectoryRecord;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::ObjectType;
using cryfs::formatv2::Timestamp;
using cryfs::formatv2::deserializeDirectoryRecord;
using cryfs::formatv2::directoryRecordMatchesObject;
using cryfs::formatv2::serializeDirectoryRecord;

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

DirectoryEntry entry(
  const std::string &name,
  ObjectType type,
  unsigned int objectIdSeed,
  uint64_t generation) {
  return DirectoryEntry{name, type, objectId(objectIdSeed), generation};
}

DirectoryRecord directoryRecord() {
  return DirectoryRecord{
    filesystemId(),
    objectId(1),
    7,
    metadata(),
    {
      entry("alpha", ObjectType::File, 2, 3),
      entry("beta", ObjectType::Directory, 3, 4),
      entry("gamma", ObjectType::Symlink, 4, 5)
    }
  };
}

cpputils::Data serializeStringOnly(const std::string &value) {
  cpputils::Serializer serializer(cpputils::Serializer::StringSize(value));
  serializer.writeString(value);
  return serializer.finished();
}

cpputils::Data withTrailingByte(const cpputils::Data &data) {
  cpputils::Data result(data.size() + 1);
  std::memcpy(result.data(), data.data(), data.size());
  static_cast<char*>(result.data())[data.size()] = 0;
  return result;
}

cpputils::Data truncated(const cpputils::Data &data) {
  cpputils::Data result(data.size() - 1);
  std::memcpy(result.data(), data.data(), result.size());
  return result;
}

}

TEST(DirectoryRecordTest, RoundTripsDirectoryRecord) {
  const DirectoryRecord record = directoryRecord();

  const auto loaded = deserializeDirectoryRecord(serializeDirectoryRecord(record));

  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(record, *loaded);
}

TEST(DirectoryRecordTest, RoundTripsEmptyDirectory) {
  DirectoryRecord record = directoryRecord();
  record.entries.clear();

  const auto loaded = deserializeDirectoryRecord(serializeDirectoryRecord(record));

  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(record, *loaded);
}

TEST(DirectoryRecordTest, RejectsWrongHeader) {
  EXPECT_FALSE(deserializeDirectoryRecord(serializeStringOnly("not directory")).is_initialized());
}

TEST(DirectoryRecordTest, RejectsTruncatedRecord) {
  EXPECT_FALSE(deserializeDirectoryRecord(truncated(serializeDirectoryRecord(directoryRecord()))).is_initialized());
}

TEST(DirectoryRecordTest, RejectsRecordWithTrailingData) {
  EXPECT_FALSE(deserializeDirectoryRecord(withTrailingByte(serializeDirectoryRecord(directoryRecord()))).is_initialized());
}

TEST(DirectoryRecordTest, RejectsNullFilesystemId) {
  DirectoryRecord record = directoryRecord();
  record.filesystemId = FilesystemId::Null();

  EXPECT_THROW(serializeDirectoryRecord(record), std::runtime_error);
}

TEST(DirectoryRecordTest, RejectsNullDirectoryId) {
  DirectoryRecord record = directoryRecord();
  record.directoryId = ObjectId::Null();

  EXPECT_THROW(serializeDirectoryRecord(record), std::runtime_error);
}

TEST(DirectoryRecordTest, RejectsZeroGeneration) {
  DirectoryRecord record = directoryRecord();
  record.generation = 0;

  EXPECT_THROW(serializeDirectoryRecord(record), std::runtime_error);
}

TEST(DirectoryRecordTest, RejectsInvalidPermissions) {
  DirectoryRecord record = directoryRecord();
  record.metadata.permissions = 010000;

  EXPECT_THROW(serializeDirectoryRecord(record), std::runtime_error);
}

TEST(DirectoryRecordTest, RejectsInvalidTimestampNanoseconds) {
  DirectoryRecord record = directoryRecord();
  record.metadata.mtime.nanoseconds = 1000000000;

  EXPECT_THROW(serializeDirectoryRecord(record), std::runtime_error);
}

TEST(DirectoryRecordTest, RejectsInvalidEntryNames) {
  for (const char *name: {"", ".", "..", "a/b"}) {
    DirectoryRecord record = directoryRecord();
    record.entries[0].name = std::string(name);
    EXPECT_THROW(serializeDirectoryRecord(record), std::runtime_error);
  }

  DirectoryRecord record = directoryRecord();
  record.entries[0].name = std::string("a\0b", 3);
  EXPECT_THROW(serializeDirectoryRecord(record), std::runtime_error);
}

TEST(DirectoryRecordTest, RejectsDuplicateEntryNames) {
  DirectoryRecord record = directoryRecord();
  record.entries[1].name = record.entries[0].name;

  EXPECT_THROW(serializeDirectoryRecord(record), std::runtime_error);
}

TEST(DirectoryRecordTest, RejectsUnsortedEntryNames) {
  DirectoryRecord record = directoryRecord();
  std::swap(record.entries[0], record.entries[1]);

  EXPECT_THROW(serializeDirectoryRecord(record), std::runtime_error);
}

TEST(DirectoryRecordTest, RejectsUnknownEntryType) {
  DirectoryRecord record = directoryRecord();
  record.entries[0].type = static_cast<ObjectType>(99);

  EXPECT_THROW(serializeDirectoryRecord(record), std::runtime_error);
}

TEST(DirectoryRecordTest, RejectsNullEntryObjectId) {
  DirectoryRecord record = directoryRecord();
  record.entries[0].objectId = ObjectId::Null();

  EXPECT_THROW(serializeDirectoryRecord(record), std::runtime_error);
}

TEST(DirectoryRecordTest, RejectsZeroEntryGeneration) {
  DirectoryRecord record = directoryRecord();
  record.entries[0].generation = 0;

  EXPECT_THROW(serializeDirectoryRecord(record), std::runtime_error);
}

TEST(DirectoryRecordTest, MatchesExpectedObjectIdentity) {
  const DirectoryRecord record = directoryRecord();

  EXPECT_TRUE(directoryRecordMatchesObject(record, record.filesystemId, record.directoryId, record.generation));
}

TEST(DirectoryRecordTest, RejectsMismatchedObjectIdentity) {
  const DirectoryRecord record = directoryRecord();

  EXPECT_FALSE(directoryRecordMatchesObject(record, otherFilesystemId(), record.directoryId, record.generation));
  EXPECT_FALSE(directoryRecordMatchesObject(record, record.filesystemId, objectId(99), record.generation));
  EXPECT_FALSE(directoryRecordMatchesObject(record, record.filesystemId, record.directoryId, record.generation + 1));
}
