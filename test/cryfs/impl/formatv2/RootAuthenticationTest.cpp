#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/RootAuthentication.h>

#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/data/Serializer.h>

#include <cstdint>
#include <stdexcept>
#include <string>

using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::ROOT_AUTHENTICATION_KEY_SIZE;
using cryfs::formatv2::RootId;
using cryfs::formatv2::RootRecord;
using cryfs::formatv2::authenticateRootRecord;
using cryfs::formatv2::serializeRootRecord;
using cryfs::formatv2::verifyRootRecord;

namespace {

FilesystemId filesystemId(const std::string &value) {
  return FilesystemId::FromString(value);
}

RootId rootId(const std::string &value) {
  return RootId::FromString(value);
}

FilesystemId filesystemId() {
  return filesystemId("00112233445566778899AABBCCDDEEFF");
}

FilesystemId otherFilesystemId() {
  return filesystemId("FFEEDDCCBBAA99887766554433221100");
}

RootRecord rootRecord() {
  return RootRecord{
    filesystemId(),
    7,
    rootId("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF")
  };
}

cpputils::EncryptionKey authenticationKey(unsigned int seed = 1) {
  return cpputils::EncryptionKey::FromString(
    cpputils::DataFixture::generate(ROOT_AUTHENTICATION_KEY_SIZE, seed).ToString());
}

cpputils::Data serializeStringOnly(const std::string &value) {
  cpputils::Serializer serializer(cpputils::Serializer::StringSize(value));
  serializer.writeString(value);
  return serializer.finished();
}

void flipByte(cpputils::Data *data, size_t offset) {
  auto *byte = static_cast<uint8_t*>(data->dataOffset(offset));
  *byte ^= 1;
}

}

TEST(RootAuthenticationTest, AuthenticatesAndVerifiesRootRecord) {
  const RootRecord record = rootRecord();

  const auto verified = verifyRootRecord(
    authenticateRootRecord(record, authenticationKey()),
    authenticationKey(),
    record.filesystemId);

  ASSERT_TRUE(verified.is_initialized());
  EXPECT_EQ(record.epoch, verified->epoch);
  EXPECT_EQ(record.rootId, verified->rootId);
}

TEST(RootAuthenticationTest, AuthenticationIsDeterministicForSameRecordAndKey) {
  const RootRecord record = rootRecord();

  const cpputils::Data first = authenticateRootRecord(record, authenticationKey());
  const cpputils::Data second = authenticateRootRecord(record, authenticationKey());

  EXPECT_EQ(first, second);
}

TEST(RootAuthenticationTest, RejectsWrongAuthenticationKey) {
  const RootRecord record = rootRecord();
  const cpputils::Data authenticated = authenticateRootRecord(record, authenticationKey(1));

  EXPECT_FALSE(verifyRootRecord(authenticated, authenticationKey(2), record.filesystemId).is_initialized());
}

TEST(RootAuthenticationTest, RejectsModifiedAuthenticatedRecord) {
  const RootRecord record = rootRecord();
  cpputils::Data authenticated = authenticateRootRecord(record, authenticationKey());
  flipByte(&authenticated, authenticated.size() - 1);

  EXPECT_FALSE(verifyRootRecord(authenticated, authenticationKey(), record.filesystemId).is_initialized());
}

TEST(RootAuthenticationTest, RejectsWrongFilesystemId) {
  const RootRecord record = rootRecord();
  const cpputils::Data authenticated = authenticateRootRecord(record, authenticationKey());

  EXPECT_FALSE(verifyRootRecord(authenticated, authenticationKey(), otherFilesystemId()).is_initialized());
}

TEST(RootAuthenticationTest, RejectsMalformedEnvelope) {
  EXPECT_FALSE(verifyRootRecord(
    serializeStringOnly("cryfs.formatv2.authenticated-root;0"),
    authenticationKey(),
    filesystemId()).is_initialized());
}

TEST(RootAuthenticationTest, RejectsUnauthenticatedRootRecordBytes) {
  const RootRecord record = rootRecord();

  EXPECT_FALSE(verifyRootRecord(
    serializeRootRecord(record),
    authenticationKey(),
    record.filesystemId).is_initialized());
}

TEST(RootAuthenticationTest, RejectsWrongAuthenticationKeySize) {
  const cpputils::EncryptionKey shortKey = cpputils::EncryptionKey::Null(ROOT_AUTHENTICATION_KEY_SIZE - 1);

  EXPECT_THROW(authenticateRootRecord(rootRecord(), shortKey), std::runtime_error);
  EXPECT_THROW(verifyRootRecord(serializeStringOnly("anything"), shortKey, filesystemId()), std::runtime_error);
}
