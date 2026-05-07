#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/VolumePathOperations.h>

#include <boost/filesystem.hpp>
#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

using cryfs::formatv2::AuthenticatedRoot;
using cryfs::formatv2::DirectoryMetadata;
using cryfs::formatv2::DirectoryRecordStore;
using cryfs::formatv2::DirectoryEntry;
using cryfs::formatv2::FileDataRecord;
using cryfs::formatv2::FileExtent;
using cryfs::formatv2::FileRecord;
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
using cryfs::formatv2::VolumePath;
using cryfs::formatv2::createOrOpenInitialEmptyVolume;
using cryfs::formatv2::deriveObjectEncryptionKey;
using cryfs::formatv2::openVolumeRoot;
using cryfs::formatv2::publishVolumeCreateDirectoryAtPath;
using cryfs::formatv2::publishVolumeCreateFileAtPath;
using cryfs::formatv2::publishVolumeCreateFileWithDataAtPath;
using cryfs::formatv2::publishVolumeCreateSymlinkAtPath;
using cryfs::formatv2::publishVolumeRootDirectoryTransaction;
using cryfs::formatv2::publishVolumeMoveNodeAtPath;
using cryfs::formatv2::publishVolumeMoveNodeAtPathReplacingTarget;
using cryfs::formatv2::publishVolumeRemoveEmptyDirectoryAtPath;
using cryfs::formatv2::publishVolumeRemoveFileAtPath;
using cryfs::formatv2::publishVolumeRemoveSymlinkAtPath;
using cryfs::formatv2::publishVolumeTruncateFileAtPath;
using cryfs::formatv2::publishVolumeUpdateDirectoryMetadataAtPath;
using cryfs::formatv2::publishVolumeUpdateFileMetadataAtPath;
using cryfs::formatv2::publishVolumeUpdateSymlinkMetadataAtPath;
using cryfs::formatv2::publishVolumeWriteFileAtPath;
using cryfs::formatv2::loadVolumeNodeAtPath;
using cryfs::formatv2::loadVolumeFileContentsAtPath;
using cryfs::formatv2::loadVolumeFileRangeAtPath;
using cryfs::formatv2::splitAbsoluteVolumePath;
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

std::string payloadString(const cpputils::Data &data) {
  return std::string(static_cast<const char*>(data.data()), data.size());
}

class SequenceRandomGenerator final : public cpputils::RandomGenerator {
public:
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
  size_t _next = 0;
};

class VolumePathOperationsTest : public ::testing::Test {
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

  void createInitialVolume() const {
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
  }

  void publishSparseFileAtRoot() const {
    FileRecordStore store = fileRecordStore();
    store.storeData(FileDataRecord{filesystemId(), objectId(5), 1, payload("ab")});
    store.storeData(FileDataRecord{filesystemId(), objectId(6), 1, payload("cde")});
    store.store(FileRecord{
      filesystemId(),
      objectId(4),
      1,
      metadata(0600),
      8,
      {
        FileExtent{0, 2, objectId(5), 1},
        FileExtent{5, 3, objectId(6), 1}
      }
    });

    SequenceRandomGenerator random;
    random.add(dataForId(rootId(3)));
    ASSERT_EQ(
      RootOpenStatus::Selected,
      publishVolumeRootDirectoryTransaction(
        layout(),
        filesystemId(),
        masterKey(),
        &random,
        metadata(),
        {
          DirectoryEntry{"sparse", ObjectType::File, objectId(4), 1}
        }).status);
  }

  cpputils::TempDir _baseDir;
  cpputils::TempDir _localStateDir;
};

}

TEST_F(VolumePathOperationsTest, SplitAbsoluteVolumePathAcceptsRootChildPath) {
  const VolumePath path = splitAbsoluteVolumePath("/child");

  EXPECT_TRUE(path.parentDirectoryPath.empty());
  EXPECT_EQ("child", path.childName);
}

TEST_F(VolumePathOperationsTest, SplitAbsoluteVolumePathAcceptsNestedPath) {
  const VolumePath path = splitAbsoluteVolumePath("/parent/child");

  ASSERT_EQ(1u, path.parentDirectoryPath.size());
  EXPECT_EQ("parent", path.parentDirectoryPath[0]);
  EXPECT_EQ("child", path.childName);
}

TEST_F(VolumePathOperationsTest, SplitAbsoluteVolumePathRejectsInvalidPaths) {
  EXPECT_THROW(splitAbsoluteVolumePath("relative"), std::runtime_error);
  EXPECT_THROW(splitAbsoluteVolumePath("/"), std::runtime_error);
  EXPECT_THROW(splitAbsoluteVolumePath(bf::path("/parent") / "." / "child"), std::runtime_error);
  EXPECT_THROW(splitAbsoluteVolumePath(bf::path("/parent") / ".." / "child"), std::runtime_error);
}

TEST_F(VolumePathOperationsTest, LoadVolumeNodeAtPathLoadsRootDirectory) {
  createInitialVolume();

  const auto node = loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/");

  ASSERT_TRUE(node.is_initialized());
  EXPECT_EQ(ObjectType::Directory, node->type);
  EXPECT_EQ(objectId(2), node->objectId);
  EXPECT_EQ(1u, node->generation);
  ASSERT_TRUE(node->directory.is_initialized());
  EXPECT_TRUE(node->directory->entries.empty());
  EXPECT_FALSE(node->file.is_initialized());
  EXPECT_FALSE(node->symlink.is_initialized());
}

