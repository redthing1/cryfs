#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/RootTransaction.h>

#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using cryfs::formatv2::AcceptedRootStateStore;
using cryfs::formatv2::AuthenticatedRoot;
using cryfs::formatv2::DirectoryEntry;
using cryfs::formatv2::DirectoryChildReference;
using cryfs::formatv2::DirectoryMetadata;
using cryfs::formatv2::DirectoryPathSegment;
using cryfs::formatv2::DirectoryRecord;
using cryfs::formatv2::DirectoryRecordStore;
using cryfs::formatv2::FileDataRecord;
using cryfs::formatv2::FileExtent;
using cryfs::formatv2::FileRecord;
using cryfs::formatv2::FileRecordStore;
using cryfs::formatv2::FileUpdate;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::ObjectTreeRecordType;
using cryfs::formatv2::ObjectTreeValidationStatus;
using cryfs::formatv2::ObjectType;
using cryfs::formatv2::RootContent;
using cryfs::formatv2::RootId;
using cryfs::formatv2::RootOpenStatus;
using cryfs::formatv2::RootOpenWithValidatedTreeResult;
using cryfs::formatv2::RootPublicationStore;
using cryfs::formatv2::RootContentStore;
using cryfs::formatv2::SymlinkRecordStore;
using cryfs::formatv2::SymlinkUpdate;
using cryfs::formatv2::Timestamp;
using cryfs::formatv2::buildRootDirectoryTransaction;
using cryfs::formatv2::buildCreateRootDirectoryChildTransaction;
using cryfs::formatv2::buildCreateRootFileChildTransaction;
using cryfs::formatv2::buildCreateRootFileWithDataChildTransaction;
using cryfs::formatv2::buildCreateRootSymlinkChildTransaction;
using cryfs::formatv2::buildCreateNestedDirectoryChildTransaction;
using cryfs::formatv2::buildCreateNestedFileChildTransaction;
using cryfs::formatv2::buildCreateNestedFileWithDataChildTransaction;
using cryfs::formatv2::buildCreateNestedSymlinkChildTransaction;
using cryfs::formatv2::createInitialEmptyRoot;
using cryfs::formatv2::buildUpdateRootFileChildTransaction;
using cryfs::formatv2::buildUpdateNestedFileChildTransaction;
using cryfs::formatv2::buildUpdateRootSymlinkChildTransaction;
using cryfs::formatv2::buildUpdateNestedSymlinkChildTransaction;
using cryfs::formatv2::buildUpdateNestedDirectoryMetadataTransaction;
using cryfs::formatv2::buildRemoveRootChildTransaction;
using cryfs::formatv2::buildRemoveNestedChildTransaction;
using cryfs::formatv2::buildMoveChildTransaction;
using cryfs::formatv2::buildMoveChildReplacingTransaction;
using cryfs::formatv2::deriveObjectEncryptionKey;
using cryfs::formatv2::deriveRootAuthenticationKey;
using cryfs::formatv2::loadDirectoryPathFromRoot;
using cryfs::formatv2::publishCreateRootDirectoryChildTransaction;
using cryfs::formatv2::publishCreateRootFileChildTransaction;
using cryfs::formatv2::publishCreateRootFileWithDataChildTransaction;
using cryfs::formatv2::publishCreateRootSymlinkChildTransaction;
using cryfs::formatv2::publishCreateNestedDirectoryChildTransaction;
using cryfs::formatv2::publishCreateNestedFileChildTransaction;
using cryfs::formatv2::publishCreateNestedFileWithDataChildTransaction;
using cryfs::formatv2::publishCreateNestedSymlinkChildTransaction;
using cryfs::formatv2::publishRootDirectoryTransaction;
using cryfs::formatv2::publishUpdateRootFileChildTransaction;
using cryfs::formatv2::publishUpdateNestedFileChildTransaction;
using cryfs::formatv2::publishUpdateRootSymlinkChildTransaction;
using cryfs::formatv2::publishUpdateNestedSymlinkChildTransaction;
using cryfs::formatv2::publishUpdateNestedDirectoryMetadataTransaction;
using cryfs::formatv2::publishRemoveRootChildTransaction;
using cryfs::formatv2::publishRemoveNestedChildTransaction;
using cryfs::formatv2::publishMoveChildTransaction;
using cryfs::formatv2::publishMoveChildReplacingTransaction;

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

Timestamp timestamp(int64_t seconds, uint32_t nanoseconds) {
  return Timestamp{seconds, nanoseconds};
}

DirectoryMetadata metadata(uint32_t permissions = 0755) {
  return DirectoryMetadata{
    permissions,
    1000,
    1001,
    timestamp(10, 11),
    timestamp(12, 13),
    timestamp(14, 15)
  };
}

DirectoryEntry directoryEntry(const std::string &name, unsigned int seed) {
  return DirectoryEntry{name, ObjectType::Directory, objectId(seed), 1};
}

DirectoryEntry missingFileEntry(const std::string &name, unsigned int seed) {
  return DirectoryEntry{name, ObjectType::File, objectId(seed), 1};
}

cpputils::Data payload(const std::string &value) {
  cpputils::Data data(value.size());
  std::memcpy(data.data(), value.data(), value.size());
  return data;
}

std::string payloadString(const cpputils::Data &data) {
  return std::string(static_cast<const char*>(data.data()), data.size());
}

std::vector<FileDataRecord> fileDataRecords(unsigned int seed, const std::string &value) {
  std::vector<FileDataRecord> records;
  records.push_back(FileDataRecord{filesystemId(), objectId(seed), 1, payload(value)});
  return records;
}

class RootTransactionTestFixture : public ::testing::Test {
public:
  RootTransactionTestFixture()
    : publicationStore(_tempDir.path() / "roots", filesystemId(), rootAuthenticationKey()),
      acceptedRootStateStore(_tempDir.path() / "accepted-root", filesystemId()),
      rootContentStore(_tempDir.path() / "root-content", filesystemId(), objectEncryptionKey()),
      directoryRecordStore(_tempDir.path() / "directories", filesystemId(), objectEncryptionKey()),
      fileRecordStore(_tempDir.path() / "files", filesystemId(), objectEncryptionKey()),
      symlinkRecordStore(_tempDir.path() / "symlinks", filesystemId(), objectEncryptionKey()) {
  }

  RootOpenWithValidatedTreeResult createBaseRoot() const {
    return createInitialEmptyRoot(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      filesystemId(),
      rootId(1),
      objectId(11),
      metadata());
  }

