#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/RootPublisher.h>

#include <boost/filesystem.hpp>
#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

using cryfs::formatv2::AcceptedRootStateStore;
using cryfs::formatv2::AuthenticatedRoot;
using cryfs::formatv2::DirectoryEntry;
using cryfs::formatv2::DirectoryMetadata;
using cryfs::formatv2::DirectoryRecord;
using cryfs::formatv2::DirectoryRecordStore;
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
using cryfs::formatv2::SymlinkRecordStore;
using cryfs::formatv2::Timestamp;
using cryfs::formatv2::createInitialEmptyRoot;
using cryfs::formatv2::deriveObjectEncryptionKey;
using cryfs::formatv2::deriveRootAuthenticationKey;
using cryfs::formatv2::openRootWithValidatedTree;
using cryfs::formatv2::publishValidatedRoot;

namespace {

namespace bf = boost::filesystem;

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

RootRecord rootRecord(uint64_t epoch, unsigned int rootIdSeed) {
  return RootRecord{filesystemId(), epoch, rootId(rootIdSeed)};
}

AuthenticatedRoot authenticatedRoot(const RootRecord &record) {
  return AuthenticatedRoot{record.epoch, record.rootId};
}

RootContent rootContent(const RootRecord &record, unsigned int rootDirectoryIdSeed = 11) {
  return RootContent{record.filesystemId, record.epoch, record.rootId, objectId(rootDirectoryIdSeed), 3};
}

DirectoryRecord emptyRootDirectory(const RootContent &content) {
  return DirectoryRecord{
    content.filesystemId,
    content.rootDirectoryId,
    content.rootDirectoryGeneration,
    metadata(),
    {}
  };
}

DirectoryRecord conflictingDirectoryRecord(const DirectoryRecord &record) {
  DirectoryRecord conflicting = record;
  conflicting.metadata.permissions = 0700;
  return conflicting;
}

RootContent conflictingRootContent(const RootContent &content) {
  RootContent conflicting = content;
  conflicting.rootDirectoryGeneration += 1;
  return conflicting;
}

DirectoryRecord rootDirectoryWithMissingFile(const RootContent &content) {
  return DirectoryRecord{
    content.filesystemId,
    content.rootDirectoryId,
    content.rootDirectoryGeneration,
    metadata(),
    {
      DirectoryEntry{"missing", ObjectType::File, objectId(21), 4}
    }
  };
}

void replaceDirectoryWithFile(const bf::path &directory) {
  ASSERT_TRUE(bf::remove(directory));
  cpputils::DataFixture::generate(1).StoreToFile(directory);
}

class RootPublisherTestFixture : public ::testing::Test {
public:
  RootPublisherTestFixture()
    : publicationStore(_tempDir.path() / "roots", filesystemId(), rootAuthenticationKey()),
      acceptedRootStateStore(_tempDir.path() / "accepted-root", filesystemId()),
      rootContentStore(_tempDir.path() / "root-content", filesystemId(), objectEncryptionKey()),
      directoryRecordStore(_tempDir.path() / "directories", filesystemId(), objectEncryptionKey()),
      fileRecordStore(_tempDir.path() / "files", filesystemId(), objectEncryptionKey()),
      symlinkRecordStore(_tempDir.path() / "symlinks", filesystemId(), objectEncryptionKey()) {
  }

protected:
  cpputils::TempDir _tempDir;
  RootPublicationStore publicationStore;
  AcceptedRootStateStore acceptedRootStateStore;
  RootContentStore rootContentStore;
  DirectoryRecordStore directoryRecordStore;
  FileRecordStore fileRecordStore;
  SymlinkRecordStore symlinkRecordStore;
};

}

TEST_F(RootPublisherTestFixture, PublishValidatedRootPublishesAfterValidationAndAdvancesAcceptedRoot) {
  const RootRecord record = rootRecord(1, 1);
  const RootContent content = rootContent(record);
  const DirectoryRecord directory = emptyRootDirectory(content);

  const auto result = publishValidatedRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    record,
    content,
    directory);

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootContent.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::Valid, result.treeValidation->status);
  EXPECT_EQ(authenticatedRoot(record), *result.selectedRoot);
  EXPECT_EQ(authenticatedRoot(record), *acceptedRootStateStore.load());
  EXPECT_EQ(content, *rootContentStore.load(authenticatedRoot(record)));
  EXPECT_EQ(directory, *directoryRecordStore.load(directory.directoryId, directory.generation));
  EXPECT_EQ(1u, publicationStore.loadAuthenticatedRoots().size());
}

