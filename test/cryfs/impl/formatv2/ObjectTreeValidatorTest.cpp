#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/ObjectTreeValidator.h>

#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using cryfs::formatv2::DirectoryEntry;
using cryfs::formatv2::DirectoryMetadata;
using cryfs::formatv2::DirectoryRecord;
using cryfs::formatv2::DirectoryRecordStore;
using cryfs::formatv2::FileDataRecord;
using cryfs::formatv2::FileExtent;
using cryfs::formatv2::FileMetadata;
using cryfs::formatv2::FileRecord;
using cryfs::formatv2::FileRecordStore;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::ObjectTreeRecordType;
using cryfs::formatv2::ObjectTreeValidationStatus;
using cryfs::formatv2::ObjectType;
using cryfs::formatv2::SymlinkMetadata;
using cryfs::formatv2::SymlinkRecord;
using cryfs::formatv2::SymlinkRecordStore;
using cryfs::formatv2::Timestamp;
using cryfs::formatv2::deriveObjectEncryptionKey;
using cryfs::formatv2::validateObjectTree;

namespace {

FilesystemId filesystemId() {
  return FilesystemId::FromString("00112233445566778899AABBCCDDEEFF");
}

ObjectId objectId(unsigned int seed) {
  return cpputils::DataFixture::generateFixedSize<ObjectId::BINARY_LENGTH>(seed);
}

cpputils::EncryptionKey masterKey(unsigned int seed = 1) {
  return cpputils::EncryptionKey::FromString(
    cpputils::DataFixture::generate(FORMAT_V2_MASTER_KEY_SIZE, seed).ToString());
}

cpputils::EncryptionKey objectEncryptionKey() {
  return deriveObjectEncryptionKey(masterKey(), filesystemId());
}

Timestamp timestamp(int64_t seconds, uint32_t nanoseconds) {
  return Timestamp{seconds, nanoseconds};
}

DirectoryMetadata directoryMetadata() {
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

DirectoryEntry entry(const std::string &name, ObjectType type, unsigned int objectIdSeed, uint64_t generation) {
  return DirectoryEntry{name, type, objectId(objectIdSeed), generation};
}

DirectoryRecord directoryRecord(
  unsigned int directoryIdSeed,
  uint64_t generation,
  std::vector<DirectoryEntry> entries) {
  return DirectoryRecord{
    filesystemId(),
    objectId(directoryIdSeed),
    generation,
    directoryMetadata(),
    std::move(entries)
  };
}

FileExtent extent(uint64_t offset, uint64_t size, unsigned int dataIdSeed, uint64_t generation) {
  return FileExtent{offset, size, objectId(dataIdSeed), generation};
}

FileRecord fileRecord() {
  return FileRecord{
    filesystemId(),
    objectId(2),
    3,
    fileMetadata(),
    8,
    {
      extent(0, 4, 3, 4),
      extent(6, 2, 4, 5)
    }
  };
}

FileDataRecord fileDataRecord(unsigned int dataIdSeed, uint64_t generation, size_t payloadSize) {
  return FileDataRecord{
    filesystemId(),
    objectId(dataIdSeed),
    generation,
    cpputils::DataFixture::generate(payloadSize)
  };
}

SymlinkRecord symlinkRecord() {
  return SymlinkRecord{
    filesystemId(),
    objectId(5),
    6,
    symlinkMetadata(),
    "../target"
  };
}

class ObjectTreeValidatorTestFixture : public ::testing::Test {
public:
  ObjectTreeValidatorTestFixture()
    : directoryRecordStore(_tempDir.path() / "directories", filesystemId(), objectEncryptionKey()),
      fileRecordStore(_tempDir.path() / "files", filesystemId(), objectEncryptionKey()),
      symlinkRecordStore(_tempDir.path() / "symlinks", filesystemId(), objectEncryptionKey()) {
  }

protected:
  void storeValidFile() const {
    const FileRecord file = fileRecord();
    fileRecordStore.storeData(fileDataRecord(3, 4, 4));
    fileRecordStore.storeData(fileDataRecord(4, 5, 2));
    fileRecordStore.store(file);
  }

  cpputils::TempDir _tempDir;
  DirectoryRecordStore directoryRecordStore;
  FileRecordStore fileRecordStore;
  SymlinkRecordStore symlinkRecordStore;
};

}

TEST_F(ObjectTreeValidatorTestFixture, AcceptsReachableTree) {
  const DirectoryRecord childDirectory = directoryRecord(6, 7, {});
  const SymlinkRecord symlink = symlinkRecord();
  const DirectoryRecord root = directoryRecord(1, 2, {
    entry("file", ObjectType::File, 2, 3),
    entry("link", ObjectType::Symlink, 5, 6),
    entry("sub", ObjectType::Directory, 6, 7)
  });
  storeValidFile();
  symlinkRecordStore.store(symlink);
  directoryRecordStore.store(childDirectory);

  const auto result = validateObjectTree(root, directoryRecordStore, fileRecordStore, symlinkRecordStore);

  EXPECT_EQ(ObjectTreeValidationStatus::Valid, result.status);
}

TEST_F(ObjectTreeValidatorTestFixture, ReportsMissingFile) {
  const DirectoryRecord root = directoryRecord(1, 2, {
    entry("file", ObjectType::File, 2, 3)
  });

  const auto result = validateObjectTree(root, directoryRecordStore, fileRecordStore, symlinkRecordStore);

  EXPECT_EQ(ObjectTreeValidationStatus::MissingFile, result.status);
  EXPECT_EQ("/file", result.path);
  EXPECT_EQ(ObjectTreeRecordType::File, result.expectedType);
  EXPECT_EQ(objectId(2), result.objectId);
  EXPECT_EQ(3u, result.generation);
}

TEST_F(ObjectTreeValidatorTestFixture, ReportsMissingFileData) {
  const FileRecord file = fileRecord();
  const DirectoryRecord root = directoryRecord(1, 2, {
    entry("file", ObjectType::File, 2, 3)
  });
  fileRecordStore.store(file);

  const auto result = validateObjectTree(root, directoryRecordStore, fileRecordStore, symlinkRecordStore);

  EXPECT_EQ(ObjectTreeValidationStatus::MissingFileData, result.status);
  EXPECT_EQ("/file", result.path);
  EXPECT_EQ(ObjectTreeRecordType::FileData, result.expectedType);
  EXPECT_EQ(objectId(3), result.objectId);
  EXPECT_EQ(4u, result.generation);
}

TEST_F(ObjectTreeValidatorTestFixture, ReportsFileDataSizeMismatch) {
  const FileRecord file = fileRecord();
  const DirectoryRecord root = directoryRecord(1, 2, {
    entry("file", ObjectType::File, 2, 3)
  });
  fileRecordStore.storeData(fileDataRecord(3, 4, 3));
  fileRecordStore.storeData(fileDataRecord(4, 5, 2));
  fileRecordStore.store(file);

  const auto result = validateObjectTree(root, directoryRecordStore, fileRecordStore, symlinkRecordStore);

  EXPECT_EQ(ObjectTreeValidationStatus::FileDataSizeMismatch, result.status);
  EXPECT_EQ("/file", result.path);
  EXPECT_EQ(ObjectTreeRecordType::FileData, result.expectedType);
  EXPECT_EQ(objectId(3), result.objectId);
  EXPECT_EQ(4u, result.generation);
}

TEST_F(ObjectTreeValidatorTestFixture, ReportsMissingSymlink) {
  const DirectoryRecord root = directoryRecord(1, 2, {
    entry("link", ObjectType::Symlink, 5, 6)
  });

  const auto result = validateObjectTree(root, directoryRecordStore, fileRecordStore, symlinkRecordStore);

  EXPECT_EQ(ObjectTreeValidationStatus::MissingSymlink, result.status);
  EXPECT_EQ("/link", result.path);
  EXPECT_EQ(ObjectTreeRecordType::Symlink, result.expectedType);
  EXPECT_EQ(objectId(5), result.objectId);
  EXPECT_EQ(6u, result.generation);
}

TEST_F(ObjectTreeValidatorTestFixture, ReportsMissingDirectory) {
  const DirectoryRecord root = directoryRecord(1, 2, {
    entry("sub", ObjectType::Directory, 6, 7)
  });

  const auto result = validateObjectTree(root, directoryRecordStore, fileRecordStore, symlinkRecordStore);

  EXPECT_EQ(ObjectTreeValidationStatus::MissingDirectory, result.status);
  EXPECT_EQ("/sub", result.path);
  EXPECT_EQ(ObjectTreeRecordType::Directory, result.expectedType);
  EXPECT_EQ(objectId(6), result.objectId);
  EXPECT_EQ(7u, result.generation);
}

TEST_F(ObjectTreeValidatorTestFixture, ReportsRootDirectoryCycle) {
  const DirectoryRecord root = directoryRecord(1, 2, {
    entry("loop", ObjectType::Directory, 1, 2)
  });

  const auto result = validateObjectTree(root, directoryRecordStore, fileRecordStore, symlinkRecordStore);

  EXPECT_EQ(ObjectTreeValidationStatus::DirectoryCycle, result.status);
  EXPECT_EQ("/loop", result.path);
  EXPECT_EQ(ObjectTreeRecordType::Directory, result.expectedType);
  EXPECT_EQ(objectId(1), result.objectId);
  EXPECT_EQ(2u, result.generation);
}

TEST_F(ObjectTreeValidatorTestFixture, ReportsNestedDirectoryCycle) {
  const DirectoryRecord childDirectory = directoryRecord(6, 7, {
    entry("back", ObjectType::Directory, 1, 2)
  });
  const DirectoryRecord root = directoryRecord(1, 2, {
    entry("sub", ObjectType::Directory, 6, 7)
  });
  directoryRecordStore.store(childDirectory);

  const auto result = validateObjectTree(root, directoryRecordStore, fileRecordStore, symlinkRecordStore);

  EXPECT_EQ(ObjectTreeValidationStatus::DirectoryCycle, result.status);
  EXPECT_EQ("/sub/back", result.path);
  EXPECT_EQ(ObjectTreeRecordType::Directory, result.expectedType);
  EXPECT_EQ(objectId(1), result.objectId);
  EXPECT_EQ(2u, result.generation);
}
