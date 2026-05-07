#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/ObjectRecord.h>

#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/data/Serializer.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::OBJECT_ENCRYPTION_KEY_SIZE;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::ObjectRecordType;
using cryfs::formatv2::decryptObjectRecordPayload;
using cryfs::formatv2::deriveObjectEncryptionKey;
using cryfs::formatv2::encryptObjectRecordPayload;

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

ObjectId objectId(const std::string &value) {
  return ObjectId::FromString(value);
}

ObjectId objectId() {
  return objectId("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
}

ObjectId otherObjectId() {
  return objectId("FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210");
}

cpputils::EncryptionKey masterKey(unsigned int seed = 1) {
  return cpputils::EncryptionKey::FromString(
    cpputils::DataFixture::generate(FORMAT_V2_MASTER_KEY_SIZE, seed).ToString());
}

cpputils::EncryptionKey objectEncryptionKey(unsigned int seed = 1) {
  return deriveObjectEncryptionKey(masterKey(seed), filesystemId());
}

cpputils::Data payload() {
  return cpputils::DataFixture::generate(64);
}

cpputils::Data encryptedObjectRecord(const cpputils::Data &recordPayload = payload()) {
  return encryptObjectRecordPayload(
    recordPayload,
    objectEncryptionKey(),
    filesystemId(),
    ObjectRecordType::RootContent,
    objectId(),
    7);
}

cpputils::Data withFlippedByte(cpputils::Data data, size_t offset) {
  auto *byte = static_cast<uint8_t*>(data.dataOffset(offset));
  *byte ^= 1;
  return data;
}

cpputils::Data serializeStringOnly(const std::string &value) {
  cpputils::Serializer serializer(cpputils::Serializer::StringSize(value));
  serializer.writeString(value);
  return serializer.finished();
}

}

TEST(ObjectRecordTest, EncryptsAndDecryptsPayload) {
  const cpputils::Data recordPayload = payload();
  const cpputils::Data encrypted = encryptedObjectRecord(recordPayload);

  const auto decrypted = decryptObjectRecordPayload(
    encrypted,
    objectEncryptionKey(),
    filesystemId(),
    ObjectRecordType::RootContent,
    objectId(),
    7);

  ASSERT_TRUE(decrypted.is_initialized());
  EXPECT_EQ(recordPayload, *decrypted);
}

TEST(ObjectRecordTest, SupportsEmptyPayload) {
  const cpputils::Data emptyPayload(0);

  const auto decrypted = decryptObjectRecordPayload(
    encryptedObjectRecord(emptyPayload),
    objectEncryptionKey(),
    filesystemId(),
    ObjectRecordType::RootContent,
    objectId(),
    7);

  ASSERT_TRUE(decrypted.is_initialized());
  EXPECT_EQ(emptyPayload, *decrypted);
}

TEST(ObjectRecordTest, SupportsEveryKnownRecordType) {
  const cpputils::Data recordPayload = payload();
  const std::array<ObjectRecordType, 5> types{
    ObjectRecordType::RootContent,
    ObjectRecordType::Directory,
    ObjectRecordType::File,
    ObjectRecordType::Symlink,
    ObjectRecordType::FileData
  };

  for (const ObjectRecordType type: types) {
    const cpputils::Data encrypted = encryptObjectRecordPayload(
      recordPayload,
      objectEncryptionKey(),
      filesystemId(),
      type,
      objectId(),
      7);

    const auto decrypted = decryptObjectRecordPayload(
      encrypted,
      objectEncryptionKey(),
      filesystemId(),
      type,
      objectId(),
      7);

    ASSERT_TRUE(decrypted.is_initialized());
    EXPECT_EQ(recordPayload, *decrypted);
  }
}

TEST(ObjectRecordTest, CiphertextDoesNotExposePayloadBytes) {
  const cpputils::Data recordPayload = payload();
  const cpputils::Data encrypted = encryptedObjectRecord(recordPayload);

  EXPECT_NE(recordPayload, encrypted);
}