  RootOpenWithValidatedTreeResult createBaseRootWithParentDirectory() const {
    const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();
    return publishCreateRootDirectoryChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      baseRoot,
      rootId(2),
      objectId(21),
      "parent",
      metadata(),
      metadata(0711));
  }

  RootOpenWithValidatedTreeResult createBaseRootWithRootFile() const {
    const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();
    return publishCreateRootFileWithDataChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      baseRoot,
      rootId(2),
      objectId(31),
      objectId(32),
      "file",
      metadata(),
      metadata(0600),
      payload("old"));
  }

  RootOpenWithValidatedTreeResult createBaseRootWithNestedFile() const {
    const RootOpenWithValidatedTreeResult parentRoot = createBaseRootWithParentDirectory();
    return publishCreateNestedFileWithDataChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      parentRoot,
      rootId(3),
      loadParentPath(parentRoot),
      objectId(31),
      objectId(32),
      "file",
      metadata(),
      metadata(0600),
      payload("old"));
  }

  RootOpenWithValidatedTreeResult createBaseRootWithRootSymlink() const {
    const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();
    return publishCreateRootSymlinkChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      baseRoot,
      rootId(2),
      objectId(41),
      "link",
      metadata(),
      metadata(0777),
      "../target");
  }

  RootOpenWithValidatedTreeResult createBaseRootWithNestedSymlink() const {
    const RootOpenWithValidatedTreeResult parentRoot = createBaseRootWithParentDirectory();
    return publishCreateNestedSymlinkChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      parentRoot,
      rootId(3),
      loadParentPath(parentRoot),
      objectId(41),
      "link",
      metadata(),
      metadata(0777),
      "../target");
  }

  RootOpenWithValidatedTreeResult createBaseRootWithParentDirectoryAndRootFile() const {
    const RootOpenWithValidatedTreeResult parentRoot = createBaseRootWithParentDirectory();
    return publishCreateRootFileWithDataChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      parentRoot,
      rootId(3),
      objectId(31),
      objectId(32),
      "file",
      metadata(),
      metadata(0600),
      payload("old"));
  }

  RootOpenWithValidatedTreeResult createBaseRootWithTwoRootFiles() const {
    const RootOpenWithValidatedTreeResult sourceRoot = createBaseRootWithRootFile();
    return publishCreateRootFileChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      sourceRoot,
      rootId(3),
      objectId(41),
      "target",
      metadata(),
      metadata(0600));
  }

  RootOpenWithValidatedTreeResult createBaseRootWithNestedDirectory() const {
    const RootOpenWithValidatedTreeResult parentRoot = createBaseRootWithParentDirectory();
    return publishCreateNestedDirectoryChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      parentRoot,
      rootId(3),
      loadParentPath(parentRoot),
      objectId(41),
      "child",
      metadata(),
      metadata(0700));
  }

  std::vector<DirectoryPathSegment> loadChildPath(
    const RootOpenWithValidatedTreeResult &baseRoot) const {
    return loadDirectoryPathFromRoot(
      baseRoot,
      directoryRecordStore,
      {"parent", "child"});
  }

  std::vector<DirectoryPathSegment> loadParentPath(
    const RootOpenWithValidatedTreeResult &baseRoot) const {
    return loadDirectoryPathFromRoot(
      baseRoot,
      directoryRecordStore,
      {"parent"});
  }

  void storeConflictingRootContentForCandidate(
    const RootOpenWithValidatedTreeResult &baseRoot,
    const RootId &nextRootId) const {
    ASSERT_TRUE(baseRoot.selectedRoot.is_initialized());
    rootContentStore.store(RootContent{
      filesystemId(),
      baseRoot.selectedRoot->epoch + 1,
      nextRootId,
      objectId(99),
      1
    });
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

TEST_F(RootTransactionTestFixture, BuildRootDirectoryTransactionCreatesNextEpochAndGeneration) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  const auto transaction = buildRootDirectoryTransaction(
    baseRoot,
    rootId(2),
    metadata(0700),
    {});

  EXPECT_EQ(filesystemId(), transaction.rootRecord.filesystemId);
  EXPECT_EQ(2u, transaction.rootRecord.epoch);
  EXPECT_EQ(rootId(2), transaction.rootRecord.rootId);
  EXPECT_EQ(objectId(11), transaction.rootContent.rootDirectoryId);
  EXPECT_EQ(2u, transaction.rootContent.rootDirectoryGeneration);
  EXPECT_EQ(objectId(11), transaction.rootDirectory.directoryId);
  EXPECT_EQ(2u, transaction.rootDirectory.generation);
  EXPECT_EQ(0700u, transaction.rootDirectory.metadata.permissions);
}

TEST_F(RootTransactionTestFixture, BuildRootDirectoryTransactionCanonicalizesEntries) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  const auto transaction = buildRootDirectoryTransaction(
    baseRoot,
    rootId(2),
    metadata(),
    {
      directoryEntry("z", 21),
      directoryEntry("a", 22)
    });

  ASSERT_EQ(2u, transaction.rootDirectory.entries.size());
  EXPECT_EQ("a", transaction.rootDirectory.entries[0].name);
  EXPECT_EQ("z", transaction.rootDirectory.entries[1].name);
}

TEST_F(RootTransactionTestFixture, BuildRootDirectoryTransactionRejectsDuplicateEntries) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  EXPECT_THROW(
    buildRootDirectoryTransaction(
      baseRoot,
      rootId(2),
      metadata(),
      {
        directoryEntry("same", 21),
        directoryEntry("same", 22)
      }),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, BuildRootDirectoryTransactionRejectsUnselectedBaseRoot) {
  const RootOpenWithValidatedTreeResult baseRoot{
    RootOpenStatus::NoAuthenticatedRoots,
    boost::none,
    boost::none,
    boost::none,
    boost::none
  };

  EXPECT_THROW(
    buildRootDirectoryTransaction(baseRoot, rootId(2), metadata(), {}),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, BuildRootDirectoryTransactionRejectsReusedRootId) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  ASSERT_TRUE(baseRoot.selectedRoot.is_initialized());
  EXPECT_THROW(
    buildRootDirectoryTransaction(baseRoot, baseRoot.selectedRoot->rootId, metadata(), {}),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, PublishRootDirectoryTransactionPublishesNextSelectedRoot) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  const auto result = publishRootDirectoryTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(2),
    metadata(0700),
    {});

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootContent.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *result.selectedRoot);
  EXPECT_EQ(2u, result.rootContent->epoch);
  EXPECT_EQ(2u, result.rootDirectory->generation);
  EXPECT_EQ(0700u, result.rootDirectory->metadata.permissions);
  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *acceptedRootStateStore.load());
  EXPECT_EQ(2u, publicationStore.loadAuthenticatedRoots().size());
  EXPECT_TRUE(directoryRecordStore.load(objectId(11), 2).is_initialized());
}

TEST_F(RootTransactionTestFixture, PublishRootDirectoryTransactionDoesNotPublishInvalidTree) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  const auto result = publishRootDirectoryTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(2),
    metadata(),
    {
      missingFileEntry("missing", 21)
    });

  ASSERT_EQ(RootOpenStatus::RootTreeInvalid, result.status);
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::MissingFile, result.treeValidation->status);
  EXPECT_EQ(ObjectTreeRecordType::File, result.treeValidation->expectedType);
  EXPECT_EQ("/missing", result.treeValidation->path);
  EXPECT_EQ((AuthenticatedRoot{1, rootId(1)}), *acceptedRootStateStore.load());
  EXPECT_EQ(1u, publicationStore.loadAuthenticatedRoots().size());
  EXPECT_FALSE(directoryRecordStore.load(objectId(11), 2).is_initialized());
  EXPECT_FALSE(rootContentStore.load(AuthenticatedRoot{2, rootId(2)}).is_initialized());
}

TEST_F(RootTransactionTestFixture, BuildCreateRootDirectoryChildTransactionBuildsChildAndRootUpdate) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  const auto transaction = buildCreateRootDirectoryChildTransaction(
    baseRoot,
    rootId(2),
    objectId(21),
    "child",
    metadata(0700),
    metadata(0711));

  EXPECT_EQ(objectId(21), transaction.childDirectory.directoryId);
  EXPECT_EQ(1u, transaction.childDirectory.generation);
  EXPECT_EQ(0711u, transaction.childDirectory.metadata.permissions);
  EXPECT_TRUE(transaction.childDirectory.entries.empty());
  EXPECT_EQ(2u, transaction.rootUpdate.rootRecord.epoch);
  ASSERT_EQ(1u, transaction.rootUpdate.rootDirectory.entries.size());
  EXPECT_EQ("child", transaction.rootUpdate.rootDirectory.entries[0].name);
  EXPECT_EQ(ObjectType::Directory, transaction.rootUpdate.rootDirectory.entries[0].type);
  EXPECT_EQ(objectId(21), transaction.rootUpdate.rootDirectory.entries[0].objectId);
  EXPECT_EQ(1u, transaction.rootUpdate.rootDirectory.entries[0].generation);
}

TEST_F(RootTransactionTestFixture, BuildCreateRootDirectoryChildTransactionRejectsRootDirectoryIdReuse) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  ASSERT_TRUE(baseRoot.rootDirectory.is_initialized());
  EXPECT_THROW(
    buildCreateRootDirectoryChildTransaction(
      baseRoot,
      rootId(2),
      baseRoot.rootDirectory->directoryId,
      "child",
      metadata(),
      metadata()),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, PublishCreateRootDirectoryChildTransactionPublishesReachableChild) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  const auto result = publishCreateRootDirectoryChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(2),
    objectId(21),
    "child",
    metadata(0700),
    metadata(0711));

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::Valid, result.treeValidation->status);
  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *result.selectedRoot);
  ASSERT_EQ(1u, result.rootDirectory->entries.size());
  EXPECT_EQ("child", result.rootDirectory->entries[0].name);
  EXPECT_EQ(objectId(21), result.rootDirectory->entries[0].objectId);
  const auto child = directoryRecordStore.load(objectId(21), 1);
  ASSERT_TRUE(child.is_initialized());
  EXPECT_EQ(0711u, child->metadata.permissions);
  EXPECT_TRUE(child->entries.empty());
}

