#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/FileRecord.h>

#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/data/Serializer.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

using cryfs::formatv2::FileDataRecord;
using cryfs::formatv2::FileExtent;
using cryfs::formatv2::FileMetadata;
using cryfs::formatv2::FileRecord;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::Timestamp;
using cryfs::formatv2::deserializeFileDataRecord;
using cryfs::formatv2::deserializeFileRecord;
using cryfs::formatv2::fileDataRecordMatchesObject;
using cryfs::formatv2::fileRecordMatchesObject;
using cryfs::formatv2::serializeFileDataRecord;
using cryfs::formatv2::serializeFileRecord;

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

FileDataRecord fileDataRecord() {
  return FileDataRecord{
    filesystemId(),
    objectId(2),
    3,
    cpputils::DataFixture::generate(32)
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

TEST(FileRecordTest, RoundTripsFileRecord) {
  const FileRecord record = fileRecord();

  const auto loaded = deserializeFileRecord(serializeFileRecord(record));

  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(record, *loaded);
}

TEST(FileRecordTest, RoundTripsEmptyFileRecord) {
  FileRecord record = fileRecord();
  record.size = 0;
  record.extents.clear();

  const auto loaded = deserializeFileRecord(serializeFileRecord(record));

  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(record, *loaded);
}

TEST(FileRecordTest, RoundTripsSparseFileRecord) {
  FileRecord record = fileRecord();
  record.extents = {extent(12, 4, 4, 5)};

  const auto loaded = deserializeFileRecord(serializeFileRecord(record));

  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(record, *loaded);
}

TEST(FileRecordTest, RoundTripsFileDataRecord) {
  const FileDataRecord record = fileDataRecord();

  const auto loaded = deserializeFileDataRecord(serializeFileDataRecord(record));

  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(record, *loaded);
}

TEST(FileRecordTest, RejectsWrongHeaders) {
  EXPECT_FALSE(deserializeFileRecord(serializeStringOnly("not file")).is_initialized());
  EXPECT_FALSE(deserializeFileDataRecord(serializeStringOnly("not file data")).is_initialized());
}

TEST(FileRecordTest, RejectsTruncatedRecords) {
  EXPECT_FALSE(deserializeFileRecord(truncated(serializeFileRecord(fileRecord()))).is_initialized());
  EXPECT_FALSE(deserializeFileDataRecord(truncated(serializeFileDataRecord(fileDataRecord()))).is_initialized());
}

TEST(FileRecordTest, RejectsRecordsWithTrailingData) {
  EXPECT_FALSE(deserializeFileRecord(withTrailingByte(serializeFileRecord(fileRecord()))).is_initialized());
  EXPECT_FALSE(deserializeFileDataRecord(withTrailingByte(serializeFileDataRecord(fileDataRecord()))).is_initialized());
}

TEST(FileRecordTest, RejectsInvalidFileIdentity) {
  FileRecord record = fileRecord();
  record.filesystemId = FilesystemId::Null();
  EXPECT_THROW(serializeFileRecord(record), std::runtime_error);

  record = fileRecord();
  record.fileId = ObjectId::Null();
  EXPECT_THROW(serializeFileRecord(record), std::runtime_error);

  record = fileRecord();
  record.generation = 0;
  EXPECT_THROW(serializeFileRecord(record), std::runtime_error);
}

TEST(FileRecordTest, RejectsInvalidFileDataIdentity) {
  FileDataRecord record = fileDataRecord();
  record.filesystemId = FilesystemId::Null();
  EXPECT_THROW(serializeFileDataRecord(record), std::runtime_error);

  record = fileDataRecord();
  record.dataId = ObjectId::Null();
  EXPECT_THROW(serializeFileDataRecord(record), std::runtime_error);

  record = fileDataRecord();
  record.generation = 0;
  EXPECT_THROW(serializeFileDataRecord(record), std::runtime_error);
}

TEST(FileRecordTest, RejectsInvalidMetadata) {
  FileRecord record = fileRecord();
  record.metadata.permissions = 010000;
  EXPECT_THROW(serializeFileRecord(record), std::runtime_error);

  record = fileRecord();
  record.metadata.ctime.nanoseconds = 1000000000;
  EXPECT_THROW(serializeFileRecord(record), std::runtime_error);
}

TEST(FileRecordTest, RejectsInvalidExtents) {
  FileRecord record = fileRecord();
  record.extents[0].size = 0;
  EXPECT_THROW(serializeFileRecord(record), std::runtime_error);

  record = fileRecord();
  record.extents[0].dataId = ObjectId::Null();
  EXPECT_THROW(serializeFileRecord(record), std::runtime_error);

  record = fileRecord();
  record.extents[0].generation = 0;
  EXPECT_THROW(serializeFileRecord(record), std::runtime_error);

  record = fileRecord();
  record.extents[1].offset = 15;
  record.extents[1].size = 2;
  EXPECT_THROW(serializeFileRecord(record), std::runtime_error);

  record = fileRecord();
  record.extents[1].offset = 3;
  EXPECT_THROW(serializeFileRecord(record), std::runtime_error);

  record = fileRecord();
  std::swap(record.extents[0], record.extents[1]);
  EXPECT_THROW(serializeFileRecord(record), std::runtime_error);
}

TEST(FileRecordTest, RejectsEmptyFileDataPayload) {
  FileDataRecord record{
    filesystemId(),
    objectId(2),
    3,
    cpputils::Data(0)
  };

  EXPECT_THROW(serializeFileDataRecord(record), std::runtime_error);
}

TEST(FileRecordTest, MatchesExpectedObjectIdentity) {
  const FileRecord record = fileRecord();
  const FileDataRecord data = fileDataRecord();

  EXPECT_TRUE(fileRecordMatchesObject(record, record.filesystemId, record.fileId, record.generation));
  EXPECT_TRUE(fileDataRecordMatchesObject(data, data.filesystemId, data.dataId, data.generation));
}

TEST(FileRecordTest, RejectsMismatchedObjectIdentity) {
  const FileRecord record = fileRecord();
  const FileDataRecord data = fileDataRecord();

  EXPECT_FALSE(fileRecordMatchesObject(record, otherFilesystemId(), record.fileId, record.generation));
  EXPECT_FALSE(fileRecordMatchesObject(record, record.filesystemId, objectId(99), record.generation));
  EXPECT_FALSE(fileRecordMatchesObject(record, record.filesystemId, record.fileId, record.generation + 1));

  EXPECT_FALSE(fileDataRecordMatchesObject(data, otherFilesystemId(), data.dataId, data.generation));
  EXPECT_FALSE(fileDataRecordMatchesObject(data, data.filesystemId, objectId(99), data.generation));
  EXPECT_FALSE(fileDataRecordMatchesObject(data, data.filesystemId, data.dataId, data.generation + 1));
}