TEST_F(RootPublisherTestFixture, PublishValidatedRootDoesNotPublishInvalidTree) {
  const RootRecord record = rootRecord(1, 1);
  const RootContent content = rootContent(record);
  const DirectoryRecord directory = rootDirectoryWithMissingFile(content);

  const auto result = publishValidatedRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    record,
    content,
    directory);

  ASSERT_EQ(RootOpenStatus::RootTreeInvalid, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootContent.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::MissingFile, result.treeValidation->status);
  EXPECT_EQ(ObjectTreeRecordType::File, result.treeValidation->expectedType);
  EXPECT_EQ("/missing", result.treeValidation->path);
  EXPECT_TRUE(publicationStore.loadAuthenticatedRoots().empty());
  EXPECT_FALSE(rootContentStore.load(authenticatedRoot(record)).is_initialized());
  EXPECT_FALSE(directoryRecordStore.load(directory.directoryId, directory.generation).is_initialized());
  EXPECT_FALSE(acceptedRootStateStore.load().is_initialized());
}

TEST_F(RootPublisherTestFixture, PublishValidatedRootRejectsMismatchedRootContent) {
  const RootRecord record = rootRecord(1, 1);
  const RootRecord otherRecord = rootRecord(1, 2);
  const RootContent content = rootContent(otherRecord);
  const DirectoryRecord directory = emptyRootDirectory(content);

  EXPECT_THROW(
    publishValidatedRoot(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      record,
      content,
      directory),
    std::runtime_error);
  EXPECT_TRUE(publicationStore.loadAuthenticatedRoots().empty());
}

TEST_F(RootPublisherTestFixture, PublishValidatedRootRejectsMismatchedRootDirectory) {
  const RootRecord record = rootRecord(1, 1);
  const RootContent content = rootContent(record);
  DirectoryRecord directory = emptyRootDirectory(content);
  directory.directoryId = objectId(99);

  EXPECT_THROW(
    publishValidatedRoot(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      record,
      content,
      directory),
    std::runtime_error);
  EXPECT_TRUE(publicationStore.loadAuthenticatedRoots().empty());
}

TEST_F(RootPublisherTestFixture, PublishValidatedRootRejectsCandidateThatWouldNotBecomeSelected) {
  const RootRecord accepted = rootRecord(5, 5);
  const RootRecord record = rootRecord(4, 4);
  const RootContent content = rootContent(record);
  const DirectoryRecord directory = emptyRootDirectory(content);
  acceptedRootStateStore.store(authenticatedRoot(accepted));

  EXPECT_THROW(
    publishValidatedRoot(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      record,
      content,
      directory),
    std::runtime_error);
  EXPECT_TRUE(publicationStore.loadAuthenticatedRoots().empty());
  EXPECT_FALSE(rootContentStore.load(authenticatedRoot(record)).is_initialized());
  EXPECT_FALSE(directoryRecordStore.load(directory.directoryId, directory.generation).is_initialized());
  EXPECT_EQ(authenticatedRoot(accepted), *acceptedRootStateStore.load());
}

TEST_F(RootPublisherTestFixture, PublishValidatedRootRejectsAlreadyPublishedCandidate) {
  const RootRecord record = rootRecord(1, 1);
  const RootContent content = rootContent(record);
  const DirectoryRecord directory = emptyRootDirectory(content);
  publicationStore.publish(record);

  EXPECT_THROW(
    publishValidatedRoot(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      record,
      content,
      directory),
    std::runtime_error);
  EXPECT_FALSE(rootContentStore.load(authenticatedRoot(record)).is_initialized());
  EXPECT_FALSE(directoryRecordStore.load(directory.directoryId, directory.generation).is_initialized());
  EXPECT_EQ(1u, publicationStore.loadAuthenticatedRoots().size());
}

TEST_F(RootPublisherTestFixture, PublishValidatedRootDoesNotPublishWhenDirectoryStoreFails) {
  const RootRecord record = rootRecord(1, 1);
  const RootContent content = rootContent(record);
  const DirectoryRecord directory = emptyRootDirectory(content);
  const DirectoryRecord existingDirectory = conflictingDirectoryRecord(directory);
  directoryRecordStore.store(existingDirectory);

  EXPECT_THROW(
    publishValidatedRoot(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      record,
      content,
      directory),
    std::exception);

  EXPECT_EQ(existingDirectory, *directoryRecordStore.load(directory.directoryId, directory.generation));
  EXPECT_FALSE(rootContentStore.load(authenticatedRoot(record)).is_initialized());
  EXPECT_TRUE(publicationStore.loadAuthenticatedRoots().empty());
  EXPECT_FALSE(acceptedRootStateStore.load().is_initialized());
}

TEST_F(RootPublisherTestFixture, PublishValidatedRootDoesNotPublishWhenRootContentStoreFails) {
  const RootRecord record = rootRecord(1, 1);
  const RootContent content = rootContent(record);
  const RootContent existingContent = conflictingRootContent(content);
  const DirectoryRecord directory = emptyRootDirectory(content);
  rootContentStore.store(existingContent);

  EXPECT_THROW(
    publishValidatedRoot(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      record,
      content,
      directory),
    std::exception);

  EXPECT_EQ(directory, *directoryRecordStore.load(directory.directoryId, directory.generation));
  EXPECT_EQ(existingContent, *rootContentStore.load(authenticatedRoot(record)));
  EXPECT_TRUE(publicationStore.loadAuthenticatedRoots().empty());
  EXPECT_FALSE(acceptedRootStateStore.load().is_initialized());
}