TEST_F(RootTransactionTestFixture, PublishCreateRootDirectoryChildTransactionRejectsDuplicateNameBeforeStagingChild) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();
  const auto first = publishCreateRootDirectoryChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(2),
    objectId(21),
    "child",
    metadata(),
    metadata());
  ASSERT_EQ(RootOpenStatus::Selected, first.status);

  EXPECT_THROW(
    publishCreateRootDirectoryChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      first,
      rootId(3),
      objectId(22),
      "child",
      metadata(),
      metadata()),
    std::runtime_error);
  EXPECT_FALSE(directoryRecordStore.load(objectId(22), 1).is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *acceptedRootStateStore.load());
}

TEST_F(RootTransactionTestFixture, PublishCreateRootDirectoryChildTransactionKeepsAcceptedRootWhenRootContentStoreFails) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();
  storeConflictingRootContentForCandidate(baseRoot, rootId(2));

  EXPECT_THROW(
    publishCreateRootDirectoryChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      baseRoot,
      rootId(2),
      objectId(21),
      "child",
      metadata(),
      metadata()),
    std::runtime_error);

  EXPECT_EQ((AuthenticatedRoot{1, rootId(1)}), *acceptedRootStateStore.load());
  EXPECT_EQ(1u, publicationStore.loadAuthenticatedRoots().size());
  EXPECT_TRUE(directoryRecordStore.load(objectId(21), 1).is_initialized());
  EXPECT_TRUE(directoryRecordStore.load(objectId(11), 2).is_initialized());
}

TEST_F(RootTransactionTestFixture, LoadDirectoryPathFromRootLoadsNestedDirectoryRecords) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();

  const std::vector<DirectoryPathSegment> parentPath = loadParentPath(baseRoot);

  ASSERT_EQ(1u, parentPath.size());
  EXPECT_EQ("parent", parentPath[0].name);
  EXPECT_EQ(objectId(21), parentPath[0].directory.directoryId);
  EXPECT_EQ(1u, parentPath[0].directory.generation);
}

TEST_F(RootTransactionTestFixture, LoadDirectoryPathFromRootRejectsMissingComponent) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();

  EXPECT_THROW(
    loadDirectoryPathFromRoot(
      baseRoot,
      directoryRecordStore,
      {"missing"}),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, BuildCreateNestedDirectoryChildTransactionBuildsLeafAndAncestorUpdates) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();

  const auto transaction = buildCreateNestedDirectoryChildTransaction(
    baseRoot,
    rootId(3),
    loadParentPath(baseRoot),
    objectId(31),
    "child",
    metadata(0701),
    metadata(0711));

  EXPECT_EQ(objectId(31), transaction.childDirectory.directoryId);
  EXPECT_EQ(1u, transaction.childDirectory.generation);
  EXPECT_EQ(0711u, transaction.childDirectory.metadata.permissions);
  ASSERT_EQ(1u, transaction.pathUpdate.stagedDirectories.size());
  const auto &updatedParent = transaction.pathUpdate.stagedDirectories[0];
  EXPECT_EQ(objectId(21), updatedParent.directoryId);
  EXPECT_EQ(2u, updatedParent.generation);
  EXPECT_EQ(0701u, updatedParent.metadata.permissions);
  ASSERT_EQ(1u, updatedParent.entries.size());
  EXPECT_EQ("child", updatedParent.entries[0].name);
  EXPECT_EQ(objectId(31), updatedParent.entries[0].objectId);
  EXPECT_EQ(1u, updatedParent.entries[0].generation);
  EXPECT_EQ(3u, transaction.pathUpdate.rootUpdate.rootRecord.epoch);
  EXPECT_EQ(3u, transaction.pathUpdate.rootUpdate.rootDirectory.generation);
  ASSERT_EQ(1u, transaction.pathUpdate.rootUpdate.rootDirectory.entries.size());
  EXPECT_EQ("parent", transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].name);
  EXPECT_EQ(objectId(21), transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].objectId);
  EXPECT_EQ(2u, transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].generation);
}

TEST_F(RootTransactionTestFixture, BuildCreateNestedDirectoryChildTransactionRejectsEmptyParentPath) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  EXPECT_THROW(
    buildCreateNestedDirectoryChildTransaction(
      baseRoot,
      rootId(2),
      {},
      objectId(31),
      "child",
      metadata(),
      metadata()),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, PublishCreateNestedDirectoryChildTransactionPublishesReachableChild) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();

  const auto result = publishCreateNestedDirectoryChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(3),
    loadParentPath(baseRoot),
    objectId(31),
    "child",
    metadata(0701),
    metadata(0711));

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::Valid, result.treeValidation->status);
  EXPECT_EQ((AuthenticatedRoot{3, rootId(3)}), *result.selectedRoot);
  ASSERT_EQ(1u, result.rootDirectory->entries.size());
  EXPECT_EQ("parent", result.rootDirectory->entries[0].name);
  EXPECT_EQ(objectId(21), result.rootDirectory->entries[0].objectId);
  EXPECT_EQ(2u, result.rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore.load(objectId(21), 2);
  ASSERT_TRUE(parent.is_initialized());
  ASSERT_EQ(1u, parent->entries.size());
  EXPECT_EQ("child", parent->entries[0].name);
  EXPECT_EQ(objectId(31), parent->entries[0].objectId);
  EXPECT_EQ(1u, parent->entries[0].generation);
  EXPECT_TRUE(directoryRecordStore.load(objectId(31), 1).is_initialized());
}

TEST_F(RootTransactionTestFixture, PublishCreateNestedDirectoryChildTransactionRejectsDuplicateNameBeforeStagingChild) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();
  const auto first = publishCreateNestedDirectoryChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(3),
    loadParentPath(baseRoot),
    objectId(31),
    "child",
    metadata(),
    metadata());
  ASSERT_EQ(RootOpenStatus::Selected, first.status);

  EXPECT_THROW(
    publishCreateNestedDirectoryChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      first,
      rootId(4),
      loadParentPath(first),
      objectId(32),
      "child",
      metadata(),
      metadata()),
    std::runtime_error);
  EXPECT_FALSE(directoryRecordStore.load(objectId(32), 1).is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(3)}), *acceptedRootStateStore.load());
}

TEST_F(RootTransactionTestFixture, PublishCreateNestedDirectoryChildTransactionKeepsAcceptedRootWhenRootContentStoreFails) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();
  storeConflictingRootContentForCandidate(baseRoot, rootId(3));

  EXPECT_THROW(
    publishCreateNestedDirectoryChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      baseRoot,
      rootId(3),
      loadParentPath(baseRoot),
      objectId(31),
      "child",
      metadata(),
      metadata()),
    std::runtime_error);

  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *acceptedRootStateStore.load());
  EXPECT_EQ(2u, publicationStore.loadAuthenticatedRoots().size());
  EXPECT_TRUE(directoryRecordStore.load(objectId(31), 1).is_initialized());
  EXPECT_TRUE(directoryRecordStore.load(objectId(21), 2).is_initialized());
  EXPECT_TRUE(directoryRecordStore.load(objectId(11), 3).is_initialized());
}

TEST_F(RootTransactionTestFixture, BuildCreateNestedFileChildTransactionBuildsFileAndPathUpdate) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();

  const auto transaction = buildCreateNestedFileChildTransaction(
    baseRoot,
    rootId(3),
    loadParentPath(baseRoot),
    objectId(31),
    "file",
    metadata(0701),
    metadata(0600));

  EXPECT_EQ(objectId(31), transaction.childFile.fileId);
  EXPECT_EQ(1u, transaction.childFile.generation);
  EXPECT_EQ(0600u, transaction.childFile.metadata.permissions);
  EXPECT_EQ(0u, transaction.childFile.size);
  EXPECT_TRUE(transaction.childFile.extents.empty());
  ASSERT_EQ(1u, transaction.pathUpdate.stagedDirectories.size());
  const auto &updatedParent = transaction.pathUpdate.stagedDirectories[0];
  EXPECT_EQ(objectId(21), updatedParent.directoryId);
  EXPECT_EQ(2u, updatedParent.generation);
  EXPECT_EQ(0701u, updatedParent.metadata.permissions);
  ASSERT_EQ(1u, updatedParent.entries.size());
  EXPECT_EQ("file", updatedParent.entries[0].name);
  EXPECT_EQ(ObjectType::File, updatedParent.entries[0].type);
  EXPECT_EQ(objectId(31), updatedParent.entries[0].objectId);
  ASSERT_EQ(1u, transaction.pathUpdate.rootUpdate.rootDirectory.entries.size());
  EXPECT_EQ(2u, transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].generation);
}

