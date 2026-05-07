#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/Volume.h>

#include <boost/filesystem.hpp>
#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

using cryfs::formatv2::AuthenticatedRoot;
using cryfs::formatv2::DirectoryEntry;
using cryfs::formatv2::DirectoryMetadata;
using cryfs::formatv2::DirectoryRecordStore;
using cryfs::formatv2::FileRecordStore;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::ObjectType;
using cryfs::formatv2::RootId;
using cryfs::formatv2::RootOpenStatus;
using cryfs::formatv2::SymlinkRecordStore;
using cryfs::formatv2::Timestamp;
using cryfs::formatv2::VolumeLayout;
using cryfs::formatv2::createOrOpenInitialEmptyVolume;
using cryfs::formatv2::deriveObjectEncryptionKey;
using cryfs::formatv2::initialRootDirectoryMetadata;
using cryfs::formatv2::openVolumeRoot;
using cryfs::formatv2::publishVolumeCreateNestedDirectoryChildTransaction;
using cryfs::formatv2::publishVolumeCreateNestedFileChildTransaction;
using cryfs::formatv2::publishVolumeCreateNestedFileWithDataChildTransaction;
using cryfs::formatv2::publishVolumeCreateNestedSymlinkChildTransaction;
using cryfs::formatv2::publishVolumeCreateRootDirectoryChildTransaction;
using cryfs::formatv2::publishVolumeCreateRootFileChildTransaction;
using cryfs::formatv2::publishVolumeCreateRootFileWithDataChildTransaction;
using cryfs::formatv2::publishVolumeCreateRootSymlinkChildTransaction;
using cryfs::formatv2::publishVolumeRootDirectoryTransaction;
using cryfs::formatv2::volumeLayout;

namespace bf = boost::filesystem;

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

template<class Id>
cpputils::Data dataForId(const Id &id) {
  cpputils::Data data(Id::BINARY_LENGTH);
  id.ToBinary(data.data());
  return data;
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

cpputils::Data payload(const std::string &value) {
  cpputils::Data data(value.size());
  std::memcpy(data.data(), value.data(), value.size());
  return data;
}

class SequenceRandomGenerator final : public cpputils::RandomGenerator {
public:
  SequenceRandomGenerator()
    : _outputs(), _next(0) {
  }

  void add(cpputils::Data output) {
    _outputs.emplace_back(std::move(output));
  }

private:
  void _get(void *target, size_t bytes) override {
    ASSERT_LT(_next, _outputs.size());
    ASSERT_EQ(bytes, _outputs[_next].size());
    std::memcpy(target, _outputs[_next].data(), bytes);
    ++_next;
  }

  std::vector<cpputils::Data> _outputs;
  size_t _next;
};

class FormatV2VolumeTest : public ::testing::Test {
protected:
  VolumeLayout layout() const {
    return volumeLayout(_baseDir.path(), _localStateDir.path());
  }

  DirectoryRecordStore directoryRecordStore() const {
    return DirectoryRecordStore(
      layout().directoriesDirectory,
      filesystemId(),
      deriveObjectEncryptionKey(masterKey(), filesystemId()));
  }

  FileRecordStore fileRecordStore() const {
    return FileRecordStore(
      layout().filesDirectory,
      filesystemId(),
      deriveObjectEncryptionKey(masterKey(), filesystemId()));
  }

  SymlinkRecordStore symlinkRecordStore() const {
    return SymlinkRecordStore(
      layout().symlinksDirectory,
      filesystemId(),
      deriveObjectEncryptionKey(masterKey(), filesystemId()));
  }

  cpputils::TempDir _baseDir;
  cpputils::TempDir _localStateDir;
};

}

