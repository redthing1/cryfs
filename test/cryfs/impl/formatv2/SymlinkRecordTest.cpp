#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/SymlinkRecord.h>

#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/data/Serializer.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::SymlinkMetadata;
using cryfs::formatv2::SymlinkRecord;
using cryfs::formatv2::Timestamp;
using cryfs::formatv2::deserializeSymlinkRecord;
using cryfs::formatv2::serializeSymlinkRecord;
using cryfs::formatv2::symlinkRecordMatchesObject;

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

TEST(SymlinkRecordTest, RoundTripsSymlinkRecord) {
  const SymlinkRecord record = symlinkRecord();

  const auto loaded = deserializeSymlinkRecord(serializeSymlinkRecord(record));

  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(record, *loaded);
}

TEST(SymlinkRecordTest, RejectsWrongHeader) {
  EXPECT_FALSE(deserializeSymlinkRecord(serializeStringOnly("not symlink")).is_initialized());
}

TEST(SymlinkRecordTest, RejectsTruncatedRecord) {
  EXPECT_FALSE(deserializeSymlinkRecord(truncated(serializeSymlinkRecord(symlinkRecord()))).is_initialized());
}

TEST(SymlinkRecordTest, RejectsRecordWithTrailingData) {
  EXPECT_FALSE(deserializeSymlinkRecord(withTrailingByte(serializeSymlinkRecord(symlinkRecord()))).is_initialized());
}

TEST(SymlinkRecordTest, RejectsInvalidIdentity) {
  SymlinkRecord record = symlinkRecord();
  record.filesystemId = FilesystemId::Null();
  EXPECT_THROW(serializeSymlinkRecord(record), std::runtime_error);

  record = symlinkRecord();
  record.symlinkId = ObjectId::Null();
  EXPECT_THROW(serializeSymlinkRecord(record), std::runtime_error);

  record = symlinkRecord();
  record.generation = 0;
  EXPECT_THROW(serializeSymlinkRecord(record), std::runtime_error);
}

TEST(SymlinkRecordTest, RejectsInvalidMetadata) {
  SymlinkRecord record = symlinkRecord();
  record.metadata.permissions = 010000;
  EXPECT_THROW(serializeSymlinkRecord(record), std::runtime_error);

  record = symlinkRecord();
  record.metadata.atime.nanoseconds = 1000000000;
  EXPECT_THROW(serializeSymlinkRecord(record), std::runtime_error);
}

TEST(SymlinkRecordTest, RejectsInvalidTargets) {
  SymlinkRecord record = symlinkRecord();
  record.target.clear();
  EXPECT_THROW(serializeSymlinkRecord(record), std::runtime_error);

  record = symlinkRecord();
  record.target = std::string("a\0b", 3);
  EXPECT_THROW(serializeSymlinkRecord(record), std::runtime_error);
}

TEST(SymlinkRecordTest, MatchesExpectedObjectIdentity) {
  const SymlinkRecord record = symlinkRecord();

  EXPECT_TRUE(symlinkRecordMatchesObject(record, record.filesystemId, record.symlinkId, record.generation));
}

TEST(SymlinkRecordTest, RejectsMismatchedObjectIdentity) {
  const SymlinkRecord record = symlinkRecord();

  EXPECT_FALSE(symlinkRecordMatchesObject(record, otherFilesystemId(), record.symlinkId, record.generation));
  EXPECT_FALSE(symlinkRecordMatchesObject(record, record.filesystemId, objectId(99), record.generation));
  EXPECT_FALSE(symlinkRecordMatchesObject(record, record.filesystemId, record.symlinkId, record.generation + 1));
}