TEST_F(RootTransactionTestFixture, PublishCreateNestedFileChildTransactionPublishesReachableEmptyFile) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();

  const auto result = publishCreateNestedFileChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(3),
    loadParentPath(baseRoot),
    objectId(31),
    "file",
    metadata(0701),
    metadata(0600));

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::Valid, result.treeValidation->status);
  EXPECT_EQ((AuthenticatedRoot{3, rootId(3)}), *result.selectedRoot);
  EXPECT_EQ(2u, result.rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore.load(objectId(21), 2);
  ASSERT_TRUE(parent.is_initialized());
  ASSERT_EQ(1u, parent->entries.size());
  EXPECT_EQ("file", parent->entries[0].name);
  EXPECT_EQ(ObjectType::File, parent->entries[0].type);
  EXPECT_EQ(objectId(31), parent->entries[0].objectId);

  const auto file = fileRecordStore.load(objectId(31), 1);
  ASSERT_TRUE(file.is_initialized());
  EXPECT_EQ(0u, file->size);
  EXPECT_TRUE(file->extents.empty());
}

TEST_F(RootTransactionTestFixture, PublishCreateNestedFileChildTransactionRejectsDuplicateNameBeforeStagingFile) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();
  const auto first = publishCreateNestedFileChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(3),
    loadParentPath(baseRoot),
    objectId(31),
    "file",
    metadata(),
    metadata());
  ASSERT_EQ(RootOpenStatus::Selected, first.status);

  EXPECT_THROW(
    publishCreateNestedFileChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      first,
      rootId(4),
      loadParentPath(first),
      objectId(32),
      "file",
      metadata(),
      metadata()),
    std::runtime_error);
  EXPECT_FALSE(fileRecordStore.load(objectId(32), 1).is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(3)}), *acceptedRootStateStore.load());
}

TEST_F(RootTransactionTestFixture, PublishCreateNestedFileChildTransactionKeepsAcceptedRootWhenRootContentStoreFails) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();
  storeConflictingRootContentForCandidate(baseRoot, rootId(3));

  EXPECT_THROW(
    publishCreateNestedFileChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      baseRoot,
      rootId(3),
      loadParentPath(baseRoot),
      objectId(31),
      "file",
      metadata(),
      metadata()),
    std::runtime_error);

  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *acceptedRootStateStore.load());
  EXPECT_EQ(2u, publicationStore.loadAuthenticatedRoots().size());
  EXPECT_TRUE(fileRecordStore.load(objectId(31), 1).is_initialized());
  EXPECT_TRUE(directoryRecordStore.load(objectId(21), 2).is_initialized());
  EXPECT_TRUE(directoryRecordStore.load(objectId(11), 3).is_initialized());
}

TEST_F(RootTransactionTestFixture, BuildCreateNestedFileWithDataChildTransactionBuildsFileDataAndPathUpdate) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();

  const auto transaction = buildCreateNestedFileWithDataChildTransaction(
    baseRoot,
    rootId(3),
    loadParentPath(baseRoot),
    objectId(31),
    objectId(32),
    "file",
    metadata(0701),
    metadata(0600),
    payload("hello"));

  EXPECT_EQ(objectId(31), transaction.childFile.fileId);
  EXPECT_EQ(1u, transaction.childFile.generation);
  EXPECT_EQ(5u, transaction.childFile.size);
  ASSERT_EQ(1u, transaction.childFile.extents.size());
  EXPECT_EQ(objectId(32), transaction.childFile.extents[0].dataId);
  EXPECT_EQ(1u, transaction.childFile.extents[0].generation);
  EXPECT_EQ(objectId(32), transaction.childFileData.dataId);
  EXPECT_EQ("hello", payloadString(transaction.childFileData.payload));
  ASSERT_EQ(1u, transaction.pathUpdate.stagedDirectories.size());
  ASSERT_EQ(1u, transaction.pathUpdate.stagedDirectories[0].entries.size());
  EXPECT_EQ("file", transaction.pathUpdate.stagedDirectories[0].entries[0].name);
  EXPECT_EQ(ObjectType::File, transaction.pathUpdate.stagedDirectories[0].entries[0].type);
  EXPECT_EQ(objectId(31), transaction.pathUpdate.stagedDirectories[0].entries[0].objectId);
}

TEST_F(RootTransactionTestFixture, BuildCreateNestedFileWithDataChildTransactionRejectsEmptyPayload) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();

  EXPECT_THROW(
    buildCreateNestedFileWithDataChildTransaction(
      baseRoot,
      rootId(3),
      loadParentPath(baseRoot),
      objectId(31),
      objectId(32),
      "file",
      metadata(),
      metadata(),
      payload("")),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, PublishCreateNestedFileWithDataChildTransactionPublishesReachableFileData) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();

  const auto result = publishCreateNestedFileWithDataChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(3),
    loadParentPath(baseRoot),
    objectId(31),
    objectId(32),
    "file",
    metadata(0701),
    metadata(0600),
    payload("hello"));

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::Valid, result.treeValidation->status);
  EXPECT_EQ((AuthenticatedRoot{3, rootId(3)}), *result.selectedRoot);
  EXPECT_EQ(2u, result.rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore.load(objectId(21), 2);
  ASSERT_TRUE(parent.is_initialized());
  ASSERT_EQ(1u, parent->entries.size());
  EXPECT_EQ("file", parent->entries[0].name);
  EXPECT_EQ(ObjectType::File, parent->entries[0].type);
  EXPECT_EQ(objectId(31), parent->entries[0].objectId);

  const auto file = fileRecordStore.load(objectId(31), 1);
  ASSERT_TRUE(file.is_initialized());
  EXPECT_EQ(5u, file->size);
  ASSERT_EQ(1u, file->extents.size());
  EXPECT_EQ(objectId(32), file->extents[0].dataId);
  const auto data = fileRecordStore.loadData(objectId(32), 1);
  ASSERT_TRUE(data.is_initialized());
  EXPECT_EQ("hello", payloadString(data->payload));
}

TEST_F(RootTransactionTestFixture, PublishCreateNestedFileWithDataChildTransactionRejectsDuplicateNameBeforeStagingFileData) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();
  const auto first = publishCreateNestedFileWithDataChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(3),
    loadParentPath(baseRoot),
    objectId(31),
    objectId(32),
    "file",
    metadata(),
    metadata(),
    payload("hello"));
  ASSERT_EQ(RootOpenStatus::Selected, first.status);

  EXPECT_THROW(
    publishCreateNestedFileWithDataChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      first,
      rootId(4),
      loadParentPath(first),
      objectId(33),
      objectId(34),
      "file",
      metadata(),
      metadata(),
      payload("other")),
    std::runtime_error);
  EXPECT_FALSE(fileRecordStore.load(objectId(33), 1).is_initialized());
  EXPECT_FALSE(fileRecordStore.loadData(objectId(34), 1).is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(3)}), *acceptedRootStateStore.load());
}

TEST_F(RootTransactionTestFixture, PublishCreateNestedFileWithDataChildTransactionKeepsAcceptedRootWhenRootContentStoreFails) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();
  storeConflictingRootContentForCandidate(baseRoot, rootId(3));

  EXPECT_THROW(
    publishCreateNestedFileWithDataChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      baseRoot,
      rootId(3),
      loadParentPath(baseRoot),
      objectId(31),
      objectId(32),
      "file",
      metadata(),
      metadata(),
      payload("hello")),
    std::runtime_error);

  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *acceptedRootStateStore.load());
  EXPECT_EQ(2u, publicationStore.loadAuthenticatedRoots().size());
  EXPECT_TRUE(fileRecordStore.loadData(objectId(32), 1).is_initialized());
  EXPECT_TRUE(fileRecordStore.load(objectId(31), 1).is_initialized());
  EXPECT_TRUE(directoryRecordStore.load(objectId(21), 2).is_initialized());
  EXPECT_TRUE(directoryRecordStore.load(objectId(11), 3).is_initialized());
}