TEST_F(FormatV2VolumeTest, VolumeLayoutUsesDedicatedFormatV2Directories) {
  const VolumeLayout result = layout();

  EXPECT_EQ(_baseDir.path() / "format-v2" / "roots", result.rootsDirectory);
  EXPECT_EQ(_baseDir.path() / "format-v2" / "root-content", result.rootContentDirectory);
  EXPECT_EQ(_baseDir.path() / "format-v2" / "objects" / "directories", result.directoriesDirectory);
  EXPECT_EQ(_baseDir.path() / "format-v2" / "objects" / "files", result.filesDirectory);
  EXPECT_EQ(_baseDir.path() / "format-v2" / "objects" / "symlinks", result.symlinksDirectory);
  EXPECT_EQ(_localStateDir.path() / "format-v2" / "accepted-root", result.acceptedRootFile);
}

TEST_F(FormatV2VolumeTest, InitialRootDirectoryMetadataIsValidAndPrivate) {
  const DirectoryMetadata result = initialRootDirectoryMetadata();

  EXPECT_EQ(0700u, result.permissions);
  EXPECT_EQ(0u, result.uid);
  EXPECT_EQ(0u, result.gid);
  EXPECT_EQ((Timestamp{0, 0}), result.atime);
  EXPECT_EQ((Timestamp{0, 0}), result.mtime);
  EXPECT_EQ((Timestamp{0, 0}), result.ctime);
}

TEST_F(FormatV2VolumeTest, CreatesInitialEmptyRootAndCanOpenItAgain) {
  SequenceRandomGenerator random;
  random.add(dataForId(rootId(1)));
  random.add(dataForId(objectId(2)));

  const auto created = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &random,
    metadata());

  ASSERT_EQ(RootOpenStatus::Selected, created.status);
  ASSERT_TRUE(created.selectedRoot.is_initialized());
  ASSERT_TRUE(created.rootDirectory.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{1, rootId(1)}), *created.selectedRoot);
  EXPECT_EQ(objectId(2), created.rootDirectory->directoryId);
  EXPECT_TRUE(created.rootDirectory->entries.empty());
  EXPECT_TRUE(bf::exists(layout().acceptedRootFile));

  const auto opened = openVolumeRoot(layout(), filesystemId(), masterKey());

  ASSERT_EQ(RootOpenStatus::Selected, opened.status);
  ASSERT_TRUE(opened.selectedRoot.is_initialized());
  ASSERT_TRUE(opened.rootDirectory.is_initialized());
  EXPECT_EQ(*created.selectedRoot, *opened.selectedRoot);
  EXPECT_TRUE(opened.rootDirectory->entries.empty());
}

TEST_F(FormatV2VolumeTest, CreateOrOpenInitialEmptyVolumeIsIdempotentForExistingInitialRoot) {
  SequenceRandomGenerator firstRandom;
  firstRandom.add(dataForId(rootId(1)));
  firstRandom.add(dataForId(objectId(2)));
  const auto first = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &firstRandom,
    metadata());

  SequenceRandomGenerator secondRandom;
  secondRandom.add(dataForId(rootId(3)));
  secondRandom.add(dataForId(objectId(4)));
  const auto second = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &secondRandom,
    metadata());

  ASSERT_EQ(RootOpenStatus::Selected, first.status);
  ASSERT_EQ(RootOpenStatus::Selected, second.status);
  ASSERT_TRUE(first.selectedRoot.is_initialized());
  ASSERT_TRUE(second.selectedRoot.is_initialized());
  EXPECT_EQ(*first.selectedRoot, *second.selectedRoot);
  EXPECT_EQ((AuthenticatedRoot{1, rootId(1)}), *second.selectedRoot);
}

TEST_F(FormatV2VolumeTest, RejectsMasterKeysWithWrongSize) {
  SequenceRandomGenerator random;
  random.add(dataForId(rootId(1)));
  random.add(dataForId(objectId(2)));

  EXPECT_THROW(
    createOrOpenInitialEmptyVolume(
      layout(),
      filesystemId(),
      cpputils::EncryptionKey::Null(FORMAT_V2_MASTER_KEY_SIZE - 1),
      &random,
      metadata()),
    std::runtime_error);
}

