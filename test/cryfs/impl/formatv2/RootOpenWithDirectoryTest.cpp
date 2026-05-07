#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/RootOpenWithDirectory.h>

#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/data/Serializer.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstdint>
#include <stdexcept>
#include <string>

using cryfs::formatv2::AcceptedRootStateStore;
using cryfs::formatv2::AuthenticatedRoot;
using cryfs::formatv2::DirectoryEntry;
using cryfs::formatv2::DirectoryMetadata;
using cryfs::formatv2::DirectoryRecord;
using cryfs::formatv2::DirectoryRecordStore;
using cryfs::formatv2::FileMetadata;
using cryfs::formatv2::FileRecord;
using cryfs::formatv2::FileRecordStore;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::ObjectTreeRecordType;
using cryfs::formatv2::ObjectTreeValidationStatus;
using cryfs::formatv2::ObjectType;
using cryfs::formatv2::RootContent;
using cryfs::formatv2::RootContentStore;
using cryfs::formatv2::RootId;
using cryfs::formatv2::RootOpenStatus;
using cryfs::formatv2::RootPublicationStore;
using cryfs::formatv2::RootRecord;
using cryfs::formatv2::SymlinkMetadata;
using cryfs::formatv2::SymlinkRecord;
using cryfs::formatv2::SymlinkRecordStore;
using cryfs::formatv2::Timestamp;
using cryfs::formatv2::deriveObjectEncryptionKey;
using cryfs::formatv2::deriveRootAuthenticationKey;
using cryfs::formatv2::openRootWithValidatedTree;
using cryfs::formatv2::selectRootWithDirectory;

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

FileMetadata fileMetadata() {
  return FileMetadata{
    0644,
    1000,
    1001,
    timestamp(20, 21),
    timestamp(22, 23),
    timestamp(24, 25)
  };
}

SymlinkMetadata symlinkMetadata() {
  return SymlinkMetadata{
    0777,
    1000,
    1001,
    timestamp(30, 31),
    timestamp(32, 33),
    timestamp(34, 35)
  };
}

DirectoryRecord rootDirectoryRecord(const RootContent &content) {
  return DirectoryRecord{
    content.filesystemId,
    content.rootDirectoryId,
    content.rootDirectoryGeneration,
    metadata(),
    {
      DirectoryEntry{"alpha", ObjectType::File, objectId(21), 4},
      DirectoryEntry{"beta", ObjectType::Symlink, objectId(22), 5}
    }
  };
}

FileRecord fileRecord() {
  return FileRecord{
    filesystemId(),
    objectId(21),
    4,
    fileMetadata(),
    0,
    {}
  };
}

SymlinkRecord symlinkRecord() {
  return SymlinkRecord{
    filesystemId(),
    objectId(22),
    5,
    symlinkMetadata(),
    "../target"
  };
}

cpputils::Data serializeStringOnly(const std::string &value) {
  cpputils::Serializer serializer(cpputils::Serializer::StringSize(value));
  serializer.writeString(value);
  return serializer.finished();
}

class RootOpenWithDirectoryTestFixture : public ::testing::Test {
public:
  RootOpenWithDirectoryTestFixture()
    : publicationStore(_tempDir.path() / "roots", filesystemId(), rootAuthenticationKey()),
      acceptedRootStateStore(_tempDir.path() / "accepted-root", filesystemId()),
      rootContentStore(_tempDir.path() / "root-content", filesystemId(), objectEncryptionKey()),
      directoryRecordStore(_tempDir.path() / "directories", filesystemId(), objectEncryptionKey()),
      fileRecordStore(_tempDir.path() / "files", filesystemId(), objectEncryptionKey()),
      symlinkRecordStore(_tempDir.path() / "symlinks", filesystemId(), objectEncryptionKey()) {
  }

protected:
  void publishRootWithDirectory(const RootRecord &record) const {
    const RootContent content = rootContent(record);
    publicationStore.publish(record);
    rootContentStore.store(content);
    directoryRecordStore.store(rootDirectoryRecord(content));
  }

  void storeReachableRootTree() const {
    fileRecordStore.store(fileRecord());
    symlinkRecordStore.store(symlinkRecord());
  }

  cpputils::TempDir _tempDir;
  RootPublicationStore publicationStore;
  AcceptedRootStateStore acceptedRootStateStore;
  RootContentStore rootContentStore;
  DirectoryRecordStore directoryRecordStore;
  FileRecordStore fileRecordStore;
  SymlinkRecordStore symlinkRecordStore;
};

}