TEST_F(RootTransactionTestFixture, BuildCreateNestedSymlinkChildTransactionRejectsInvalidTarget) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();

  EXPECT_THROW(
    buildCreateNestedSymlinkChildTransaction(
      baseRoot,
      rootId(3),
      loadParentPath(baseRoot),
      objectId(41),
      "link",
      metadata(),
      metadata(),
      ""),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, PublishCreateNestedSymlinkChildTransactionPublishesReachableSymlink) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();

  const auto result = publishCreateNestedSymlinkChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(3),
    loadParentPath(baseRoot),
    objectId(41),
    "link",
    metadata(0701),
    metadata(0777),
    "../target");

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::Valid, result.treeValidation->status);
  EXPECT_EQ((AuthenticatedRoot{3, rootId(3)}), *result.selectedRoot);
  EXPECT_EQ(2u, result.rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore.load(objectId(21), 2);
  ASSERT_TRUE(parent.is_initialized());
  ASSERT_EQ(1u, parent->entries.size());
  EXPECT_EQ("link", parent->entries[0].name);
  EXPECT_EQ(ObjectType::Symlink, parent->entries[0].type);
  EXPECT_EQ(objectId(41), parent->entries[0].objectId);

  const auto symlink = symlinkRecordStore.load(objectId(41), 1);
  ASSERT_TRUE(symlink.is_initialized());
  EXPECT_EQ("../target", symlink->target);
}

TEST_F(RootTransactionTestFixture, PublishCreateNestedSymlinkChildTransactionKeepsAcceptedRootWhenRootContentStoreFails) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectory();
  storeConflictingRootContentForCandidate(baseRoot, rootId(3));

  EXPECT_THROW(
    publishCreateNestedSymlinkChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      baseRoot,
      rootId(3),
      loadParentPath(baseRoot),
      objectId(41),
      "link",
      metadata(),
      metadata(),
      "../target"),
    std::runtime_error);

  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *acceptedRootStateStore.load());
  EXPECT_EQ(2u, publicationStore.loadAuthenticatedRoots().size());
  EXPECT_TRUE(symlinkRecordStore.load(objectId(41), 1).is_initialized());
  EXPECT_TRUE(directoryRecordStore.load(objectId(21), 2).is_initialized());
  EXPECT_TRUE(directoryRecordStore.load(objectId(11), 3).is_initialized());
}

TEST_F(RootTransactionTestFixture, BuildCreateRootFileChildTransactionBuildsFileAndRootUpdate) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  const auto transaction = buildCreateRootFileChildTransaction(
    baseRoot,
    rootId(2),
    objectId(31),
    "file",
    metadata(0700),
    metadata(0600));

  EXPECT_EQ(objectId(31), transaction.childFile.fileId);
  EXPECT_EQ(1u, transaction.childFile.generation);
  EXPECT_EQ(0600u, transaction.childFile.metadata.permissions);
  EXPECT_EQ(0u, transaction.childFile.size);
  EXPECT_TRUE(transaction.childFile.extents.empty());
  EXPECT_EQ(2u, transaction.rootUpdate.rootRecord.epoch);
  ASSERT_EQ(1u, transaction.rootUpdate.rootDirectory.entries.size());
  EXPECT_EQ("file", transaction.rootUpdate.rootDirectory.entries[0].name);
  EXPECT_EQ(ObjectType::File, transaction.rootUpdate.rootDirectory.entries[0].type);
  EXPECT_EQ(objectId(31), transaction.rootUpdate.rootDirectory.entries[0].objectId);
  EXPECT_EQ(1u, transaction.rootUpdate.rootDirectory.entries[0].generation);
}

TEST_F(RootTransactionTestFixture, PublishCreateRootFileChildTransactionPublishesReachableEmptyFile) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  const auto result = publishCreateRootFileChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(2),
    objectId(31),
    "file",
    metadata(0700),
    metadata(0600));

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::Valid, result.treeValidation->status);
  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *result.selectedRoot);
  ASSERT_EQ(1u, result.rootDirectory->entries.size());
  EXPECT_EQ("file", result.rootDirectory->entries[0].name);
  EXPECT_EQ(objectId(31), result.rootDirectory->entries[0].objectId);
  const auto file = fileRecordStore.load(objectId(31), 1);
  ASSERT_TRUE(file.is_initialized());
  EXPECT_EQ(0600u, file->metadata.permissions);
  EXPECT_EQ(0u, file->size);
  EXPECT_TRUE(file->extents.empty());
}

TEST_F(RootTransactionTestFixture, PublishCreateRootFileChildTransactionRejectsDuplicateNameBeforeStagingFile) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();
  const auto first = publishCreateRootFileChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(2),
    objectId(31),
    "file",
    metadata(),
    metadata());
  ASSERT_EQ(RootOpenStatus::Selected, first.status);

  EXPECT_THROW(
    publishCreateRootFileChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      first,
      rootId(3),
      objectId(32),
      "file",
      metadata(),
      metadata()),
    std::runtime_error);
  EXPECT_FALSE(fileRecordStore.load(objectId(32), 1).is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *acceptedRootStateStore.load());
}

TEST_F(RootTransactionTestFixture, BuildCreateRootFileWithDataChildTransactionBuildsFileDataAndRootUpdate) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  const auto transaction = buildCreateRootFileWithDataChildTransaction(
    baseRoot,
    rootId(2),
    objectId(31),
    objectId(32),
    "file",
    metadata(0700),
    metadata(0600),
    payload("hello"));

  EXPECT_EQ(objectId(31), transaction.childFile.fileId);
  EXPECT_EQ(1u, transaction.childFile.generation);
  EXPECT_EQ(0600u, transaction.childFile.metadata.permissions);
  EXPECT_EQ(5u, transaction.childFile.size);
  ASSERT_EQ(1u, transaction.childFile.extents.size());
  EXPECT_EQ(0u, transaction.childFile.extents[0].offset);
  EXPECT_EQ(5u, transaction.childFile.extents[0].size);
  EXPECT_EQ(objectId(32), transaction.childFile.extents[0].dataId);
  EXPECT_EQ(1u, transaction.childFile.extents[0].generation);
  EXPECT_EQ(objectId(32), transaction.childFileData.dataId);
  EXPECT_EQ(1u, transaction.childFileData.generation);
  EXPECT_EQ("hello", payloadString(transaction.childFileData.payload));
  ASSERT_EQ(1u, transaction.rootUpdate.rootDirectory.entries.size());
  EXPECT_EQ("file", transaction.rootUpdate.rootDirectory.entries[0].name);
  EXPECT_EQ(ObjectType::File, transaction.rootUpdate.rootDirectory.entries[0].type);
  EXPECT_EQ(objectId(31), transaction.rootUpdate.rootDirectory.entries[0].objectId);
}

TEST_F(RootTransactionTestFixture, BuildCreateRootFileWithDataChildTransactionRejectsEmptyPayload) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  EXPECT_THROW(
    buildCreateRootFileWithDataChildTransaction(
      baseRoot,
      rootId(2),
      objectId(31),
      objectId(32),
      "file",
      metadata(),
      metadata(),
      payload("")),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, BuildCreateRootFileWithDataChildTransactionRejectsFileIdReuseAsDataId) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  EXPECT_THROW(
    buildCreateRootFileWithDataChildTransaction(
      baseRoot,
      rootId(2),
      objectId(31),
      objectId(31),
      "file",
      metadata(),
      metadata(),
      payload("hello")),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, PublishCreateRootFileWithDataChildTransactionPublishesReachableFileData) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  const auto result = publishCreateRootFileWithDataChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(2),
    objectId(31),
    objectId(32),
    "file",
    metadata(0700),
    metadata(0600),
    payload("hello"));

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::Valid, result.treeValidation->status);
  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *result.selectedRoot);
  ASSERT_EQ(1u, result.rootDirectory->entries.size());
  EXPECT_EQ("file", result.rootDirectory->entries[0].name);
  EXPECT_EQ(objectId(31), result.rootDirectory->entries[0].objectId);

  const auto file = fileRecordStore.load(objectId(31), 1);
  ASSERT_TRUE(file.is_initialized());
  EXPECT_EQ(5u, file->size);
  ASSERT_EQ(1u, file->extents.size());
  EXPECT_EQ(objectId(32), file->extents[0].dataId);

  const auto data = fileRecordStore.loadData(objectId(32), 1);
  ASSERT_TRUE(data.is_initialized());
  EXPECT_EQ("hello", payloadString(data->payload));
}

TEST_F(RootTransactionTestFixture, PublishCreateRootFileWithDataChildTransactionRejectsDuplicateNameBeforeStagingFileData) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();
  const auto first = publishCreateRootFileWithDataChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(2),
    objectId(31),
    objectId(32),
    "file",
    metadata(),
    metadata(),
    payload("hello"));
  ASSERT_EQ(RootOpenStatus::Selected, first.status);

  EXPECT_THROW(
    publishCreateRootFileWithDataChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      first,
      rootId(3),
      objectId(33),
      objectId(34),
      "file",
      metadata(),
      metadata(),
      payload("other")),
    std::runtime_error);
  EXPECT_FALSE(fileRecordStore.load(objectId(33), 1).is_initialized());
  EXPECT_FALSE(fileRecordStore.loadData(objectId(34), 1).is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *acceptedRootStateStore.load());
}