TEST_F(FormatV2VolumeTest, PublishesRootDirectoryTransactionFromCurrentVolume) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator transactionRandom;
  transactionRandom.add(dataForId(rootId(3)));
  const auto published = publishVolumeRootDirectoryTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &transactionRandom,
    metadata(0700),
    {});

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, published.status);
  ASSERT_TRUE(published.selectedRoot.is_initialized());
  ASSERT_TRUE(published.rootDirectory.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(3)}), *published.selectedRoot);
  EXPECT_EQ(objectId(2), published.rootDirectory->directoryId);
  EXPECT_EQ(2u, published.rootDirectory->generation);
  EXPECT_EQ(0700u, published.rootDirectory->metadata.permissions);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ(*published.selectedRoot, *reopened.selectedRoot);
}

TEST_F(FormatV2VolumeTest, PublishRootDirectoryTransactionRequiresSelectedBaseRoot) {
  SequenceRandomGenerator random;

  EXPECT_THROW(
    publishVolumeRootDirectoryTransaction(
      layout(),
      filesystemId(),
      masterKey(),
      &random,
      metadata(),
      {}),
    std::runtime_error);
}

TEST_F(FormatV2VolumeTest, PublishRootDirectoryTransactionDoesNotPublishInvalidTree) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator transactionRandom;
  transactionRandom.add(dataForId(rootId(3)));
  const auto published = publishVolumeRootDirectoryTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &transactionRandom,
    metadata(),
    {
      DirectoryEntry{"missing", ObjectType::File, objectId(4), 1}
    });

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  EXPECT_EQ(RootOpenStatus::RootTreeInvalid, published.status);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{1, rootId(1)}), *reopened.selectedRoot);
}

TEST_F(FormatV2VolumeTest, PublishesCreateRootDirectoryChildTransactionFromCurrentVolume) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator transactionRandom;
  transactionRandom.add(dataForId(rootId(3)));
  transactionRandom.add(dataForId(objectId(4)));
  const auto published = publishVolumeCreateRootDirectoryChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &transactionRandom,
    "child",
    metadata(0700),
    metadata(0711));

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, published.status);
  ASSERT_TRUE(published.selectedRoot.is_initialized());
  ASSERT_TRUE(published.rootDirectory.is_initialized());
  ASSERT_TRUE(published.treeValidation.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(3)}), *published.selectedRoot);
  EXPECT_EQ(cryfs::formatv2::ObjectTreeValidationStatus::Valid, published.treeValidation->status);
  ASSERT_EQ(1u, published.rootDirectory->entries.size());
  EXPECT_EQ("child", published.rootDirectory->entries[0].name);
  EXPECT_EQ(ObjectType::Directory, published.rootDirectory->entries[0].type);
  EXPECT_EQ(objectId(4), published.rootDirectory->entries[0].objectId);
  EXPECT_EQ(1u, published.rootDirectory->entries[0].generation);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  ASSERT_TRUE(reopened.rootDirectory.is_initialized());
  EXPECT_EQ(*published.selectedRoot, *reopened.selectedRoot);
  ASSERT_EQ(1u, reopened.rootDirectory->entries.size());
  EXPECT_EQ("child", reopened.rootDirectory->entries[0].name);
}

TEST_F(FormatV2VolumeTest, PublishCreateRootDirectoryChildTransactionRejectsDuplicateName) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator firstTransactionRandom;
  firstTransactionRandom.add(dataForId(rootId(3)));
  firstTransactionRandom.add(dataForId(objectId(4)));
  const auto first = publishVolumeCreateRootDirectoryChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &firstTransactionRandom,
    "child",
    metadata(),
    metadata());

  SequenceRandomGenerator secondTransactionRandom;
  secondTransactionRandom.add(dataForId(rootId(5)));
  secondTransactionRandom.add(dataForId(objectId(6)));

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, first.status);
  EXPECT_THROW(
    publishVolumeCreateRootDirectoryChildTransaction(
      layout(),
      filesystemId(),
      masterKey(),
      &secondTransactionRandom,
      "child",
      metadata(),
      metadata()),
    std::runtime_error);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(3)}), *reopened.selectedRoot);
}