TEST_F(VolumePathOperationsTest, LoadVolumeNodeAtPathLoadsDirectoryFileAndSymlink) {
  createInitialVolume();

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &parentRandom,
      "/parent",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator fileRandom;
  fileRandom.add(dataForId(rootId(5)));
  fileRandom.add(dataForId(objectId(6)));
  fileRandom.add(dataForId(objectId(7)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileWithDataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &fileRandom,
      "/parent/file",
      metadata(),
      metadata(0600),
      payload("hello")).status);

  SequenceRandomGenerator symlinkRandom;
  symlinkRandom.add(dataForId(rootId(8)));
  symlinkRandom.add(dataForId(objectId(9)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateSymlinkAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &symlinkRandom,
      "/parent/link",
      metadata(),
      metadata(0777),
      "../target").status);

  const auto directoryNode = loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/parent");
  ASSERT_TRUE(directoryNode.is_initialized());
  EXPECT_EQ(ObjectType::Directory, directoryNode->type);
  EXPECT_EQ(objectId(4), directoryNode->objectId);
  EXPECT_EQ(3u, directoryNode->generation);
  ASSERT_TRUE(directoryNode->directory.is_initialized());
  ASSERT_EQ(2u, directoryNode->directory->entries.size());

  const auto fileNode = loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/parent/file");
  ASSERT_TRUE(fileNode.is_initialized());
  EXPECT_EQ(ObjectType::File, fileNode->type);
  EXPECT_EQ(objectId(6), fileNode->objectId);
  EXPECT_EQ(1u, fileNode->generation);
  ASSERT_TRUE(fileNode->file.is_initialized());
  EXPECT_EQ(5u, fileNode->file->size);
  ASSERT_EQ(1u, fileNode->file->extents.size());
  EXPECT_EQ(objectId(7), fileNode->file->extents[0].dataId);
  EXPECT_FALSE(fileNode->directory.is_initialized());
  EXPECT_FALSE(fileNode->symlink.is_initialized());

  const auto symlinkNode = loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/parent/link");
  ASSERT_TRUE(symlinkNode.is_initialized());
  EXPECT_EQ(ObjectType::Symlink, symlinkNode->type);
  EXPECT_EQ(objectId(9), symlinkNode->objectId);
  EXPECT_EQ(1u, symlinkNode->generation);
  ASSERT_TRUE(symlinkNode->symlink.is_initialized());
  EXPECT_EQ("../target", symlinkNode->symlink->target);
  EXPECT_FALSE(symlinkNode->directory.is_initialized());
  EXPECT_FALSE(symlinkNode->file.is_initialized());
}

TEST_F(VolumePathOperationsTest, LoadVolumeNodeAtPathReturnsNoneForMissingPath) {
  createInitialVolume();

  EXPECT_FALSE(loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/missing").is_initialized());
  EXPECT_FALSE(loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/missing/child").is_initialized());
}

TEST_F(VolumePathOperationsTest, LoadVolumeNodeAtPathRejectsNonDirectoryTraversal) {
  createInitialVolume();

  SequenceRandomGenerator random;
  random.add(dataForId(rootId(3)));
  random.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &random,
      "/file",
      metadata(),
      metadata(0600)).status);

  EXPECT_THROW(
    loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/file/child"),
    std::runtime_error);
}

TEST_F(VolumePathOperationsTest, LoadVolumeNodeAtPathRejectsMissingSelectedRoot) {
  EXPECT_THROW(
    loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/"),
    std::runtime_error);
}

TEST_F(VolumePathOperationsTest, LoadVolumeFileContentsAtPathLoadsRootAndNestedFiles) {
  createInitialVolume();

  SequenceRandomGenerator rootFileRandom;
  rootFileRandom.add(dataForId(rootId(3)));
  rootFileRandom.add(dataForId(objectId(4)));
  rootFileRandom.add(dataForId(objectId(5)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileWithDataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &rootFileRandom,
      "/root-file",
      metadata(),
      metadata(0600),
      payload("root")).status);

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(6)));
  parentRandom.add(dataForId(objectId(7)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &parentRandom,
      "/parent",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator nestedFileRandom;
  nestedFileRandom.add(dataForId(rootId(8)));
  nestedFileRandom.add(dataForId(objectId(9)));
  nestedFileRandom.add(dataForId(objectId(10)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileWithDataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &nestedFileRandom,
      "/parent/nested-file",
      metadata(),
      metadata(0600),
      payload("nested")).status);

  const auto rootFile = loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/root-file");
  ASSERT_TRUE(rootFile.is_initialized());
  EXPECT_EQ("root", payloadString(*rootFile));

  const auto nestedFile = loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/parent/nested-file");
  ASSERT_TRUE(nestedFile.is_initialized());
  EXPECT_EQ("nested", payloadString(*nestedFile));
}

TEST_F(VolumePathOperationsTest, LoadVolumeFileContentsAtPathLoadsEmptyFile) {
  createInitialVolume();

  SequenceRandomGenerator random;
  random.add(dataForId(rootId(3)));
  random.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &random,
      "/empty",
      metadata(),
      metadata(0600)).status);

  const auto contents = loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/empty");

  ASSERT_TRUE(contents.is_initialized());
  EXPECT_EQ(0u, contents->size());
}

TEST_F(VolumePathOperationsTest, LoadVolumeFileContentsAtPathLoadsSparseFile) {
  createInitialVolume();
  publishSparseFileAtRoot();

  const auto contents = loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/sparse");

  ASSERT_TRUE(contents.is_initialized());
  EXPECT_EQ(std::string("ab\0\0\0cde", 8), payloadString(*contents));
}

TEST_F(VolumePathOperationsTest, LoadVolumeFileRangeAtPathReadsSparseRangesAndClampsAtEof) {
  createInitialVolume();
  publishSparseFileAtRoot();

  const auto middle = loadVolumeFileRangeAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/sparse",
    1,
    6);
  ASSERT_TRUE(middle.is_initialized());
  EXPECT_EQ(std::string("b\0\0\0cd", 6), payloadString(*middle));

  const auto tail = loadVolumeFileRangeAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/sparse",
    7,
    20);
  ASSERT_TRUE(tail.is_initialized());
  EXPECT_EQ("e", payloadString(*tail));

  const auto pastEnd = loadVolumeFileRangeAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/sparse",
    8,
    20);
  ASSERT_TRUE(pastEnd.is_initialized());
  EXPECT_EQ(0u, pastEnd->size());

  const auto zeroLength = loadVolumeFileRangeAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/sparse",
    1,
    0);
  ASSERT_TRUE(zeroLength.is_initialized());
  EXPECT_EQ(0u, zeroLength->size());
}