TEST_F(RootTransactionTestFixture, PublishCreateRootFileWithDataChildTransactionKeepsAcceptedRootWhenRootContentStoreFails) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();
  storeConflictingRootContentForCandidate(baseRoot, rootId(2));

  EXPECT_THROW(
    publishCreateRootFileWithDataChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      baseRoot,
      rootId(2),
      objectId(31),
      objectId(32),
      "file",
      metadata(),
      metadata(),
      payload("hello")),
    std::runtime_error);

  EXPECT_EQ((AuthenticatedRoot{1, rootId(1)}), *acceptedRootStateStore.load());
  EXPECT_EQ(1u, publicationStore.loadAuthenticatedRoots().size());
  EXPECT_TRUE(fileRecordStore.loadData(objectId(32), 1).is_initialized());
  EXPECT_TRUE(fileRecordStore.load(objectId(31), 1).is_initialized());
  EXPECT_TRUE(directoryRecordStore.load(objectId(11), 2).is_initialized());
}

TEST_F(RootTransactionTestFixture, BuildUpdateRootFileChildTransactionBuildsNewFileGenerationAndRootReference) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithRootFile();
  const auto baseFile = fileRecordStore.load(objectId(31), 1);
  ASSERT_TRUE(baseFile.is_initialized());

  const auto transaction = buildUpdateRootFileChildTransaction(
    baseRoot,
    rootId(3),
    "file",
    FileUpdate{
      *baseFile,
      metadata(0601),
      3,
      {
        FileExtent{0, 3, objectId(33), 1}
      },
      fileDataRecords(33, "new")
    });

  EXPECT_EQ(objectId(31), transaction.file.fileId);
  EXPECT_EQ(2u, transaction.file.generation);
  EXPECT_EQ(0601u, transaction.file.metadata.permissions);
  EXPECT_EQ(3u, transaction.file.size);
  ASSERT_EQ(1u, transaction.file.extents.size());
  EXPECT_EQ(objectId(33), transaction.file.extents[0].dataId);
  ASSERT_EQ(1u, transaction.rootUpdate.rootDirectory.entries.size());
  EXPECT_EQ("file", transaction.rootUpdate.rootDirectory.entries[0].name);
  EXPECT_EQ(objectId(31), transaction.rootUpdate.rootDirectory.entries[0].objectId);
  EXPECT_EQ(2u, transaction.rootUpdate.rootDirectory.entries[0].generation);
}

TEST_F(RootTransactionTestFixture, PublishUpdateRootFileChildTransactionPublishesReachableUpdatedFile) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithRootFile();
  const auto baseFile = fileRecordStore.load(objectId(31), 1);
  ASSERT_TRUE(baseFile.is_initialized());

  const auto result = publishUpdateRootFileChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(3),
    "file",
    FileUpdate{
      *baseFile,
      metadata(0601),
      3,
      {
        FileExtent{0, 3, objectId(33), 1}
      },
      fileDataRecords(33, "new")
    });

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(3)}), *result.selectedRoot);
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_EQ(1u, result.rootDirectory->entries.size());
  EXPECT_EQ(2u, result.rootDirectory->entries[0].generation);
  EXPECT_EQ((AuthenticatedRoot{3, rootId(3)}), *acceptedRootStateStore.load());

  const auto updatedFile = fileRecordStore.load(objectId(31), 2);
  ASSERT_TRUE(updatedFile.is_initialized());
  EXPECT_EQ(0601u, updatedFile->metadata.permissions);
  EXPECT_EQ(3u, updatedFile->size);
  EXPECT_TRUE(fileRecordStore.load(objectId(31), 1).is_initialized());
  const auto data = fileRecordStore.loadData(objectId(33), 1);
  ASSERT_TRUE(data.is_initialized());
  EXPECT_EQ("new", payloadString(data->payload));
}

TEST_F(RootTransactionTestFixture, PublishUpdateNestedFileChildTransactionPublishesReachableUpdatedFile) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithNestedFile();
  const auto baseFile = fileRecordStore.load(objectId(31), 1);
  ASSERT_TRUE(baseFile.is_initialized());

  const auto result = publishUpdateNestedFileChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(4),
    loadParentPath(baseRoot),
    "file",
    FileUpdate{
      *baseFile,
      metadata(0601),
      3,
      {
        FileExtent{0, 3, objectId(33), 1}
      },
      fileDataRecords(33, "new")
    });

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{4, rootId(4)}), *result.selectedRoot);
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_EQ(1u, result.rootDirectory->entries.size());
  EXPECT_EQ(objectId(21), result.rootDirectory->entries[0].objectId);
  EXPECT_EQ(3u, result.rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore.load(objectId(21), 3);
  ASSERT_TRUE(parent.is_initialized());
  ASSERT_EQ(1u, parent->entries.size());
  EXPECT_EQ("file", parent->entries[0].name);
  EXPECT_EQ(objectId(31), parent->entries[0].objectId);
  EXPECT_EQ(2u, parent->entries[0].generation);

  const auto updatedFile = fileRecordStore.load(objectId(31), 2);
  ASSERT_TRUE(updatedFile.is_initialized());
  EXPECT_EQ(0601u, updatedFile->metadata.permissions);
  const auto data = fileRecordStore.loadData(objectId(33), 1);
  ASSERT_TRUE(data.is_initialized());
  EXPECT_EQ("new", payloadString(data->payload));
}

TEST_F(RootTransactionTestFixture, BuildUpdateRootSymlinkChildTransactionBuildsNewSymlinkGenerationAndRootReference) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithRootSymlink();
  const auto baseSymlink = symlinkRecordStore.load(objectId(41), 1);
  ASSERT_TRUE(baseSymlink.is_initialized());

  const auto transaction = buildUpdateRootSymlinkChildTransaction(
    baseRoot,
    rootId(3),
    "link",
    SymlinkUpdate{
      *baseSymlink,
      metadata(0701),
      baseSymlink->target
    });

  EXPECT_EQ(objectId(41), transaction.symlink.symlinkId);
  EXPECT_EQ(2u, transaction.symlink.generation);
  EXPECT_EQ(0701u, transaction.symlink.metadata.permissions);
  EXPECT_EQ("../target", transaction.symlink.target);
  ASSERT_EQ(1u, transaction.rootUpdate.rootDirectory.entries.size());
  EXPECT_EQ("link", transaction.rootUpdate.rootDirectory.entries[0].name);
  EXPECT_EQ(ObjectType::Symlink, transaction.rootUpdate.rootDirectory.entries[0].type);
  EXPECT_EQ(objectId(41), transaction.rootUpdate.rootDirectory.entries[0].objectId);
  EXPECT_EQ(2u, transaction.rootUpdate.rootDirectory.entries[0].generation);
}

TEST_F(RootTransactionTestFixture, PublishUpdateNestedSymlinkChildTransactionPublishesReachableUpdatedSymlink) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithNestedSymlink();
  const auto baseSymlink = symlinkRecordStore.load(objectId(41), 1);
  ASSERT_TRUE(baseSymlink.is_initialized());

  const auto result = publishUpdateNestedSymlinkChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(4),
    loadParentPath(baseRoot),
    "link",
    SymlinkUpdate{
      *baseSymlink,
      metadata(0701),
      baseSymlink->target
    });

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{4, rootId(4)}), *result.selectedRoot);
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_EQ(1u, result.rootDirectory->entries.size());
  EXPECT_EQ(objectId(21), result.rootDirectory->entries[0].objectId);
  EXPECT_EQ(3u, result.rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore.load(objectId(21), 3);
  ASSERT_TRUE(parent.is_initialized());
  ASSERT_EQ(1u, parent->entries.size());
  EXPECT_EQ("link", parent->entries[0].name);
  EXPECT_EQ(ObjectType::Symlink, parent->entries[0].type);
  EXPECT_EQ(objectId(41), parent->entries[0].objectId);
  EXPECT_EQ(2u, parent->entries[0].generation);

  const auto updatedSymlink = symlinkRecordStore.load(objectId(41), 2);
  ASSERT_TRUE(updatedSymlink.is_initialized());
  EXPECT_EQ(0701u, updatedSymlink->metadata.permissions);
  EXPECT_EQ("../target", updatedSymlink->target);
  EXPECT_TRUE(symlinkRecordStore.load(objectId(41), 1).is_initialized());
}