TEST_F(FormatV2VolumeTest, PublishesCreateNestedDirectoryChildTransactionFromCurrentVolume) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  const auto parent = publishVolumeCreateRootDirectoryChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &parentRandom,
    "parent",
    metadata(),
    metadata(0711));

  SequenceRandomGenerator childRandom;
  childRandom.add(dataForId(rootId(5)));
  childRandom.add(dataForId(objectId(6)));
  const auto published = publishVolumeCreateNestedDirectoryChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &childRandom,
    {"parent"},
    "child",
    metadata(0701),
    metadata(0711));

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, parent.status);
  ASSERT_EQ(RootOpenStatus::Selected, published.status);
  ASSERT_TRUE(published.selectedRoot.is_initialized());
  ASSERT_TRUE(published.rootDirectory.is_initialized());
  ASSERT_TRUE(published.treeValidation.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(5)}), *published.selectedRoot);
  EXPECT_EQ(cryfs::formatv2::ObjectTreeValidationStatus::Valid, published.treeValidation->status);
  ASSERT_EQ(1u, published.rootDirectory->entries.size());
  EXPECT_EQ("parent", published.rootDirectory->entries[0].name);
  EXPECT_EQ(objectId(4), published.rootDirectory->entries[0].objectId);
  EXPECT_EQ(2u, published.rootDirectory->entries[0].generation);

  const auto parentDirectory = directoryRecordStore().load(objectId(4), 2);
  ASSERT_TRUE(parentDirectory.is_initialized());
  ASSERT_EQ(1u, parentDirectory->entries.size());
  EXPECT_EQ("child", parentDirectory->entries[0].name);
  EXPECT_EQ(objectId(6), parentDirectory->entries[0].objectId);
  EXPECT_EQ(1u, parentDirectory->entries[0].generation);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ(*published.selectedRoot, *reopened.selectedRoot);
}

TEST_F(FormatV2VolumeTest, PublishesCreateNestedFileChildTransactionFromCurrentVolume) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  const auto parent = publishVolumeCreateRootDirectoryChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &parentRandom,
    "parent",
    metadata(),
    metadata(0711));

  SequenceRandomGenerator childRandom;
  childRandom.add(dataForId(rootId(5)));
  childRandom.add(dataForId(objectId(6)));
  const auto published = publishVolumeCreateNestedFileChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &childRandom,
    {"parent"},
    "file",
    metadata(0701),
    metadata(0600));

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, parent.status);
  ASSERT_EQ(RootOpenStatus::Selected, published.status);
  ASSERT_TRUE(published.selectedRoot.is_initialized());
  ASSERT_TRUE(published.rootDirectory.is_initialized());
  ASSERT_TRUE(published.treeValidation.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(5)}), *published.selectedRoot);
  EXPECT_EQ(cryfs::formatv2::ObjectTreeValidationStatus::Valid, published.treeValidation->status);
  EXPECT_EQ(2u, published.rootDirectory->entries[0].generation);

  const auto parentDirectory = directoryRecordStore().load(objectId(4), 2);
  ASSERT_TRUE(parentDirectory.is_initialized());
  ASSERT_EQ(1u, parentDirectory->entries.size());
  EXPECT_EQ("file", parentDirectory->entries[0].name);
  EXPECT_EQ(ObjectType::File, parentDirectory->entries[0].type);
  EXPECT_EQ(objectId(6), parentDirectory->entries[0].objectId);
  EXPECT_TRUE(fileRecordStore().load(objectId(6), 1).is_initialized());
}