TEST_F(VolumePathOperationsTest, LoadVolumeFileContentsAtPathReturnsNoneForMissingPath) {
  createInitialVolume();

  EXPECT_FALSE(loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/missing").is_initialized());
}

TEST_F(VolumePathOperationsTest, LoadVolumeFileContentsAtPathRejectsNonFilePath) {
  createInitialVolume();

  SequenceRandomGenerator directoryRandom;
  directoryRandom.add(dataForId(rootId(3)));
  directoryRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &directoryRandom,
      "/directory",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator symlinkRandom;
  symlinkRandom.add(dataForId(rootId(5)));
  symlinkRandom.add(dataForId(objectId(6)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateSymlinkAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &symlinkRandom,
      "/link",
      metadata(),
      metadata(0777),
      "target").status);

  EXPECT_THROW(
    loadVolumeFileContentsAtPath(layout(), filesystemId(), masterKey(), "/"),
    std::runtime_error);
  EXPECT_THROW(
    loadVolumeFileContentsAtPath(layout(), filesystemId(), masterKey(), "/directory"),
    std::runtime_error);
  EXPECT_THROW(
    loadVolumeFileContentsAtPath(layout(), filesystemId(), masterKey(), "/link"),
    std::runtime_error);
}

TEST_F(VolumePathOperationsTest, PublishVolumeWriteFileAtPathOverwritesRootFileWithNewGeneration) {
  createInitialVolume();

  SequenceRandomGenerator createRandom;
  createRandom.add(dataForId(rootId(3)));
  createRandom.add(dataForId(objectId(4)));
  createRandom.add(dataForId(objectId(5)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileWithDataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &createRandom,
      "/file",
      metadata(),
      metadata(0600),
      payload("abcdef")).status);

  SequenceRandomGenerator writeRandom;
  writeRandom.add(dataForId(objectId(6)));
  writeRandom.add(dataForId(objectId(7)));
  writeRandom.add(dataForId(objectId(8)));
  writeRandom.add(dataForId(rootId(9)));
  const auto result = publishVolumeWriteFileAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &writeRandom,
    "/file",
    metadata(0601),
    2,
    payload("XY"));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(9)}), *result->selectedRoot);
  ASSERT_TRUE(result->rootDirectory.is_initialized());
  ASSERT_EQ(1u, result->rootDirectory->entries.size());
  EXPECT_EQ("file", result->rootDirectory->entries[0].name);
  EXPECT_EQ(objectId(4), result->rootDirectory->entries[0].objectId);
  EXPECT_EQ(2u, result->rootDirectory->entries[0].generation);

  const auto file = fileRecordStore().load(objectId(4), 2);
  ASSERT_TRUE(file.is_initialized());
  EXPECT_EQ(0601u, file->metadata.permissions);
  EXPECT_EQ(6u, file->size);
  ASSERT_EQ(3u, file->extents.size());
  EXPECT_EQ((FileExtent{0, 2, objectId(6), 1}), file->extents[0]);
  EXPECT_EQ((FileExtent{2, 2, objectId(8), 1}), file->extents[1]);
  EXPECT_EQ((FileExtent{4, 2, objectId(7), 1}), file->extents[2]);
  EXPECT_TRUE(fileRecordStore().load(objectId(4), 1).is_initialized());

  const auto contents = loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/file");
  ASSERT_TRUE(contents.is_initialized());
  EXPECT_EQ("abXYef", payloadString(*contents));
}

TEST_F(VolumePathOperationsTest, PublishVolumeWriteFileAtPathExtendsSparseRootFile) {
  createInitialVolume();

  SequenceRandomGenerator createRandom;
  createRandom.add(dataForId(rootId(3)));
  createRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &createRandom,
      "/file",
      metadata(),
      metadata(0600)).status);

  SequenceRandomGenerator writeRandom;
  writeRandom.add(dataForId(objectId(5)));
  writeRandom.add(dataForId(rootId(6)));
  const auto result = publishVolumeWriteFileAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &writeRandom,
    "/file",
    metadata(0600),
    4,
    payload("tail"));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  const auto file = fileRecordStore().load(objectId(4), 2);
  ASSERT_TRUE(file.is_initialized());
  EXPECT_EQ(8u, file->size);
  ASSERT_EQ(1u, file->extents.size());
  EXPECT_EQ((FileExtent{4, 4, objectId(5), 1}), file->extents[0]);

  const auto contents = loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/file");
  ASSERT_TRUE(contents.is_initialized());
  EXPECT_EQ(std::string("\0\0\0\0tail", 8), payloadString(*contents));
}

TEST_F(VolumePathOperationsTest, PublishVolumeWriteFileAtPathUpdatesNestedFileReference) {
  createInitialVolume();

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &parentRandom,
      "/parent",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator createRandom;
  createRandom.add(dataForId(rootId(5)));
  createRandom.add(dataForId(objectId(6)));
  createRandom.add(dataForId(objectId(7)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileWithDataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &createRandom,
      "/parent/file",
      metadata(),
      metadata(0600),
      payload("hello")).status);

  SequenceRandomGenerator writeRandom;
  writeRandom.add(dataForId(objectId(8)));
  writeRandom.add(dataForId(rootId(9)));
  const auto result = publishVolumeWriteFileAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &writeRandom,
    "/parent/file",
    metadata(0601),
    5,
    payload("!"));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{4, rootId(9)}), *result->selectedRoot);
  ASSERT_TRUE(result->rootDirectory.is_initialized());
  ASSERT_EQ(1u, result->rootDirectory->entries.size());
  EXPECT_EQ(objectId(4), result->rootDirectory->entries[0].objectId);
  EXPECT_EQ(3u, result->rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore().load(objectId(4), 3);
  ASSERT_TRUE(parent.is_initialized());
  ASSERT_EQ(1u, parent->entries.size());
  EXPECT_EQ("file", parent->entries[0].name);
  EXPECT_EQ(objectId(6), parent->entries[0].objectId);
  EXPECT_EQ(2u, parent->entries[0].generation);

  const auto contents = loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/parent/file");
  ASSERT_TRUE(contents.is_initialized());
  EXPECT_EQ("hello!", payloadString(*contents));
}

