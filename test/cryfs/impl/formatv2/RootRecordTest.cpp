#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/RootRecord.h>

#include <cpp-utils/data/Data.h>
#include <cpp-utils/data/Serializer.h>

#include <cstring>
#include <stdexcept>
#include <string>

using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::RootId;
using cryfs::formatv2::RootRecord;
using cryfs::formatv2::deserializeRootRecord;
using cryfs::formatv2::serializeRootRecord;
using cryfs::formatv2::trustedRootFromRecord;

namespace {

FilesystemId filesystemId(const std::string &value) {
  return FilesystemId::FromString(value);
}

RootId rootId(const std::string &value) {
  return RootId::FromString(value);
}

RootRecord rootRecord() {
  return RootRecord{
    filesystemId("00112233445566778899AABBCCDDEEFF"),
    7,
    rootId("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF")
  };
}

cpputils::Data serializeStringOnly(const std::string &value) {
  cpputils::Serializer serializer(cpputils::Serializer::StringSize(value));
  serializer.writeString(value);
  return serializer.finished();
}

}

TEST(RootRecordTest, RoundTripsRootRecord) {
  const RootRecord original = rootRecord();

  const auto parsed = deserializeRootRecord(serializeRootRecord(original));

  ASSERT_TRUE(parsed.is_initialized());
  EXPECT_EQ(original, *parsed);
}

TEST(RootRecordTest, RejectsWrongHeader) {
  EXPECT_FALSE(deserializeRootRecord(serializeStringOnly("cryfs.formatv2.other;0")).is_initialized());
}

TEST(RootRecordTest, RejectsTruncatedRecord) {
  const auto serialized = serializeRootRecord(rootRecord());
  cpputils::Data truncated(serialized.size() - 1);
  std::memcpy(truncated.data(), serialized.data(), truncated.size());

  EXPECT_FALSE(deserializeRootRecord(truncated).is_initialized());
}

TEST(RootRecordTest, RejectsRecordWithTrailingData) {
  const auto serialized = serializeRootRecord(rootRecord());
  cpputils::Data withTrailingByte(serialized.size() + 1);
  std::memcpy(withTrailingByte.data(), serialized.data(), serialized.size());
  static_cast<char*>(withTrailingByte.data())[serialized.size()] = 0;

  EXPECT_FALSE(deserializeRootRecord(withTrailingByte).is_initialized());
}

TEST(RootRecordTest, RejectsZeroEpoch) {
  RootRecord record = rootRecord();
  record.epoch = 0;

  EXPECT_THROW(serializeRootRecord(record), std::runtime_error);
}

TEST(RootRecordTest, RejectsNullRootId) {
  RootRecord record = rootRecord();
  record.rootId = RootId::Null();

  EXPECT_THROW(serializeRootRecord(record), std::runtime_error);
}

TEST(RootRecordTest, TrustedRootUsesEpochAndRootIdWhenFilesystemMatches) {
  const RootRecord record = rootRecord();

  const auto trustedRoot = trustedRootFromRecord(record, record.filesystemId);

  ASSERT_TRUE(trustedRoot.is_initialized());
  EXPECT_EQ(record.epoch, trustedRoot->epoch);
  EXPECT_EQ(record.rootId, trustedRoot->rootId);
}

TEST(RootRecordTest, TrustedRootRejectsWrongFilesystem) {
  const RootRecord record = rootRecord();
  const auto otherFilesystemId = filesystemId("FFEEDDCCBBAA99887766554433221100");

  EXPECT_FALSE(trustedRootFromRecord(record, otherFilesystemId).is_initialized());
}