TEST_F(FormatV2VolumeTest, PublishesCreateNestedFileWithDataChildTransactionFromCurrentVolume) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  const auto parent = publishVolumeCreateRootDirectoryChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &parentRandom,
    "parent",
    metadata(),
    metadata(0711));

  SequenceRandomGenerator childRandom;
  childRandom.add(dataForId(rootId(5)));
  childRandom.add(dataForId(objectId(6)));
  childRandom.add(dataForId(objectId(7)));
  const auto published = publishVolumeCreateNestedFileWithDataChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &childRandom,
    {"parent"},
    "file",
    metadata(0701),
    metadata(0600),
    payload("hello"));

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, parent.status);
  ASSERT_EQ(RootOpenStatus::Selected, published.status);
  ASSERT_TRUE(published.selectedRoot.is_initialized());
  ASSERT_TRUE(published.rootDirectory.is_initialized());
  ASSERT_TRUE(published.treeValidation.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(5)}), *published.selectedRoot);
  EXPECT_EQ(cryfs::formatv2::ObjectTreeValidationStatus::Valid, published.treeValidation->status);
  EXPECT_EQ(2u, published.rootDirectory->entries[0].generation);

  const auto parentDirectory = directoryRecordStore().load(objectId(4), 2);
  ASSERT_TRUE(parentDirectory.is_initialized());
  ASSERT_EQ(1u, parentDirectory->entries.size());
  EXPECT_EQ("file", parentDirectory->entries[0].name);
  EXPECT_EQ(ObjectType::File, parentDirectory->entries[0].type);
  EXPECT_EQ(objectId(6), parentDirectory->entries[0].objectId);
  const auto file = fileRecordStore().load(objectId(6), 1);
  ASSERT_TRUE(file.is_initialized());
  EXPECT_EQ(5u, file->size);
  ASSERT_EQ(1u, file->extents.size());
  EXPECT_EQ(objectId(7), file->extents[0].dataId);
  EXPECT_TRUE(fileRecordStore().loadData(objectId(7), 1).is_initialized());
}

TEST_F(FormatV2VolumeTest, PublishesCreateNestedSymlinkChildTransactionFromCurrentVolume) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  const auto parent = publishVolumeCreateRootDirectoryChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &parentRandom,
    "parent",
    metadata(),
    metadata(0711));

  SequenceRandomGenerator childRandom;
  childRandom.add(dataForId(rootId(5)));
  childRandom.add(dataForId(objectId(6)));
  const auto published = publishVolumeCreateNestedSymlinkChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &childRandom,
    {"parent"},
    "link",
    metadata(0701),
    metadata(0777),
    "../target");

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, parent.status);
  ASSERT_EQ(RootOpenStatus::Selected, published.status);
  ASSERT_TRUE(published.selectedRoot.is_initialized());
  ASSERT_TRUE(published.rootDirectory.is_initialized());
  ASSERT_TRUE(published.treeValidation.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(5)}), *published.selectedRoot);
  EXPECT_EQ(cryfs::formatv2::ObjectTreeValidationStatus::Valid, published.treeValidation->status);
  EXPECT_EQ(2u, published.rootDirectory->entries[0].generation);

  const auto parentDirectory = directoryRecordStore().load(objectId(4), 2);
  ASSERT_TRUE(parentDirectory.is_initialized());
  ASSERT_EQ(1u, parentDirectory->entries.size());
  EXPECT_EQ("link", parentDirectory->entries[0].name);
  EXPECT_EQ(ObjectType::Symlink, parentDirectory->entries[0].type);
  EXPECT_EQ(objectId(6), parentDirectory->entries[0].objectId);
  const auto symlink = symlinkRecordStore().load(objectId(6), 1);
  ASSERT_TRUE(symlink.is_initialized());
  EXPECT_EQ("../target", symlink->target);
}

TEST_F(FormatV2VolumeTest, PublishCreateNestedDirectoryChildTransactionRejectsMissingParentPath) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator childRandom;
  childRandom.add(dataForId(rootId(3)));
  childRandom.add(dataForId(objectId(4)));

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  EXPECT_THROW(
    publishVolumeCreateNestedDirectoryChildTransaction(
      layout(),
      filesystemId(),
      masterKey(),
      &childRandom,
      {"missing"},
      "child",
      metadata(),
      metadata()),
    std::runtime_error);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{1, rootId(1)}), *reopened.selectedRoot);
}