TEST_F(RootOpenWithDirectoryTestFixture, SelectsRootLoadsContentDirectoryWithoutAdvancingAcceptedState) {
  const RootRecord record = rootRecord(1, 1);
  const RootContent content = rootContent(record);
  const DirectoryRecord directory = rootDirectoryRecord(content);
  publicationStore.publish(record);
  rootContentStore.store(content);
  directoryRecordStore.store(directory);

  const auto result = selectRootWithDirectory(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore);

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootContent.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  EXPECT_EQ(authenticatedRoot(record), *result.selectedRoot);
  EXPECT_EQ(content, *result.rootContent);
  EXPECT_EQ(directory, *result.rootDirectory);
  EXPECT_FALSE(acceptedRootStateStore.load().is_initialized());
}

TEST_F(RootOpenWithDirectoryTestFixture, SelectsNewerRootLoadsDirectoryWithoutAdvancingAcceptedState) {
  const RootRecord accepted = rootRecord(1, 1);
  const RootRecord published = rootRecord(2, 2);
  const RootContent content = rootContent(published);
  acceptedRootStateStore.store(authenticatedRoot(accepted));
  publicationStore.publish(published);
  rootContentStore.store(content);
  directoryRecordStore.store(rootDirectoryRecord(content));

  const auto result = selectRootWithDirectory(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore);

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  EXPECT_EQ(authenticatedRoot(accepted), *acceptedRootStateStore.load());
}

TEST_F(RootOpenWithDirectoryTestFixture, DoesNotAdvanceAcceptedStateWhenContentIsMissing) {
  const RootRecord accepted = rootRecord(1, 1);
  const RootRecord published = rootRecord(2, 2);
  acceptedRootStateStore.store(authenticatedRoot(accepted));
  publicationStore.publish(published);

  const auto result = selectRootWithDirectory(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore);

  EXPECT_EQ(RootOpenStatus::RootContentUnavailable, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  EXPECT_EQ(authenticatedRoot(published), *result.selectedRoot);
  EXPECT_FALSE(result.rootContent.is_initialized());
  EXPECT_FALSE(result.rootDirectory.is_initialized());
  EXPECT_EQ(authenticatedRoot(accepted), *acceptedRootStateStore.load());
}

TEST_F(RootOpenWithDirectoryTestFixture, DoesNotAdvanceAcceptedStateWhenDirectoryIsMissing) {
  const RootRecord accepted = rootRecord(1, 1);
  const RootRecord published = rootRecord(2, 2);
  const RootContent content = rootContent(published);
  acceptedRootStateStore.store(authenticatedRoot(accepted));
  publicationStore.publish(published);
  rootContentStore.store(content);

  const auto result = selectRootWithDirectory(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore);

  EXPECT_EQ(RootOpenStatus::RootDirectoryUnavailable, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootContent.is_initialized());
  EXPECT_EQ(authenticatedRoot(published), *result.selectedRoot);
  EXPECT_EQ(content, *result.rootContent);
  EXPECT_FALSE(result.rootDirectory.is_initialized());
  EXPECT_EQ(authenticatedRoot(accepted), *acceptedRootStateStore.load());
}

TEST_F(RootOpenWithDirectoryTestFixture, DoesNotAdvanceAcceptedStateWhenDirectoryUsesWrongKey) {
  const RootRecord accepted = rootRecord(1, 1);
  const RootRecord published = rootRecord(2, 2);
  const RootContent content = rootContent(published);
  const DirectoryRecord directory = rootDirectoryRecord(content);
  acceptedRootStateStore.store(authenticatedRoot(accepted));
  publicationStore.publish(published);
  rootContentStore.store(content);
  DirectoryRecordStore(_tempDir.path() / "directories", filesystemId(), objectEncryptionKey(2))
    .store(directory);

  const auto result = selectRootWithDirectory(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore);

  EXPECT_EQ(RootOpenStatus::RootDirectoryUnavailable, result.status);
  ASSERT_TRUE(result.rootContent.is_initialized());
  EXPECT_FALSE(result.rootDirectory.is_initialized());
  EXPECT_EQ(authenticatedRoot(accepted), *acceptedRootStateStore.load());
}

TEST_F(RootOpenWithDirectoryTestFixture, DoesNotChangeAcceptedStateWhenSelectionFails) {
  const RootRecord accepted = rootRecord(2, 2);
  const RootRecord published = rootRecord(1, 1);
  const RootContent content = rootContent(published);
  acceptedRootStateStore.store(authenticatedRoot(accepted));
  publicationStore.publish(published);
  rootContentStore.store(content);
  directoryRecordStore.store(rootDirectoryRecord(content));

  const auto result = selectRootWithDirectory(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore);

  EXPECT_EQ(RootOpenStatus::RollbackDetected, result.status);
  EXPECT_FALSE(result.selectedRoot.is_initialized());
  EXPECT_FALSE(result.rootContent.is_initialized());
  EXPECT_FALSE(result.rootDirectory.is_initialized());
  EXPECT_EQ(authenticatedRoot(accepted), *acceptedRootStateStore.load());
}

TEST_F(RootOpenWithDirectoryTestFixture, FailsWithoutAuthenticatedRoots) {
  const auto result = selectRootWithDirectory(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore);

  EXPECT_EQ(RootOpenStatus::NoAuthenticatedRoots, result.status);
  EXPECT_FALSE(result.selectedRoot.is_initialized());
  EXPECT_FALSE(result.rootContent.is_initialized());
  EXPECT_FALSE(result.rootDirectory.is_initialized());
  EXPECT_FALSE(acceptedRootStateStore.load().is_initialized());
}

TEST_F(RootOpenWithDirectoryTestFixture, ThrowsForMalformedAcceptedRootState) {
  const RootRecord record = rootRecord(1, 1);
  const RootContent content = rootContent(record);
  publicationStore.publish(record);
  rootContentStore.store(content);
  directoryRecordStore.store(rootDirectoryRecord(content));
  serializeStringOnly("not accepted-root state").StoreToFile(_tempDir.path() / "accepted-root");

  EXPECT_THROW(
    selectRootWithDirectory(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore),
    std::runtime_error);
}

TEST_F(RootOpenWithDirectoryTestFixture, ValidatedTreeOpenAdvancesAcceptedStateAfterTreeValidation) {
  const RootRecord record = rootRecord(1, 1);
  publishRootWithDirectory(record);
  storeReachableRootTree();

  const auto result = openRootWithValidatedTree(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore);

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootContent.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::Valid, result.treeValidation->status);
  EXPECT_EQ(authenticatedRoot(record), *result.selectedRoot);
  EXPECT_EQ(authenticatedRoot(record), *acceptedRootStateStore.load());
}