TEST_F(VolumePathOperationsTest, PublishVolumeTruncateFileAtPathShrinksAndExtendsFile) {
  createInitialVolume();

  SequenceRandomGenerator createRandom;
  createRandom.add(dataForId(rootId(3)));
  createRandom.add(dataForId(objectId(4)));
  createRandom.add(dataForId(objectId(5)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileWithDataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &createRandom,
      "/file",
      metadata(),
      metadata(0600),
      payload("abcdef")).status);

  SequenceRandomGenerator shrinkRandom;
  shrinkRandom.add(dataForId(objectId(6)));
  shrinkRandom.add(dataForId(rootId(7)));
  const auto shrink = publishVolumeTruncateFileAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &shrinkRandom,
    "/file",
    metadata(0601),
    3);

  ASSERT_TRUE(shrink.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, shrink->status);
  auto file = fileRecordStore().load(objectId(4), 2);
  ASSERT_TRUE(file.is_initialized());
  EXPECT_EQ(3u, file->size);
  EXPECT_EQ(0601u, file->metadata.permissions);
  ASSERT_EQ(1u, file->extents.size());
  EXPECT_EQ((FileExtent{0, 3, objectId(6), 1}), file->extents[0]);

  SequenceRandomGenerator extendRandom;
  extendRandom.add(dataForId(rootId(8)));
  const auto extend = publishVolumeTruncateFileAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &extendRandom,
    "/file",
    metadata(0602),
    6);

  ASSERT_TRUE(extend.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, extend->status);
  file = fileRecordStore().load(objectId(4), 3);
  ASSERT_TRUE(file.is_initialized());
  EXPECT_EQ(6u, file->size);
  EXPECT_EQ(0602u, file->metadata.permissions);
  ASSERT_EQ(1u, file->extents.size());
  EXPECT_EQ((FileExtent{0, 3, objectId(6), 1}), file->extents[0]);

  const auto contents = loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/file");
  ASSERT_TRUE(contents.is_initialized());
  EXPECT_EQ(std::string("abc\0\0\0", 6), payloadString(*contents));
}

TEST_F(VolumePathOperationsTest, PublishVolumeFileMutationAtPathRejectsMissingAndNonFilePaths) {
  createInitialVolume();

  SequenceRandomGenerator missingRandom;
  EXPECT_FALSE(publishVolumeWriteFileAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &missingRandom,
    "/missing",
    metadata(),
    0,
    payload("data")).is_initialized());
  EXPECT_FALSE(publishVolumeTruncateFileAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &missingRandom,
    "/missing",
    metadata(),
    0).is_initialized());

  SequenceRandomGenerator directoryRandom;
  directoryRandom.add(dataForId(rootId(3)));
  directoryRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &directoryRandom,
      "/directory",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator noRandomUse;
  EXPECT_THROW(
    publishVolumeWriteFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &noRandomUse,
      "/directory",
      metadata(),
      0,
      payload("data")),
    std::runtime_error);
  EXPECT_THROW(
    publishVolumeTruncateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &noRandomUse,
      "/directory",
      metadata(),
      0),
    std::runtime_error);
}

TEST_F(VolumePathOperationsTest, PublishVolumeUpdateFileMetadataAtPathUpdatesRootFileWithoutChangingContents) {
  createInitialVolume();

  SequenceRandomGenerator createRandom;
  createRandom.add(dataForId(rootId(3)));
  createRandom.add(dataForId(objectId(4)));
  createRandom.add(dataForId(objectId(5)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileWithDataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &createRandom,
      "/file",
      metadata(),
      metadata(0600),
      payload("hello")).status);

  SequenceRandomGenerator updateRandom;
  updateRandom.add(dataForId(rootId(6)));
  const auto result = publishVolumeUpdateFileMetadataAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &updateRandom,
    "/file",
    metadata(0601));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(6)}), *result->selectedRoot);
  ASSERT_TRUE(result->rootDirectory.is_initialized());
  ASSERT_EQ(1u, result->rootDirectory->entries.size());
  EXPECT_EQ(objectId(4), result->rootDirectory->entries[0].objectId);
  EXPECT_EQ(2u, result->rootDirectory->entries[0].generation);

  const auto file = fileRecordStore().load(objectId(4), 2);
  ASSERT_TRUE(file.is_initialized());
  EXPECT_EQ(0601u, file->metadata.permissions);
  EXPECT_EQ(5u, file->size);
  ASSERT_EQ(1u, file->extents.size());
  EXPECT_EQ((FileExtent{0, 5, objectId(5), 1}), file->extents[0]);
  EXPECT_TRUE(fileRecordStore().load(objectId(4), 1).is_initialized());

  const auto contents = loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/file");
  ASSERT_TRUE(contents.is_initialized());
  EXPECT_EQ("hello", payloadString(*contents));
}

TEST_F(VolumePathOperationsTest, PublishVolumeUpdateSymlinkMetadataAtPathUpdatesNestedSymlink) {
  createInitialVolume();

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &parentRandom,
      "/parent",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator symlinkRandom;
  symlinkRandom.add(dataForId(rootId(5)));
  symlinkRandom.add(dataForId(objectId(6)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateSymlinkAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &symlinkRandom,
      "/parent/link",
      metadata(),
      metadata(0777),
      "../target").status);

  SequenceRandomGenerator updateRandom;
  updateRandom.add(dataForId(rootId(7)));
  const auto result = publishVolumeUpdateSymlinkMetadataAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &updateRandom,
    "/parent/link",
    metadata(0701));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{4, rootId(7)}), *result->selectedRoot);
  ASSERT_TRUE(result->rootDirectory.is_initialized());
  ASSERT_EQ(1u, result->rootDirectory->entries.size());
  EXPECT_EQ(objectId(4), result->rootDirectory->entries[0].objectId);
  EXPECT_EQ(3u, result->rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore().load(objectId(4), 3);
  ASSERT_TRUE(parent.is_initialized());
  ASSERT_EQ(1u, parent->entries.size());
  EXPECT_EQ("link", parent->entries[0].name);
  EXPECT_EQ(ObjectType::Symlink, parent->entries[0].type);
  EXPECT_EQ(objectId(6), parent->entries[0].objectId);
  EXPECT_EQ(2u, parent->entries[0].generation);

  const auto symlink = symlinkRecordStore().load(objectId(6), 2);
  ASSERT_TRUE(symlink.is_initialized());
  EXPECT_EQ(0701u, symlink->metadata.permissions);
  EXPECT_EQ("../target", symlink->target);
  EXPECT_TRUE(symlinkRecordStore().load(objectId(6), 1).is_initialized());
}

TEST_F(VolumePathOperationsTest, PublishVolumeUpdateDirectoryMetadataAtPathUpdatesRootDirectory) {
  createInitialVolume();

  SequenceRandomGenerator updateRandom;
  updateRandom.add(dataForId(rootId(3)));
  const auto result = publishVolumeUpdateDirectoryMetadataAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &updateRandom,
    "/",
    metadata(0701));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(3)}), *result->selectedRoot);
  ASSERT_TRUE(result->rootDirectory.is_initialized());
  EXPECT_EQ(0701u, result->rootDirectory->metadata.permissions);
  EXPECT_TRUE(result->rootDirectory->entries.empty());
}