TEST_F(FormatV2VolumeTest, PublishesCreateRootFileChildTransactionFromCurrentVolume) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator transactionRandom;
  transactionRandom.add(dataForId(rootId(3)));
  transactionRandom.add(dataForId(objectId(4)));
  const auto published = publishVolumeCreateRootFileChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &transactionRandom,
    "file",
    metadata(0700),
    metadata(0600));

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, published.status);
  ASSERT_TRUE(published.selectedRoot.is_initialized());
  ASSERT_TRUE(published.rootDirectory.is_initialized());
  ASSERT_TRUE(published.treeValidation.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(3)}), *published.selectedRoot);
  EXPECT_EQ(cryfs::formatv2::ObjectTreeValidationStatus::Valid, published.treeValidation->status);
  ASSERT_EQ(1u, published.rootDirectory->entries.size());
  EXPECT_EQ("file", published.rootDirectory->entries[0].name);
  EXPECT_EQ(ObjectType::File, published.rootDirectory->entries[0].type);
  EXPECT_EQ(objectId(4), published.rootDirectory->entries[0].objectId);
  EXPECT_EQ(1u, published.rootDirectory->entries[0].generation);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  ASSERT_TRUE(reopened.rootDirectory.is_initialized());
  EXPECT_EQ(*published.selectedRoot, *reopened.selectedRoot);
  ASSERT_EQ(1u, reopened.rootDirectory->entries.size());
  EXPECT_EQ("file", reopened.rootDirectory->entries[0].name);
}

TEST_F(FormatV2VolumeTest, PublishCreateRootFileChildTransactionRejectsDuplicateName) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator firstTransactionRandom;
  firstTransactionRandom.add(dataForId(rootId(3)));
  firstTransactionRandom.add(dataForId(objectId(4)));
  const auto first = publishVolumeCreateRootFileChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &firstTransactionRandom,
    "file",
    metadata(),
    metadata());

  SequenceRandomGenerator secondTransactionRandom;
  secondTransactionRandom.add(dataForId(rootId(5)));
  secondTransactionRandom.add(dataForId(objectId(6)));

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, first.status);
  EXPECT_THROW(
    publishVolumeCreateRootFileChildTransaction(
      layout(),
      filesystemId(),
      masterKey(),
      &secondTransactionRandom,
      "file",
      metadata(),
      metadata()),
    std::runtime_error);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(3)}), *reopened.selectedRoot);
}

TEST_F(FormatV2VolumeTest, PublishesCreateRootFileWithDataChildTransactionFromCurrentVolume) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator transactionRandom;
  transactionRandom.add(dataForId(rootId(3)));
  transactionRandom.add(dataForId(objectId(4)));
  transactionRandom.add(dataForId(objectId(5)));
  const auto published = publishVolumeCreateRootFileWithDataChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &transactionRandom,
    "file",
    metadata(0700),
    metadata(0600),
    payload("hello"));

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, published.status);
  ASSERT_TRUE(published.selectedRoot.is_initialized());
  ASSERT_TRUE(published.rootDirectory.is_initialized());
  ASSERT_TRUE(published.treeValidation.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(3)}), *published.selectedRoot);
  EXPECT_EQ(cryfs::formatv2::ObjectTreeValidationStatus::Valid, published.treeValidation->status);
  ASSERT_EQ(1u, published.rootDirectory->entries.size());
  EXPECT_EQ("file", published.rootDirectory->entries[0].name);
  EXPECT_EQ(ObjectType::File, published.rootDirectory->entries[0].type);
  EXPECT_EQ(objectId(4), published.rootDirectory->entries[0].objectId);
  EXPECT_EQ(1u, published.rootDirectory->entries[0].generation);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  ASSERT_TRUE(reopened.rootDirectory.is_initialized());
  EXPECT_EQ(*published.selectedRoot, *reopened.selectedRoot);
  ASSERT_EQ(1u, reopened.rootDirectory->entries.size());
  EXPECT_EQ("file", reopened.rootDirectory->entries[0].name);
}