TEST_F(RootPublisherTestFixture, PublishValidatedRootDoesNotAdvanceAcceptedStateWhenRootPublicationFails) {
  const RootRecord record = rootRecord(1, 1);
  const RootContent content = rootContent(record);
  const DirectoryRecord directory = emptyRootDirectory(content);
  replaceDirectoryWithFile(_tempDir.path() / "roots");

  EXPECT_THROW(
    publishValidatedRoot(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      record,
      content,
      directory),
    std::exception);

  EXPECT_EQ(directory, *directoryRecordStore.load(directory.directoryId, directory.generation));
  EXPECT_EQ(content, *rootContentStore.load(authenticatedRoot(record)));
  EXPECT_TRUE(publicationStore.loadAuthenticatedRoots().empty());
  EXPECT_FALSE(acceptedRootStateStore.load().is_initialized());
}

TEST_F(RootPublisherTestFixture, PublishValidatedRootPublishesButDoesNotAdvanceWhenAcceptedStateStoreFails) {
  const RootRecord record = rootRecord(1, 1);
  const RootContent content = rootContent(record);
  const DirectoryRecord directory = emptyRootDirectory(content);
  const auto stateParent = _tempDir.path() / "accepted-root-parent-file";
  cpputils::DataFixture::generate(1).StoreToFile(stateParent);
  const AcceptedRootStateStore failingAcceptedRootStateStore(
    stateParent / "accepted-root",
    filesystemId());

  EXPECT_THROW(
    publishValidatedRoot(
      publicationStore,
      failingAcceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      record,
      content,
      directory),
    std::exception);

  EXPECT_EQ(directory, *directoryRecordStore.load(directory.directoryId, directory.generation));
  EXPECT_EQ(content, *rootContentStore.load(authenticatedRoot(record)));
  const std::vector<AuthenticatedRoot> roots = publicationStore.loadAuthenticatedRoots();
  ASSERT_EQ(1u, roots.size());
  EXPECT_EQ(authenticatedRoot(record), roots.front());
  EXPECT_FALSE(bf::exists(stateParent / "accepted-root"));

  const auto recovered = openRootWithValidatedTree(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore);
  EXPECT_EQ(RootOpenStatus::Selected, recovered.status);
  EXPECT_EQ(authenticatedRoot(record), *acceptedRootStateStore.load());
}

TEST_F(RootPublisherTestFixture, CreateInitialEmptyRootPublishesMountableTreeAndAdvancesAcceptedRoot) {
  const auto result = createInitialEmptyRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    filesystemId(),
    rootId(1),
    objectId(2),
    metadata());

  const AuthenticatedRoot expectedRoot{1, rootId(1)};
  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.rootContent.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::Valid, result.treeValidation->status);
  EXPECT_EQ(expectedRoot, *acceptedRootStateStore.load());
  EXPECT_EQ(1u, result.rootContent->epoch);
  EXPECT_EQ(rootId(1), result.rootContent->rootId);
  EXPECT_EQ(objectId(2), result.rootContent->rootDirectoryId);
  EXPECT_EQ(1u, result.rootContent->rootDirectoryGeneration);
  EXPECT_TRUE(result.rootDirectory->entries.empty());

  const auto reopen = openRootWithValidatedTree(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore);
  EXPECT_EQ(RootOpenStatus::Selected, reopen.status);
  EXPECT_EQ(expectedRoot, *reopen.selectedRoot);
}

TEST_F(RootPublisherTestFixture, CreateInitialEmptyRootRejectsExistingAcceptedState) {
  const RootRecord accepted = rootRecord(1, 7);
  acceptedRootStateStore.store(authenticatedRoot(accepted));

  EXPECT_THROW(
    createInitialEmptyRoot(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      filesystemId(),
      rootId(1),
      objectId(2),
      metadata()),
    std::runtime_error);
  EXPECT_TRUE(publicationStore.loadAuthenticatedRoots().empty());
  EXPECT_EQ(authenticatedRoot(accepted), *acceptedRootStateStore.load());
}

TEST_F(RootPublisherTestFixture, CreateInitialEmptyRootRejectsExistingPublishedRoot) {
  const RootRecord published = rootRecord(1, 7);
  publicationStore.publish(published);

  EXPECT_THROW(
    createInitialEmptyRoot(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      filesystemId(),
      rootId(1),
      objectId(2),
      metadata()),
    std::runtime_error);
  const std::vector<AuthenticatedRoot> roots = publicationStore.loadAuthenticatedRoots();
  ASSERT_EQ(1u, roots.size());
  EXPECT_EQ(authenticatedRoot(published), roots.front());
  EXPECT_FALSE(acceptedRootStateStore.load().is_initialized());
}