TEST_F(VolumePathOperationsTest, PublishVolumeUpdateDirectoryMetadataAtPathUpdatesNestedDirectoryAndAncestors) {
  createInitialVolume();

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &parentRandom,
      "/parent",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator childRandom;
  childRandom.add(dataForId(rootId(5)));
  childRandom.add(dataForId(objectId(6)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &childRandom,
      "/parent/child",
      metadata(),
      metadata(0700)).status);

  SequenceRandomGenerator updateRandom;
  updateRandom.add(dataForId(rootId(7)));
  const auto result = publishVolumeUpdateDirectoryMetadataAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &updateRandom,
    "/parent/child",
    metadata(0712));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->rootDirectory.is_initialized());
  ASSERT_EQ(1u, result->rootDirectory->entries.size());
  EXPECT_EQ(objectId(4), result->rootDirectory->entries[0].objectId);
  EXPECT_EQ(3u, result->rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore().load(objectId(4), 3);
  ASSERT_TRUE(parent.is_initialized());
  ASSERT_EQ(1u, parent->entries.size());
  EXPECT_EQ("child", parent->entries[0].name);
  EXPECT_EQ(objectId(6), parent->entries[0].objectId);
  EXPECT_EQ(2u, parent->entries[0].generation);

  const auto child = directoryRecordStore().load(objectId(6), 2);
  ASSERT_TRUE(child.is_initialized());
  EXPECT_EQ(0712u, child->metadata.permissions);
  EXPECT_TRUE(directoryRecordStore().load(objectId(6), 1).is_initialized());
}

TEST_F(VolumePathOperationsTest, PublishVolumeMetadataUpdateAtPathReturnsNoneForMissingPathAndRejectsWrongType) {
  createInitialVolume();

  SequenceRandomGenerator missingRandom;
  EXPECT_FALSE(publishVolumeUpdateFileMetadataAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &missingRandom,
    "/missing",
    metadata()).is_initialized());
  EXPECT_FALSE(publishVolumeUpdateSymlinkMetadataAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &missingRandom,
    "/missing",
    metadata()).is_initialized());
  EXPECT_FALSE(publishVolumeUpdateDirectoryMetadataAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &missingRandom,
    "/missing",
    metadata()).is_initialized());

  SequenceRandomGenerator fileRandom;
  fileRandom.add(dataForId(rootId(3)));
  fileRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &fileRandom,
      "/file",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator noRandomUse;
  EXPECT_THROW(
    publishVolumeUpdateDirectoryMetadataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &noRandomUse,
      "/file",
      metadata()),
    std::runtime_error);
  EXPECT_THROW(
    publishVolumeUpdateSymlinkMetadataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &noRandomUse,
      "/file",
      metadata()),
    std::runtime_error);
  EXPECT_THROW(
    publishVolumeUpdateFileMetadataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &noRandomUse,
      "/",
      metadata()),
    std::runtime_error);
}

TEST_F(VolumePathOperationsTest, PublishVolumeRemoveFileAtPathRemovesRootFile) {
  createInitialVolume();

  SequenceRandomGenerator createRandom;
  createRandom.add(dataForId(rootId(3)));
  createRandom.add(dataForId(objectId(4)));
  createRandom.add(dataForId(objectId(5)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileWithDataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &createRandom,
      "/file",
      metadata(),
      metadata(0600),
      payload("hello")).status);

  SequenceRandomGenerator removeRandom;
  removeRandom.add(dataForId(rootId(6)));
  const auto result = publishVolumeRemoveFileAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &removeRandom,
    "/file",
    metadata(0701));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(6)}), *result->selectedRoot);
  ASSERT_TRUE(result->rootDirectory.is_initialized());
  EXPECT_EQ(0701u, result->rootDirectory->metadata.permissions);
  EXPECT_TRUE(result->rootDirectory->entries.empty());
  EXPECT_FALSE(loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/file").is_initialized());
  EXPECT_TRUE(fileRecordStore().load(objectId(4), 1).is_initialized());
  EXPECT_TRUE(fileRecordStore().loadData(objectId(5), 1).is_initialized());
}

TEST_F(VolumePathOperationsTest, PublishVolumeRemoveSymlinkAtPathRemovesNestedSymlink) {
  createInitialVolume();

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &parentRandom,
      "/parent",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator symlinkRandom;
  symlinkRandom.add(dataForId(rootId(5)));
  symlinkRandom.add(dataForId(objectId(6)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateSymlinkAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &symlinkRandom,
      "/parent/link",
      metadata(),
      metadata(0777),
      "target").status);

  SequenceRandomGenerator removeRandom;
  removeRandom.add(dataForId(rootId(7)));
  const auto result = publishVolumeRemoveSymlinkAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &removeRandom,
    "/parent/link",
    metadata(0701));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{4, rootId(7)}), *result->selectedRoot);
  ASSERT_TRUE(result->rootDirectory.is_initialized());
  ASSERT_EQ(1u, result->rootDirectory->entries.size());
  EXPECT_EQ(objectId(4), result->rootDirectory->entries[0].objectId);
  EXPECT_EQ(3u, result->rootDirectory->entries[0].generation);

  const auto parent = directoryRecordStore().load(objectId(4), 3);
  ASSERT_TRUE(parent.is_initialized());
  EXPECT_EQ(0701u, parent->metadata.permissions);
  EXPECT_TRUE(parent->entries.empty());
  EXPECT_FALSE(loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/parent/link").is_initialized());
  EXPECT_TRUE(symlinkRecordStore().load(objectId(6), 1).is_initialized());
}

TEST_F(VolumePathOperationsTest, PublishVolumeRemoveEmptyDirectoryAtPathRemovesNestedDirectory) {
  createInitialVolume();

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &parentRandom,
      "/parent",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator childRandom;
  childRandom.add(dataForId(rootId(5)));
  childRandom.add(dataForId(objectId(6)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &childRandom,
      "/parent/empty",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator removeRandom;
  removeRandom.add(dataForId(rootId(7)));
  const auto result = publishVolumeRemoveEmptyDirectoryAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &removeRandom,
    "/parent/empty",
    metadata(0701));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  const auto parent = directoryRecordStore().load(objectId(4), 3);
  ASSERT_TRUE(parent.is_initialized());
  EXPECT_TRUE(parent->entries.empty());
  EXPECT_FALSE(loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/parent/empty").is_initialized());
  EXPECT_TRUE(directoryRecordStore().load(objectId(6), 1).is_initialized());
}

TEST_F(VolumePathOperationsTest, PublishVolumeRemoveEmptyDirectoryAtPathRejectsNonEmptyDirectory) {
  createInitialVolume();

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &parentRandom,
      "/parent",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator childRandom;
  childRandom.add(dataForId(rootId(5)));
  childRandom.add(dataForId(objectId(6)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &childRandom,
      "/parent/child",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator removeRandom;
  EXPECT_THROW(
    publishVolumeRemoveEmptyDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &removeRandom,
      "/parent",
      metadata()),
    std::runtime_error);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(5)}), *reopened.selectedRoot);
}

