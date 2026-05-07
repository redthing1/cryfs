#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/RootOpenWithContent.h>

#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/data/Serializer.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstdint>
#include <stdexcept>
#include <string>

using cryfs::formatv2::AcceptedRootStateStore;
using cryfs::formatv2::AuthenticatedRoot;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::RootContent;
using cryfs::formatv2::RootContentStore;
using cryfs::formatv2::RootId;
using cryfs::formatv2::RootOpenStatus;
using cryfs::formatv2::RootPublicationStore;
using cryfs::formatv2::RootRecord;
using cryfs::formatv2::deriveObjectEncryptionKey;
using cryfs::formatv2::deriveRootAuthenticationKey;
using cryfs::formatv2::openRootWithContent;
using cryfs::formatv2::selectRootWithContent;

namespace {

FilesystemId filesystemId() {
  return FilesystemId::FromString("00112233445566778899AABBCCDDEEFF");
}

RootId rootId(unsigned int seed) {
  return cpputils::DataFixture::generateFixedSize<RootId::BINARY_LENGTH>(seed);
}

ObjectId objectId(unsigned int seed) {
  return cpputils::DataFixture::generateFixedSize<ObjectId::BINARY_LENGTH>(seed);
}

cpputils::EncryptionKey masterKey(unsigned int seed = 1) {
  return cpputils::EncryptionKey::FromString(
    cpputils::DataFixture::generate(FORMAT_V2_MASTER_KEY_SIZE, seed).ToString());
}

cpputils::EncryptionKey rootAuthenticationKey(unsigned int seed = 1) {
  return deriveRootAuthenticationKey(masterKey(seed), filesystemId());
}

cpputils::EncryptionKey objectEncryptionKey(unsigned int seed = 1) {
  return deriveObjectEncryptionKey(masterKey(seed), filesystemId());
}

RootRecord rootRecord(uint64_t epoch, unsigned int rootIdSeed) {
  return RootRecord{filesystemId(), epoch, rootId(rootIdSeed)};
}

RootContent rootContent(const RootRecord &record, unsigned int rootDirectoryIdSeed = 11) {
  return RootContent{record.filesystemId, record.epoch, record.rootId, objectId(rootDirectoryIdSeed), 3};
}

AuthenticatedRoot authenticatedRoot(const RootRecord &record) {
  return AuthenticatedRoot{record.epoch, record.rootId};
}

cpputils::Data serializeStringOnly(const std::string &value) {
  cpputils::Serializer serializer(cpputils::Serializer::StringSize(value));
  serializer.writeString(value);
  return serializer.finished();
}

class RootOpenWithContentTestFixture : public ::testing::Test {
public:
  RootOpenWithContentTestFixture()
    : publicationStore(_tempDir.path() / "roots", filesystemId(), rootAuthenticationKey()),
      acceptedRootStateStore(_tempDir.path() / "accepted-root", filesystemId()),
      rootContentStore(_tempDir.path() / "root-content", filesystemId(), objectEncryptionKey()) {
  }

protected:
  cpputils::TempDir _tempDir;
  RootPublicationStore publicationStore;
  AcceptedRootStateStore acceptedRootStateStore;
  RootContentStore rootContentStore;
};

}

TEST_F(RootOpenWithContentTestFixture, SelectsRootLoadsContentAndAdvancesAcceptedState) {
  const RootRecord record = rootRecord(1, 1);
  const RootContent content = rootContent(record);
  publicationStore.publish(record);
  rootContentStore.store(content);

  const auto result = openRootWithContent(publicationStore, acceptedRootStateStore, rootContentStore);

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootContent.is_initialized());
  EXPECT_EQ(authenticatedRoot(record), *result.selectedRoot);
  EXPECT_EQ(content, *result.rootContent);
  EXPECT_EQ(authenticatedRoot(record), *acceptedRootStateStore.load());
}

TEST_F(RootOpenWithContentTestFixture, SelectsRootWithContentWithoutAdvancingAcceptedState) {
  const RootRecord record = rootRecord(1, 1);
  const RootContent content = rootContent(record);
  publicationStore.publish(record);
  rootContentStore.store(content);

  const auto result = selectRootWithContent(publicationStore, acceptedRootStateStore, rootContentStore);

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootContent.is_initialized());
  EXPECT_EQ(authenticatedRoot(record), *result.selectedRoot);
  EXPECT_EQ(content, *result.rootContent);
  EXPECT_FALSE(acceptedRootStateStore.load().is_initialized());
}