TEST_F(RootTransactionTestFixture, PublishUpdateNestedDirectoryMetadataTransactionUpdatesDirectoryAndAncestors) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithNestedDirectory();

  const auto result = publishUpdateNestedDirectoryMetadataTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(4),
    loadChildPath(baseRoot),
    metadata(0712));

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{4, rootId(4)}), *result.selectedRoot);
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_EQ(1u, result.rootDirectory->entries.size());
  EXPECT_EQ("parent", result.rootDirectory->entries[0].name);
  EXPECT_EQ(objectId(21), result.rootDirectory->entries[0].objectId);
  EXPECT_EQ(3u, result.rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore.load(objectId(21), 3);
  ASSERT_TRUE(parent.is_initialized());
  ASSERT_EQ(1u, parent->entries.size());
  EXPECT_EQ("child", parent->entries[0].name);
  EXPECT_EQ(objectId(41), parent->entries[0].objectId);
  EXPECT_EQ(2u, parent->entries[0].generation);

  const auto child = directoryRecordStore.load(objectId(41), 2);
  ASSERT_TRUE(child.is_initialized());
  EXPECT_EQ(0712u, child->metadata.permissions);
  EXPECT_TRUE(directoryRecordStore.load(objectId(41), 1).is_initialized());
}

TEST_F(RootTransactionTestFixture, BuildUpdateNestedDirectoryMetadataTransactionRejectsRootPath) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  EXPECT_THROW(
    buildUpdateNestedDirectoryMetadataTransaction(
      baseRoot,
      rootId(2),
      {},
      metadata(0711)),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, BuildRemoveRootChildTransactionBuildsRootWithoutChild) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithRootFile();

  const auto transaction = buildRemoveRootChildTransaction(
    baseRoot,
    rootId(3),
    DirectoryChildReference{"file", ObjectType::File, objectId(31), 1},
    metadata(0701));

  EXPECT_EQ(3u, transaction.rootUpdate.rootRecord.epoch);
  EXPECT_EQ(objectId(11), transaction.rootUpdate.rootDirectory.directoryId);
  EXPECT_EQ(3u, transaction.rootUpdate.rootDirectory.generation);
  EXPECT_EQ(0701u, transaction.rootUpdate.rootDirectory.metadata.permissions);
  EXPECT_TRUE(transaction.rootUpdate.rootDirectory.entries.empty());
}

TEST_F(RootTransactionTestFixture, BuildRemoveRootChildTransactionRejectsStaleChildReference) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithRootFile();

  EXPECT_THROW(
    buildRemoveRootChildTransaction(
      baseRoot,
      rootId(3),
      DirectoryChildReference{"file", ObjectType::File, objectId(31), 2},
      metadata()),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, PublishRemoveRootChildTransactionRemovesReachableFile) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithRootFile();

  const auto result = publishRemoveRootChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(3),
    DirectoryChildReference{"file", ObjectType::File, objectId(31), 1},
    metadata(0701));

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(3)}), *result.selectedRoot);
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  EXPECT_TRUE(result.rootDirectory->entries.empty());
  EXPECT_EQ(0701u, result.rootDirectory->metadata.permissions);
  EXPECT_EQ((AuthenticatedRoot{3, rootId(3)}), *acceptedRootStateStore.load());
  EXPECT_TRUE(fileRecordStore.load(objectId(31), 1).is_initialized());
  EXPECT_TRUE(fileRecordStore.loadData(objectId(32), 1).is_initialized());
}

TEST_F(RootTransactionTestFixture, PublishRemoveNestedChildTransactionRemovesReachableFileAndUpdatesAncestors) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithNestedFile();

  const auto result = publishRemoveNestedChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(4),
    loadParentPath(baseRoot),
    DirectoryChildReference{"file", ObjectType::File, objectId(31), 1},
    metadata(0701));

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{4, rootId(4)}), *result.selectedRoot);
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_EQ(1u, result.rootDirectory->entries.size());
  EXPECT_EQ("parent", result.rootDirectory->entries[0].name);
  EXPECT_EQ(objectId(21), result.rootDirectory->entries[0].objectId);
  EXPECT_EQ(3u, result.rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore.load(objectId(21), 3);
  ASSERT_TRUE(parent.is_initialized());
  EXPECT_EQ(0701u, parent->metadata.permissions);
  EXPECT_TRUE(parent->entries.empty());
  EXPECT_TRUE(fileRecordStore.load(objectId(31), 1).is_initialized());
  EXPECT_TRUE(fileRecordStore.loadData(objectId(32), 1).is_initialized());
}

TEST_F(RootTransactionTestFixture, BuildMoveChildTransactionRenamesRootChild) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithRootFile();

  const auto transaction = buildMoveChildTransaction(
    baseRoot,
    rootId(3),
    {},
    DirectoryChildReference{"file", ObjectType::File, objectId(31), 1},
    {},
    "renamed",
    metadata(0701),
    metadata(0702));

  EXPECT_TRUE(transaction.pathUpdate.stagedDirectories.empty());
  EXPECT_EQ(3u, transaction.pathUpdate.rootUpdate.rootRecord.epoch);
  EXPECT_EQ(0702u, transaction.pathUpdate.rootUpdate.rootDirectory.metadata.permissions);
  ASSERT_EQ(1u, transaction.pathUpdate.rootUpdate.rootDirectory.entries.size());
  EXPECT_EQ("renamed", transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].name);
  EXPECT_EQ(ObjectType::File, transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].type);
  EXPECT_EQ(objectId(31), transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].objectId);
  EXPECT_EQ(1u, transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].generation);
}

TEST_F(RootTransactionTestFixture, BuildMoveChildTransactionMovesRootChildIntoNestedDirectory) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectoryAndRootFile();

  const auto transaction = buildMoveChildTransaction(
    baseRoot,
    rootId(4),
    {},
    DirectoryChildReference{"file", ObjectType::File, objectId(31), 1},
    loadParentPath(baseRoot),
    "moved",
    metadata(0701),
    metadata(0712));

  ASSERT_EQ(1u, transaction.pathUpdate.stagedDirectories.size());
  const DirectoryRecord &parent = transaction.pathUpdate.stagedDirectories[0];
  EXPECT_EQ(objectId(21), parent.directoryId);
  EXPECT_EQ(2u, parent.generation);
  EXPECT_EQ(0712u, parent.metadata.permissions);
  ASSERT_EQ(1u, parent.entries.size());
  EXPECT_EQ("moved", parent.entries[0].name);
  EXPECT_EQ(objectId(31), parent.entries[0].objectId);

  ASSERT_EQ(1u, transaction.pathUpdate.rootUpdate.rootDirectory.entries.size());
  EXPECT_EQ("parent", transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].name);
  EXPECT_EQ(objectId(21), transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].objectId);
  EXPECT_EQ(2u, transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].generation);
  EXPECT_EQ(0701u, transaction.pathUpdate.rootUpdate.rootDirectory.metadata.permissions);
}

TEST_F(RootTransactionTestFixture, PublishMoveChildTransactionMovesNestedChildToRootAndUpdatesAncestors) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithNestedFile();

  const auto result = publishMoveChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(4),
    loadParentPath(baseRoot),
    DirectoryChildReference{"file", ObjectType::File, objectId(31), 1},
    {},
    "moved",
    metadata(0701),
    metadata(0702));

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{4, rootId(4)}), *result.selectedRoot);
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_EQ(2u, result.rootDirectory->entries.size());
  EXPECT_EQ("moved", result.rootDirectory->entries[0].name);
  EXPECT_EQ(objectId(31), result.rootDirectory->entries[0].objectId);
  EXPECT_EQ(1u, result.rootDirectory->entries[0].generation);
  EXPECT_EQ("parent", result.rootDirectory->entries[1].name);
  EXPECT_EQ(objectId(21), result.rootDirectory->entries[1].objectId);
  EXPECT_EQ(3u, result.rootDirectory->entries[1].generation);

  const auto parent = directoryRecordStore.load(objectId(21), 3);
  ASSERT_TRUE(parent.is_initialized());
  EXPECT_EQ(0701u, parent->metadata.permissions);
  EXPECT_TRUE(parent->entries.empty());
  EXPECT_TRUE(fileRecordStore.load(objectId(31), 1).is_initialized());
  EXPECT_TRUE(fileRecordStore.loadData(objectId(32), 1).is_initialized());
}