TEST_F(VolumePathOperationsTest, PublishVolumeRemoveAtPathReturnsNoneForMissingPathAndRejectsWrongType) {
  createInitialVolume();

  SequenceRandomGenerator missingRandom;
  EXPECT_FALSE(publishVolumeRemoveFileAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &missingRandom,
    "/missing",
    metadata()).is_initialized());

  SequenceRandomGenerator fileRandom;
  fileRandom.add(dataForId(rootId(3)));
  fileRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &fileRandom,
      "/file",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator noRandomUse;
  EXPECT_THROW(
    publishVolumeRemoveEmptyDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &noRandomUse,
      "/file",
      metadata()),
    std::runtime_error);
  EXPECT_THROW(
    publishVolumeRemoveSymlinkAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &noRandomUse,
      "/file",
      metadata()),
    std::runtime_error);
}

TEST_F(VolumePathOperationsTest, PublishesRootAndNestedDirectoriesAtPath) {
  createInitialVolume();

  SequenceRandomGenerator rootChildRandom;
  rootChildRandom.add(dataForId(rootId(3)));
  rootChildRandom.add(dataForId(objectId(4)));
  const auto parent = publishVolumeCreateDirectoryAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &rootChildRandom,
    "/parent",
    metadata(0700),
    metadata(0711));

  SequenceRandomGenerator nestedChildRandom;
  nestedChildRandom.add(dataForId(rootId(5)));
  nestedChildRandom.add(dataForId(objectId(6)));
  const auto child = publishVolumeCreateDirectoryAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &nestedChildRandom,
    "/parent/child",
    metadata(0701),
    metadata(0712));

  ASSERT_EQ(RootOpenStatus::Selected, parent.status);
  ASSERT_EQ(RootOpenStatus::Selected, child.status);
  ASSERT_TRUE(child.selectedRoot.is_initialized());
  ASSERT_TRUE(child.rootDirectory.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(5)}), *child.selectedRoot);
  ASSERT_EQ(1u, child.rootDirectory->entries.size());
  EXPECT_EQ("parent", child.rootDirectory->entries[0].name);
  EXPECT_EQ(objectId(4), child.rootDirectory->entries[0].objectId);
  EXPECT_EQ(2u, child.rootDirectory->entries[0].generation);

  const auto parentDirectory = directoryRecordStore().load(objectId(4), 2);
  ASSERT_TRUE(parentDirectory.is_initialized());
  ASSERT_EQ(1u, parentDirectory->entries.size());
  EXPECT_EQ("child", parentDirectory->entries[0].name);
  EXPECT_EQ(objectId(6), parentDirectory->entries[0].objectId);
}

TEST_F(VolumePathOperationsTest, PublishesRootFileWithDataAtPath) {
  createInitialVolume();

  SequenceRandomGenerator random;
  random.add(dataForId(rootId(3)));
  random.add(dataForId(objectId(4)));
  random.add(dataForId(objectId(5)));
  const auto result = publishVolumeCreateFileWithDataAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &random,
    "/file",
    metadata(0700),
    metadata(0600),
    payload("hello"));

  ASSERT_EQ(RootOpenStatus::Selected, result.status);
  ASSERT_TRUE(result.rootDirectory.is_initialized());
  ASSERT_EQ(1u, result.rootDirectory->entries.size());
  EXPECT_EQ("file", result.rootDirectory->entries[0].name);
  EXPECT_EQ(ObjectType::File, result.rootDirectory->entries[0].type);
  EXPECT_EQ(objectId(4), result.rootDirectory->entries[0].objectId);

  const auto file = fileRecordStore().load(objectId(4), 1);
  ASSERT_TRUE(file.is_initialized());
  ASSERT_EQ(1u, file->extents.size());
  EXPECT_EQ(objectId(5), file->extents[0].dataId);
  EXPECT_TRUE(fileRecordStore().loadData(objectId(5), 1).is_initialized());
}

TEST_F(VolumePathOperationsTest, PublishesNestedFileAndSymlinkAtPath) {
  createInitialVolume();

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  const auto parent = publishVolumeCreateDirectoryAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &parentRandom,
    "/parent",
    metadata(),
    metadata());
  ASSERT_EQ(RootOpenStatus::Selected, parent.status);

  SequenceRandomGenerator fileRandom;
  fileRandom.add(dataForId(rootId(5)));
  fileRandom.add(dataForId(objectId(6)));
  const auto fileResult = publishVolumeCreateFileAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &fileRandom,
    "/parent/file",
    metadata(0701),
    metadata(0600));

  SequenceRandomGenerator symlinkRandom;
  symlinkRandom.add(dataForId(rootId(7)));
  symlinkRandom.add(dataForId(objectId(8)));
  const auto symlinkResult = publishVolumeCreateSymlinkAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &symlinkRandom,
    "/parent/link",
    metadata(0702),
    metadata(0777),
    "../target");

  ASSERT_EQ(RootOpenStatus::Selected, fileResult.status);
  ASSERT_EQ(RootOpenStatus::Selected, symlinkResult.status);
  const auto parentDirectory = directoryRecordStore().load(objectId(4), 3);
  ASSERT_TRUE(parentDirectory.is_initialized());
  ASSERT_EQ(2u, parentDirectory->entries.size());
  EXPECT_EQ("file", parentDirectory->entries[0].name);
  EXPECT_EQ(ObjectType::File, parentDirectory->entries[0].type);
  EXPECT_EQ(objectId(6), parentDirectory->entries[0].objectId);
  EXPECT_EQ("link", parentDirectory->entries[1].name);
  EXPECT_EQ(ObjectType::Symlink, parentDirectory->entries[1].type);
  EXPECT_EQ(objectId(8), parentDirectory->entries[1].objectId);
  EXPECT_TRUE(fileRecordStore().load(objectId(6), 1).is_initialized());
  EXPECT_TRUE(symlinkRecordStore().load(objectId(8), 1).is_initialized());
}

