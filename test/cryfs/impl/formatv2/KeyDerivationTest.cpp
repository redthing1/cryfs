#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/ObjectRecord.h>
#include <cryfs/impl/formatv2/RootAuthentication.h>

#include <cpp-utils/data/DataFixture.h>

#include <stdexcept>
#include <string>

using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::OBJECT_ENCRYPTION_KEY_SIZE;
using cryfs::formatv2::ROOT_AUTHENTICATION_KEY_SIZE;
using cryfs::formatv2::RootId;
using cryfs::formatv2::RootRecord;
using cryfs::formatv2::authenticateRootRecord;
using cryfs::formatv2::deriveObjectEncryptionKey;
using cryfs::formatv2::deriveRootAuthenticationKey;
using cryfs::formatv2::verifyRootRecord;

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

RootId rootId() {
  return RootId::FromString("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
}

RootRecord rootRecord() {
  return RootRecord{filesystemId(), 1, rootId()};
}

cpputils::EncryptionKey masterKey(unsigned int seed = 1) {
  return cpputils::EncryptionKey::FromString(
    cpputils::DataFixture::generate(FORMAT_V2_MASTER_KEY_SIZE, seed).ToString());
}

}

TEST(KeyDerivationTest, DerivesRootAuthenticationKeyWithExpectedSize) {
  const cpputils::EncryptionKey key = deriveRootAuthenticationKey(masterKey(), filesystemId());

  EXPECT_EQ(ROOT_AUTHENTICATION_KEY_SIZE, key.binaryLength());
}

TEST(KeyDerivationTest, DerivesObjectEncryptionKeyWithExpectedSize) {
  const cpputils::EncryptionKey key = deriveObjectEncryptionKey(masterKey(), filesystemId());

  EXPECT_EQ(OBJECT_ENCRYPTION_KEY_SIZE, key.binaryLength());
}

TEST(KeyDerivationTest, RootAuthenticationKeyDerivationIsDeterministic) {
  const cpputils::EncryptionKey first = deriveRootAuthenticationKey(masterKey(), filesystemId());
  const cpputils::EncryptionKey second = deriveRootAuthenticationKey(masterKey(), filesystemId());

  EXPECT_EQ(first.ToString(), second.ToString());
}

TEST(KeyDerivationTest, RootAuthenticationKeyDependsOnMasterKey) {
  const cpputils::EncryptionKey first = deriveRootAuthenticationKey(masterKey(1), filesystemId());
  const cpputils::EncryptionKey second = deriveRootAuthenticationKey(masterKey(2), filesystemId());

  EXPECT_NE(first.ToString(), second.ToString());
}

TEST(KeyDerivationTest, RootAuthenticationKeyDependsOnFilesystemId) {
  const cpputils::EncryptionKey first = deriveRootAuthenticationKey(masterKey(), filesystemId());
  const cpputils::EncryptionKey second = deriveRootAuthenticationKey(masterKey(), otherFilesystemId());

  EXPECT_NE(first.ToString(), second.ToString());
}

TEST(KeyDerivationTest, ObjectEncryptionKeyDerivationIsDeterministic) {
  const cpputils::EncryptionKey first = deriveObjectEncryptionKey(masterKey(), filesystemId());
  const cpputils::EncryptionKey second = deriveObjectEncryptionKey(masterKey(), filesystemId());

  EXPECT_EQ(first.ToString(), second.ToString());
}

TEST(KeyDerivationTest, ObjectEncryptionKeyDependsOnMasterKey) {
  const cpputils::EncryptionKey first = deriveObjectEncryptionKey(masterKey(1), filesystemId());
  const cpputils::EncryptionKey second = deriveObjectEncryptionKey(masterKey(2), filesystemId());

  EXPECT_NE(first.ToString(), second.ToString());
}

TEST(KeyDerivationTest, ObjectEncryptionKeyDependsOnFilesystemId) {
  const cpputils::EncryptionKey first = deriveObjectEncryptionKey(masterKey(), filesystemId());
  const cpputils::EncryptionKey second = deriveObjectEncryptionKey(masterKey(), otherFilesystemId());

  EXPECT_NE(first.ToString(), second.ToString());
}

TEST(KeyDerivationTest, RootAuthenticationKeyAndObjectEncryptionKeyAreSeparated) {
  const cpputils::EncryptionKey rootAuthenticationKey = deriveRootAuthenticationKey(masterKey(), filesystemId());
  const cpputils::EncryptionKey objectEncryptionKey = deriveObjectEncryptionKey(masterKey(), filesystemId());

  EXPECT_NE(rootAuthenticationKey.ToString(), objectEncryptionKey.ToString());
}

TEST(KeyDerivationTest, DerivedRootAuthenticationKeyAuthenticatesRootRecords) {
  const RootRecord record = rootRecord();
  const cpputils::EncryptionKey key = deriveRootAuthenticationKey(masterKey(), record.filesystemId);
  const cpputils::Data authenticated = authenticateRootRecord(record, key);

  EXPECT_TRUE(verifyRootRecord(authenticated, key, record.filesystemId).is_initialized());
}

TEST(KeyDerivationTest, RejectsWrongMasterKeySize) {
  const cpputils::EncryptionKey shortKey = cpputils::EncryptionKey::Null(FORMAT_V2_MASTER_KEY_SIZE - 1);
  const cpputils::EncryptionKey longKey = cpputils::EncryptionKey::Null(FORMAT_V2_MASTER_KEY_SIZE + 1);

  EXPECT_THROW(deriveRootAuthenticationKey(shortKey, filesystemId()), std::runtime_error);
  EXPECT_THROW(deriveRootAuthenticationKey(longKey, filesystemId()), std::runtime_error);
  EXPECT_THROW(deriveObjectEncryptionKey(shortKey, filesystemId()), std::runtime_error);
  EXPECT_THROW(deriveObjectEncryptionKey(longKey, filesystemId()), std::runtime_error);
}

TEST(KeyDerivationTest, RejectsNullFilesystemId) {
  EXPECT_THROW(deriveRootAuthenticationKey(masterKey(), FilesystemId::Null()), std::runtime_error);
  EXPECT_THROW(deriveObjectEncryptionKey(masterKey(), FilesystemId::Null()), std::runtime_error);
}