TEST_F(RootOpenWithContentTestFixture, SelectsNewerRootLoadsContentAndAdvancesAcceptedState) {
  const RootRecord accepted = rootRecord(1, 1);
  const RootRecord published = rootRecord(2, 2);
  const RootContent content = rootContent(published);
  acceptedRootStateStore.store(authenticatedRoot(accepted));
  publicationStore.publish(published);
  rootContentStore.store(content);

  const auto result = openRootWithContent(publicationStore, acceptedRootStateStore, rootContentStore);

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.rootContent.is_initialized());
  EXPECT_EQ(content, *result.rootContent);
  EXPECT_EQ(authenticatedRoot(published), *acceptedRootStateStore.load());
}

TEST_F(RootOpenWithContentTestFixture, DoesNotAdvanceAcceptedStateWhenContentIsMissing) {
  const RootRecord accepted = rootRecord(1, 1);
  const RootRecord published = rootRecord(2, 2);
  acceptedRootStateStore.store(authenticatedRoot(accepted));
  publicationStore.publish(published);

  const auto result = openRootWithContent(publicationStore, acceptedRootStateStore, rootContentStore);

  EXPECT_EQ(RootOpenStatus::RootContentUnavailable, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  EXPECT_EQ(authenticatedRoot(published), *result.selectedRoot);
  EXPECT_FALSE(result.rootContent.is_initialized());
  EXPECT_EQ(authenticatedRoot(accepted), *acceptedRootStateStore.load());
}

TEST_F(RootOpenWithContentTestFixture, DoesNotAdvanceAcceptedStateWhenContentIsEncryptedWithWrongKey) {
  const RootRecord accepted = rootRecord(1, 1);
  const RootRecord published = rootRecord(2, 2);
  acceptedRootStateStore.store(authenticatedRoot(accepted));
  publicationStore.publish(published);

  RootContentStore(_tempDir.path() / "root-content", filesystemId(), objectEncryptionKey(2))
    .store(rootContent(published));

  const auto result = openRootWithContent(publicationStore, acceptedRootStateStore, rootContentStore);

  EXPECT_EQ(RootOpenStatus::RootContentUnavailable, result.status);
  EXPECT_FALSE(result.rootContent.is_initialized());
  EXPECT_EQ(authenticatedRoot(accepted), *acceptedRootStateStore.load());
}

TEST_F(RootOpenWithContentTestFixture, DoesNotChangeAcceptedStateWhenSelectionFails) {
  const RootRecord accepted = rootRecord(2, 2);
  const RootRecord published = rootRecord(1, 1);
  acceptedRootStateStore.store(authenticatedRoot(accepted));
  publicationStore.publish(published);
  rootContentStore.store(rootContent(published));

  const auto result = openRootWithContent(publicationStore, acceptedRootStateStore, rootContentStore);

  EXPECT_EQ(RootOpenStatus::RollbackDetected, result.status);
  EXPECT_FALSE(result.selectedRoot.is_initialized());
  EXPECT_FALSE(result.rootContent.is_initialized());
  EXPECT_EQ(authenticatedRoot(accepted), *acceptedRootStateStore.load());
}

TEST_F(RootOpenWithContentTestFixture, FailsWithoutAuthenticatedRoots) {
  const auto result = openRootWithContent(publicationStore, acceptedRootStateStore, rootContentStore);

  EXPECT_EQ(RootOpenStatus::NoAuthenticatedRoots, result.status);
  EXPECT_FALSE(result.selectedRoot.is_initialized());
  EXPECT_FALSE(result.rootContent.is_initialized());
  EXPECT_FALSE(acceptedRootStateStore.load().is_initialized());
}

TEST_F(RootOpenWithContentTestFixture, ThrowsForMalformedAcceptedRootState) {
  const RootRecord record = rootRecord(1, 1);
  publicationStore.publish(record);
  rootContentStore.store(rootContent(record));
  serializeStringOnly("not accepted-root state").StoreToFile(_tempDir.path() / "accepted-root");

  EXPECT_THROW(openRootWithContent(publicationStore, acceptedRootStateStore, rootContentStore), std::runtime_error);
}