TEST_F(VolumePathOperationsTest, MissingNestedParentPathDoesNotPublish) {
  createInitialVolume();

  SequenceRandomGenerator random;
  EXPECT_THROW(
    publishVolumeCreateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &random,
      "/missing/file",
      metadata(),
      metadata()),
    std::runtime_error);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{1, rootId(1)}), *reopened.selectedRoot);
}

TEST_F(VolumePathOperationsTest, PublishVolumeMoveNodeAtPathRenamesRootFile) {
  createInitialVolume();

  SequenceRandomGenerator createRandom;
  createRandom.add(dataForId(rootId(3)));
  createRandom.add(dataForId(objectId(4)));
  createRandom.add(dataForId(objectId(5)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileWithDataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &createRandom,
      "/old",
      metadata(),
      metadata(0600),
      payload("contents")).status);

  SequenceRandomGenerator moveRandom;
  moveRandom.add(dataForId(rootId(6)));
  const auto result = publishVolumeMoveNodeAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &moveRandom,
    "/old",
    "/new",
    metadata(0701),
    metadata(0702));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(6)}), *result->selectedRoot);
  ASSERT_TRUE(result->rootDirectory.is_initialized());
  EXPECT_EQ(0702u, result->rootDirectory->metadata.permissions);
  EXPECT_FALSE(loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/old").is_initialized());
  const auto moved = loadVolumeFileContentsAtPath(layout(), filesystemId(), masterKey(), "/new");
  ASSERT_TRUE(moved.is_initialized());
  EXPECT_EQ("contents", payloadString(*moved));
}

TEST_F(VolumePathOperationsTest, PublishVolumeMoveNodeAtPathMovesDirectorySubtreeToRoot) {
  createInitialVolume();

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &parentRandom,
      "/parent",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator childRandom;
  childRandom.add(dataForId(rootId(5)));
  childRandom.add(dataForId(objectId(6)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &childRandom,
      "/parent/child",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator fileRandom;
  fileRandom.add(dataForId(rootId(7)));
  fileRandom.add(dataForId(objectId(8)));
  fileRandom.add(dataForId(objectId(9)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileWithDataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &fileRandom,
      "/parent/child/file",
      metadata(),
      metadata(0600),
      payload("data")).status);

  SequenceRandomGenerator moveRandom;
  moveRandom.add(dataForId(rootId(10)));
  const auto result = publishVolumeMoveNodeAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &moveRandom,
    "/parent/child",
    "/moved",
    metadata(0701),
    metadata(0702));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{5, rootId(10)}), *result->selectedRoot);
  EXPECT_FALSE(loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/parent/child").is_initialized());
  const auto movedFile = loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/moved/file");
  ASSERT_TRUE(movedFile.is_initialized());
  EXPECT_EQ("data", payloadString(*movedFile));

  const auto parent = directoryRecordStore().load(objectId(4), 4);
  ASSERT_TRUE(parent.is_initialized());
  EXPECT_EQ(0701u, parent->metadata.permissions);
  EXPECT_TRUE(parent->entries.empty());
}

TEST_F(VolumePathOperationsTest, PublishVolumeMoveNodeAtPathReturnsNoneForMissingSource) {
  createInitialVolume();

  SequenceRandomGenerator moveRandom;
  EXPECT_FALSE(publishVolumeMoveNodeAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &moveRandom,
    "/missing",
    "/new",
    metadata(),
    metadata()).is_initialized());

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{1, rootId(1)}), *reopened.selectedRoot);
}

TEST_F(VolumePathOperationsTest, PublishVolumeMoveNodeAtPathTreatsSamePathAsNoop) {
  createInitialVolume();

  SequenceRandomGenerator createRandom;
  createRandom.add(dataForId(rootId(3)));
  createRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &createRandom,
      "/file",
      metadata(),
      metadata(0600)).status);

  SequenceRandomGenerator moveRandom;
  const auto result = publishVolumeMoveNodeAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    &moveRandom,
    "/file",
    "/file",
    metadata(0701),
    metadata(0702));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{2, rootId(3)}), *result->selectedRoot);
  EXPECT_TRUE(loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/file").is_initialized());
}

TEST_F(VolumePathOperationsTest, PublishVolumeMoveNodeAtPathRejectsExistingTarget) {
  createInitialVolume();

  SequenceRandomGenerator sourceRandom;
  sourceRandom.add(dataForId(rootId(3)));
  sourceRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &sourceRandom,
      "/source",
      metadata(),
      metadata(0600)).status);

  SequenceRandomGenerator targetRandom;
  targetRandom.add(dataForId(rootId(5)));
  targetRandom.add(dataForId(objectId(6)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &targetRandom,
      "/target",
      metadata(),
      metadata(0600)).status);

  SequenceRandomGenerator moveRandom;
  EXPECT_THROW(
    publishVolumeMoveNodeAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &moveRandom,
      "/source",
      "/target",
      metadata(),
      metadata()),
    std::runtime_error);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(5)}), *reopened.selectedRoot);
}

TEST_F(VolumePathOperationsTest, PublishVolumeMoveNodeAtPathRejectsDirectoryMovedIntoDescendant) {
  createInitialVolume();

  SequenceRandomGenerator parentRandom;
  parentRandom.add(dataForId(rootId(3)));
  parentRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &parentRandom,
      "/parent",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator childRandom;
  childRandom.add(dataForId(rootId(5)));
  childRandom.add(dataForId(objectId(6)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &childRandom,
      "/parent/child",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator moveRandom;
  EXPECT_THROW(
    publishVolumeMoveNodeAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &moveRandom,
      "/parent",
      "/parent/child/moved",
      metadata(),
      metadata()),
    std::runtime_error);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(5)}), *reopened.selectedRoot);
}