TEST_F(FormatV2VolumeTest, PublishCreateRootFileWithDataChildTransactionRejectsDuplicateName) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator firstTransactionRandom;
  firstTransactionRandom.add(dataForId(rootId(3)));
  firstTransactionRandom.add(dataForId(objectId(4)));
  firstTransactionRandom.add(dataForId(objectId(5)));
  const auto first = publishVolumeCreateRootFileWithDataChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &firstTransactionRandom,
    "file",
    metadata(),
    metadata(),
    payload("hello"));

  SequenceRandomGenerator secondTransactionRandom;
  secondTransactionRandom.add(dataForId(rootId(6)));
  secondTransactionRandom.add(dataForId(objectId(7)));
  secondTransactionRandom.add(dataForId(objectId(8)));

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, first.status);
  EXPECT_THROW(
    publishVolumeCreateRootFileWithDataChildTransaction(
      layout(),
      filesystemId(),
      masterKey(),
      &secondTransactionRandom,
      "file",
      metadata(),
      metadata(),
      payload("other")),
    std::runtime_error);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(3)}), *reopened.selectedRoot);
}

TEST_F(FormatV2VolumeTest, PublishesCreateRootSymlinkChildTransactionFromCurrentVolume) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator transactionRandom;
  transactionRandom.add(dataForId(rootId(3)));
  transactionRandom.add(dataForId(objectId(4)));
  const auto published = publishVolumeCreateRootSymlinkChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &transactionRandom,
    "link",
    metadata(0700),
    metadata(0777),
    "../target");

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, published.status);
  ASSERT_TRUE(published.selectedRoot.is_initialized());
  ASSERT_TRUE(published.rootDirectory.is_initialized());
  ASSERT_TRUE(published.treeValidation.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(3)}), *published.selectedRoot);
  EXPECT_EQ(cryfs::formatv2::ObjectTreeValidationStatus::Valid, published.treeValidation->status);
  ASSERT_EQ(1u, published.rootDirectory->entries.size());
  EXPECT_EQ("link", published.rootDirectory->entries[0].name);
  EXPECT_EQ(ObjectType::Symlink, published.rootDirectory->entries[0].type);
  EXPECT_EQ(objectId(4), published.rootDirectory->entries[0].objectId);
  EXPECT_EQ(1u, published.rootDirectory->entries[0].generation);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  ASSERT_TRUE(reopened.rootDirectory.is_initialized());
  EXPECT_EQ(*published.selectedRoot, *reopened.selectedRoot);
  ASSERT_EQ(1u, reopened.rootDirectory->entries.size());
  EXPECT_EQ("link", reopened.rootDirectory->entries[0].name);
}

TEST_F(FormatV2VolumeTest, PublishCreateRootSymlinkChildTransactionRejectsDuplicateName) {
  SequenceRandomGenerator initialRandom;
  initialRandom.add(dataForId(rootId(1)));
  initialRandom.add(dataForId(objectId(2)));
  const auto initial = createOrOpenInitialEmptyVolume(
    layout(),
    filesystemId(),
    masterKey(),
    &initialRandom,
    metadata());

  SequenceRandomGenerator firstTransactionRandom;
  firstTransactionRandom.add(dataForId(rootId(3)));
  firstTransactionRandom.add(dataForId(objectId(4)));
  const auto first = publishVolumeCreateRootSymlinkChildTransaction(
    layout(),
    filesystemId(),
    masterKey(),
    &firstTransactionRandom,
    "link",
    metadata(),
    metadata(),
    "../target");

  SequenceRandomGenerator secondTransactionRandom;
  secondTransactionRandom.add(dataForId(rootId(5)));
  secondTransactionRandom.add(dataForId(objectId(6)));

  ASSERT_EQ(RootOpenStatus::Selected, initial.status);
  ASSERT_EQ(RootOpenStatus::Selected, first.status);
  EXPECT_THROW(
    publishVolumeCreateRootSymlinkChildTransaction(
      layout(),
      filesystemId(),
      masterKey(),
      &secondTransactionRandom,
      "link",
      metadata(),
      metadata(),
      "../other-target"),
    std::runtime_error);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(3)}), *reopened.selectedRoot);
}