TEST(ObjectRecordTest, RejectsWrongObjectEncryptionKey) {
  EXPECT_FALSE(decryptObjectRecordPayload(
    encryptedObjectRecord(),
    objectEncryptionKey(2),
    filesystemId(),
    ObjectRecordType::RootContent,
    objectId(),
    7).is_initialized());
}

TEST(ObjectRecordTest, RejectsTamperedEncryptedRecord) {
  cpputils::Data encrypted = encryptedObjectRecord();
  const size_t lastByte = encrypted.size() - 1;
  const cpputils::Data tampered = withFlippedByte(std::move(encrypted), lastByte);

  EXPECT_FALSE(decryptObjectRecordPayload(
    tampered,
    objectEncryptionKey(),
    filesystemId(),
    ObjectRecordType::RootContent,
    objectId(),
    7).is_initialized());
}

TEST(ObjectRecordTest, RejectsMalformedEnvelope) {
  EXPECT_FALSE(decryptObjectRecordPayload(
    serializeStringOnly("not an object record"),
    objectEncryptionKey(),
    filesystemId(),
    ObjectRecordType::RootContent,
    objectId(),
    7).is_initialized());
}

TEST(ObjectRecordTest, RejectsWrongFilesystemId) {
  EXPECT_FALSE(decryptObjectRecordPayload(
    encryptedObjectRecord(),
    objectEncryptionKey(),
    otherFilesystemId(),
    ObjectRecordType::RootContent,
    objectId(),
    7).is_initialized());
}

TEST(ObjectRecordTest, RejectsWrongObjectId) {
  EXPECT_FALSE(decryptObjectRecordPayload(
    encryptedObjectRecord(),
    objectEncryptionKey(),
    filesystemId(),
    ObjectRecordType::RootContent,
    otherObjectId(),
    7).is_initialized());
}

TEST(ObjectRecordTest, RejectsWrongGeneration) {
  EXPECT_FALSE(decryptObjectRecordPayload(
    encryptedObjectRecord(),
    objectEncryptionKey(),
    filesystemId(),
    ObjectRecordType::RootContent,
    objectId(),
    8).is_initialized());
}

TEST(ObjectRecordTest, RejectsWrongObjectEncryptionKeySize) {
  const cpputils::EncryptionKey shortKey = cpputils::EncryptionKey::Null(OBJECT_ENCRYPTION_KEY_SIZE - 1);

  EXPECT_THROW(
    encryptObjectRecordPayload(payload(), shortKey, filesystemId(), ObjectRecordType::RootContent, objectId(), 7),
    std::runtime_error);
  EXPECT_THROW(
    decryptObjectRecordPayload(encryptedObjectRecord(), shortKey, filesystemId(), ObjectRecordType::RootContent, objectId(), 7),
    std::runtime_error);
}

TEST(ObjectRecordTest, RejectsInvalidRecordIdentity) {
  EXPECT_THROW(
    encryptObjectRecordPayload(payload(), objectEncryptionKey(), FilesystemId::Null(), ObjectRecordType::RootContent, objectId(), 7),
    std::runtime_error);
  EXPECT_THROW(
    encryptObjectRecordPayload(payload(), objectEncryptionKey(), filesystemId(), ObjectRecordType::RootContent, ObjectId::Null(), 7),
    std::runtime_error);
  EXPECT_THROW(
    encryptObjectRecordPayload(payload(), objectEncryptionKey(), filesystemId(), ObjectRecordType::RootContent, objectId(), 0),
    std::runtime_error);
}

TEST(ObjectRecordTest, RejectsUnknownRecordType) {
  const auto unknownType = static_cast<ObjectRecordType>(99);

  EXPECT_THROW(
    encryptObjectRecordPayload(payload(), objectEncryptionKey(), filesystemId(), unknownType, objectId(), 7),
    std::runtime_error);
  EXPECT_THROW(
    decryptObjectRecordPayload(encryptedObjectRecord(), objectEncryptionKey(), filesystemId(), unknownType, objectId(), 7),
    std::runtime_error);
}