TEST_F(VolumePathOperationsTest, PublishVolumeMoveNodeAtPathReplacingTargetReplacesNonDirectoryTarget) {
  createInitialVolume();

  SequenceRandomGenerator sourceRandom;
  sourceRandom.add(dataForId(rootId(3)));
  sourceRandom.add(dataForId(objectId(4)));
  sourceRandom.add(dataForId(objectId(5)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileWithDataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &sourceRandom,
      "/source",
      metadata(),
      metadata(0600),
      payload("source-data")).status);

  SequenceRandomGenerator targetRandom;
  targetRandom.add(dataForId(rootId(6)));
  targetRandom.add(dataForId(objectId(7)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateSymlinkAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &targetRandom,
      "/target",
      metadata(),
      metadata(0777),
      "../old").status);

  SequenceRandomGenerator moveRandom;
  moveRandom.add(dataForId(rootId(8)));
  const auto result = publishVolumeMoveNodeAtPathReplacingTarget(
    layout(),
    filesystemId(),
    masterKey(),
    &moveRandom,
    "/source",
    "/target",
    metadata(0701),
    metadata(0702));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{4, rootId(8)}), *result->selectedRoot);
  EXPECT_FALSE(loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/source").is_initialized());
  const auto moved = loadVolumeFileContentsAtPath(layout(), filesystemId(), masterKey(), "/target");
  ASSERT_TRUE(moved.is_initialized());
  EXPECT_EQ("source-data", payloadString(*moved));
  EXPECT_TRUE(symlinkRecordStore().load(objectId(7), 1).is_initialized());
}

TEST_F(VolumePathOperationsTest, PublishVolumeMoveNodeAtPathReplacingTargetFallsBackWhenTargetIsAbsent) {
  createInitialVolume();

  SequenceRandomGenerator sourceRandom;
  sourceRandom.add(dataForId(rootId(3)));
  sourceRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &sourceRandom,
      "/source",
      metadata(),
      metadata(0600)).status);

  SequenceRandomGenerator moveRandom;
  moveRandom.add(dataForId(rootId(5)));
  const auto result = publishVolumeMoveNodeAtPathReplacingTarget(
    layout(),
    filesystemId(),
    masterKey(),
    &moveRandom,
    "/source",
    "/target",
    metadata(0701),
    metadata(0702));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  ASSERT_TRUE(result->selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(5)}), *result->selectedRoot);
  EXPECT_FALSE(loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/source").is_initialized());
  const auto target = loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/target");
  ASSERT_TRUE(target.is_initialized());
  EXPECT_EQ(ObjectType::File, target->type);
}

TEST_F(VolumePathOperationsTest, PublishVolumeMoveNodeAtPathReplacingTargetReplacesEmptyDirectoryTarget) {
  createInitialVolume();

  SequenceRandomGenerator sourceRandom;
  sourceRandom.add(dataForId(rootId(3)));
  sourceRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &sourceRandom,
      "/source",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator fileRandom;
  fileRandom.add(dataForId(rootId(5)));
  fileRandom.add(dataForId(objectId(6)));
  fileRandom.add(dataForId(objectId(7)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileWithDataAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &fileRandom,
      "/source/file",
      metadata(),
      metadata(0600),
      payload("nested")).status);

  SequenceRandomGenerator targetRandom;
  targetRandom.add(dataForId(rootId(8)));
  targetRandom.add(dataForId(objectId(9)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &targetRandom,
      "/target",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator moveRandom;
  moveRandom.add(dataForId(rootId(10)));
  const auto result = publishVolumeMoveNodeAtPathReplacingTarget(
    layout(),
    filesystemId(),
    masterKey(),
    &moveRandom,
    "/source",
    "/target",
    metadata(0701),
    metadata(0702));

  ASSERT_TRUE(result.is_initialized());
  ASSERT_EQ(RootOpenStatus::Selected, result->status);
  EXPECT_FALSE(loadVolumeNodeAtPath(layout(), filesystemId(), masterKey(), "/source").is_initialized());
  const auto movedFile = loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/target/file");
  ASSERT_TRUE(movedFile.is_initialized());
  EXPECT_EQ("nested", payloadString(*movedFile));
  EXPECT_TRUE(directoryRecordStore().load(objectId(9), 1).is_initialized());
}

TEST_F(VolumePathOperationsTest, PublishVolumeMoveNodeAtPathReplacingTargetRejectsNonEmptyDirectoryTarget) {
  createInitialVolume();

  SequenceRandomGenerator sourceRandom;
  sourceRandom.add(dataForId(rootId(3)));
  sourceRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &sourceRandom,
      "/source",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator targetRandom;
  targetRandom.add(dataForId(rootId(5)));
  targetRandom.add(dataForId(objectId(6)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &targetRandom,
      "/target",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator childRandom;
  childRandom.add(dataForId(rootId(7)));
  childRandom.add(dataForId(objectId(8)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &childRandom,
      "/target/file",
      metadata(),
      metadata(0600)).status);

  SequenceRandomGenerator moveRandom;
  EXPECT_THROW(
    publishVolumeMoveNodeAtPathReplacingTarget(
      layout(),
      filesystemId(),
      masterKey(),
      &moveRandom,
      "/source",
      "/target",
      metadata(),
      metadata()),
    std::runtime_error);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{4, rootId(7)}), *reopened.selectedRoot);
}

TEST_F(VolumePathOperationsTest, PublishVolumeMoveNodeAtPathReplacingTargetRejectsDirectoryNonDirectoryReplacement) {
  createInitialVolume();

  SequenceRandomGenerator sourceRandom;
  sourceRandom.add(dataForId(rootId(3)));
  sourceRandom.add(dataForId(objectId(4)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateFileAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &sourceRandom,
      "/source",
      metadata(),
      metadata(0600)).status);

  SequenceRandomGenerator targetRandom;
  targetRandom.add(dataForId(rootId(5)));
  targetRandom.add(dataForId(objectId(6)));
  ASSERT_EQ(
    RootOpenStatus::Selected,
    publishVolumeCreateDirectoryAtPath(
      layout(),
      filesystemId(),
      masterKey(),
      &targetRandom,
      "/target",
      metadata(),
      metadata()).status);

  SequenceRandomGenerator moveRandom;
  EXPECT_THROW(
    publishVolumeMoveNodeAtPathReplacingTarget(
      layout(),
      filesystemId(),
      masterKey(),
      &moveRandom,
      "/source",
      "/target",
      metadata(),
      metadata()),
    std::runtime_error);

  const auto reopened = openVolumeRoot(layout(), filesystemId(), masterKey());
  ASSERT_EQ(RootOpenStatus::Selected, reopened.status);
  ASSERT_TRUE(reopened.selectedRoot.is_initialized());
  EXPECT_EQ((AuthenticatedRoot{3, rootId(5)}), *reopened.selectedRoot);
}