TEST_F(RootTransactionTestFixture, BuildMoveChildTransactionRejectsExistingTarget) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectoryAndRootFile();

  EXPECT_THROW(
    buildMoveChildTransaction(
      baseRoot,
      rootId(4),
      {},
      DirectoryChildReference{"file", ObjectType::File, objectId(31), 1},
      {},
      "parent",
      metadata(),
      metadata()),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, BuildMoveChildTransactionRejectsDirectoryMovedIntoDescendant) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithNestedDirectory();

  EXPECT_THROW(
    buildMoveChildTransaction(
      baseRoot,
      rootId(4),
      {},
      DirectoryChildReference{"parent", ObjectType::Directory, objectId(21), 2},
      loadChildPath(baseRoot),
      "moved",
      metadata(),
      metadata()),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, BuildMoveChildReplacingTransactionReplacesRootTarget) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithTwoRootFiles();

  const auto transaction = buildMoveChildReplacingTransaction(
    baseRoot,
    rootId(4),
    {},
    DirectoryChildReference{"file", ObjectType::File, objectId(31), 1},
    {},
    DirectoryChildReference{"target", ObjectType::File, objectId(41), 1},
    "target",
    metadata(0701),
    metadata(0702));

  EXPECT_TRUE(transaction.pathUpdate.stagedDirectories.empty());
  EXPECT_EQ(4u, transaction.pathUpdate.rootUpdate.rootRecord.epoch);
  EXPECT_EQ(0702u, transaction.pathUpdate.rootUpdate.rootDirectory.metadata.permissions);
  ASSERT_EQ(1u, transaction.pathUpdate.rootUpdate.rootDirectory.entries.size());
  EXPECT_EQ("target", transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].name);
  EXPECT_EQ(ObjectType::File, transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].type);
  EXPECT_EQ(objectId(31), transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].objectId);
  EXPECT_EQ(1u, transaction.pathUpdate.rootUpdate.rootDirectory.entries[0].generation);
}

TEST_F(RootTransactionTestFixture, PublishMoveChildReplacingTransactionReplacesNestedTargetAndKeepsOldObjectsReadableByIdentity) {
  const RootOpenWithValidatedTreeResult targetRoot = createBaseRootWithNestedFile();
  const RootOpenWithValidatedTreeResult baseRoot = publishCreateRootFileChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    targetRoot,
    rootId(4),
    objectId(41),
    "source",
    metadata(),
    metadata(0600));

  const auto result = publishMoveChildReplacingTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(5),
    {},
    DirectoryChildReference{"source", ObjectType::File, objectId(41), 1},
    loadParentPath(baseRoot),
    DirectoryChildReference{"file", ObjectType::File, objectId(31), 1},
    "file",
    metadata(0701),
    metadata(0702));

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{5, rootId(5)}), *result.selectedRoot);
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_EQ(1u, result.rootDirectory->entries.size());
  EXPECT_EQ("parent", result.rootDirectory->entries[0].name);
  EXPECT_EQ(objectId(21), result.rootDirectory->entries[0].objectId);
  EXPECT_EQ(3u, result.rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore.load(objectId(21), 3);
  ASSERT_TRUE(parent.is_initialized());
  EXPECT_EQ(0702u, parent->metadata.permissions);
  ASSERT_EQ(1u, parent->entries.size());
  EXPECT_EQ("file", parent->entries[0].name);
  EXPECT_EQ(objectId(41), parent->entries[0].objectId);
  EXPECT_TRUE(fileRecordStore.load(objectId(31), 1).is_initialized());
  EXPECT_TRUE(fileRecordStore.loadData(objectId(32), 1).is_initialized());
}

TEST_F(RootTransactionTestFixture, BuildMoveChildReplacingTransactionRejectsDirectoryNonDirectoryReplacement) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRootWithParentDirectoryAndRootFile();

  EXPECT_THROW(
    buildMoveChildReplacingTransaction(
      baseRoot,
      rootId(4),
      {},
      DirectoryChildReference{"file", ObjectType::File, objectId(31), 1},
      {},
      DirectoryChildReference{"parent", ObjectType::Directory, objectId(21), 1},
      "parent",
      metadata(),
      metadata()),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, BuildCreateRootSymlinkChildTransactionBuildsSymlinkAndRootUpdate) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  const auto transaction = buildCreateRootSymlinkChildTransaction(
    baseRoot,
    rootId(2),
    objectId(41),
    "link",
    metadata(0700),
    metadata(0777),
    "../target");

  EXPECT_EQ(objectId(41), transaction.childSymlink.symlinkId);
  EXPECT_EQ(1u, transaction.childSymlink.generation);
  EXPECT_EQ(0777u, transaction.childSymlink.metadata.permissions);
  EXPECT_EQ("../target", transaction.childSymlink.target);
  EXPECT_EQ(2u, transaction.rootUpdate.rootRecord.epoch);
  ASSERT_EQ(1u, transaction.rootUpdate.rootDirectory.entries.size());
  EXPECT_EQ("link", transaction.rootUpdate.rootDirectory.entries[0].name);
  EXPECT_EQ(ObjectType::Symlink, transaction.rootUpdate.rootDirectory.entries[0].type);
  EXPECT_EQ(objectId(41), transaction.rootUpdate.rootDirectory.entries[0].objectId);
  EXPECT_EQ(1u, transaction.rootUpdate.rootDirectory.entries[0].generation);
}

TEST_F(RootTransactionTestFixture, BuildCreateRootSymlinkChildTransactionRejectsInvalidTarget) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  EXPECT_THROW(
    buildCreateRootSymlinkChildTransaction(
      baseRoot,
      rootId(2),
      objectId(41),
      "link",
      metadata(),
      metadata(),
      ""),
    std::runtime_error);
}

TEST_F(RootTransactionTestFixture, PublishCreateRootSymlinkChildTransactionPublishesReachableSymlink) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();

  const auto result = publishCreateRootSymlinkChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(2),
    objectId(41),
    "link",
    metadata(0700),
    metadata(0777),
    "../target");

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_TRUE(result.treeValidation.is_initialized());
  EXPECT_EQ(ObjectTreeValidationStatus::Valid, result.treeValidation->status);
  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *result.selectedRoot);
  ASSERT_EQ(1u, result.rootDirectory->entries.size());
  EXPECT_EQ("link", result.rootDirectory->entries[0].name);
  EXPECT_EQ(objectId(41), result.rootDirectory->entries[0].objectId);
  const auto symlink = symlinkRecordStore.load(objectId(41), 1);
  ASSERT_TRUE(symlink.is_initialized());
  EXPECT_EQ(0777u, symlink->metadata.permissions);
  EXPECT_EQ("../target", symlink->target);
}

TEST_F(RootTransactionTestFixture, PublishCreateRootSymlinkChildTransactionRejectsDuplicateNameBeforeStagingSymlink) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();
  const auto first = publishCreateRootSymlinkChildTransaction(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    baseRoot,
    rootId(2),
    objectId(41),
    "link",
    metadata(),
    metadata(),
    "../target");
  ASSERT_EQ(RootOpenStatus::Selected, first.status);

  EXPECT_THROW(
    publishCreateRootSymlinkChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      first,
      rootId(3),
      objectId(42),
      "link",
      metadata(),
      metadata(),
      "../other-target"),
    std::runtime_error);
  EXPECT_FALSE(symlinkRecordStore.load(objectId(42), 1).is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(2)}), *acceptedRootStateStore.load());
}

TEST_F(RootTransactionTestFixture, PublishCreateRootSymlinkChildTransactionKeepsAcceptedRootWhenRootContentStoreFails) {
  const RootOpenWithValidatedTreeResult baseRoot = createBaseRoot();
  storeConflictingRootContentForCandidate(baseRoot, rootId(2));

  EXPECT_THROW(
    publishCreateRootSymlinkChildTransaction(
      publicationStore,
      acceptedRootStateStore,
      rootContentStore,
      directoryRecordStore,
      fileRecordStore,
      symlinkRecordStore,
      baseRoot,
      rootId(2),
      objectId(41),
      "link",
      metadata(),
      metadata(),
      "../target"),
    std::runtime_error);

  EXPECT_EQ((AuthenticatedRoot{1, rootId(1)}), *acceptedRootStateStore.load());
  EXPECT_EQ(1u, publicationStore.loadAuthenticatedRoots().size());
  EXPECT_TRUE(symlinkRecordStore.load(objectId(41), 1).is_initialized());
  EXPECT_TRUE(directoryRecordStore.load(objectId(11), 2).is_initialized());
}