TEST_F(RootOpenWithDirectoryTestFixture, ValidatedTreeOpenDoesNotAdvanceAcceptedStateWhenTreeIsInvalid) {
  const RootRecord accepted = rootRecord(1, 1);
  const RootRecord published = rootRecord(2, 2);
  acceptedRootStateStore.store(authenticatedRoot(accepted));
  publishRootWithDirectory(published);
  symlinkRecordStore.store(symlinkRecord());

  const auto result = openRootWithValidatedTree(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore);

  ASSERT_EQ(RootOpenStatus::RootTreeInvalid, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootContent.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::MissingFile, result.treeValidation->status);
  EXPECT_EQ(ObjectTreeRecordType::File, result.treeValidation->expectedType);
  EXPECT_EQ("/alpha", result.treeValidation->path);
  EXPECT_EQ(objectId(21), result.treeValidation->objectId);
  EXPECT_EQ(4u, result.treeValidation->generation);
  EXPECT_EQ(authenticatedRoot(accepted), *acceptedRootStateStore.load());
}

TEST_F(RootOpenWithDirectoryTestFixture, ValidatedTreeOpenDoesNotValidateWhenDirectoryIsMissing) {
  const RootRecord accepted = rootRecord(1, 1);
  const RootRecord published = rootRecord(2, 2);
  const RootContent content = rootContent(published);
  acceptedRootStateStore.store(authenticatedRoot(accepted));
  publicationStore.publish(published);
  rootContentStore.store(content);

  const auto result = openRootWithValidatedTree(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore);

  EXPECT_EQ(RootOpenStatus::RootDirectoryUnavailable, result.status);
  EXPECT_FALSE(result.rootDirectory.is_initialized());
  EXPECT_FALSE(result.treeValidation.is_initialized());
  EXPECT_EQ(authenticatedRoot(accepted), *acceptedRootStateStore.load());
}
