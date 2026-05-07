#include "Volume.h"

#include "KeyDerivation.h"
#include "RootTransaction.h"

#include <cpp-utils/system/AtomicFile.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace bf = boost::filesystem;

namespace cryfs {
namespace formatv2 {
namespace {

constexpr const char *FORMAT_V2_BASE_DIRECTORY = "format-v2";
constexpr const char *OBJECTS_DIRECTORY = "objects";

template<class Id>
Id generateNonNullId(cpputils::RandomGenerator *randomGenerator, const char *description) {
  if (randomGenerator == nullptr) {
    throw std::runtime_error(std::string("Missing random generator for format-v2 ") + description);
  }

  for (size_t attempt = 0; attempt != 16; ++attempt) {
    const Id id = randomGenerator->getFixedSize<Id::BINARY_LENGTH>();
    if (id != Id::Null()) {
      return id;
    }
  }
  throw std::runtime_error(std::string("Failed to generate non-null format-v2 ") + description);
}

void validateMasterKey(const cpputils::EncryptionKey &masterKey) {
  if (masterKey.binaryLength() != FORMAT_V2_MASTER_KEY_SIZE) {
    throw std::runtime_error("Format-v2 volume initialization requires a 256-bit master key");
  }
}

struct VolumeStores final {
  RootPublicationStore publicationStore;
  AcceptedRootStateStore acceptedRootStateStore;
  RootContentStore rootContentStore;
  DirectoryRecordStore directoryRecordStore;
  FileRecordStore fileRecordStore;
  SymlinkRecordStore symlinkRecordStore;
};

VolumeStores createStores(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey) {
  validateMasterKey(masterKey);

  const cpputils::EncryptionKey rootAuthenticationKey =
    deriveRootAuthenticationKey(masterKey, filesystemId);
  const cpputils::EncryptionKey objectEncryptionKey =
    deriveObjectEncryptionKey(masterKey, filesystemId);

  cpputils::createDirectoryDurably(layout.rootsDirectory.parent_path());
  cpputils::createDirectoryDurably(layout.directoriesDirectory.parent_path());
  cpputils::createDirectoryDurably(layout.acceptedRootFile.parent_path());

  return VolumeStores{
    RootPublicationStore(layout.rootsDirectory, filesystemId, rootAuthenticationKey),
    AcceptedRootStateStore(layout.acceptedRootFile, filesystemId),
    RootContentStore(layout.rootContentDirectory, filesystemId, objectEncryptionKey),
    DirectoryRecordStore(layout.directoriesDirectory, filesystemId, objectEncryptionKey),
    FileRecordStore(layout.filesDirectory, filesystemId, objectEncryptionKey),
    SymlinkRecordStore(layout.symlinksDirectory, filesystemId, objectEncryptionKey)
  };
}

bool isSelectedEmptyRoot(const RootOpenWithValidatedTreeResult &result) {
  return result.status == RootOpenStatus::Selected
      && result.rootDirectory != boost::none
      && result.rootDirectory->entries.empty();
}

}

VolumeLayout volumeLayout(
  const bf::path &baseDir,
  const bf::path &localStateFilesystemDir) {
  const bf::path volumeDir = baseDir / FORMAT_V2_BASE_DIRECTORY;
  const bf::path objectsDir = volumeDir / OBJECTS_DIRECTORY;
  const bf::path localStateDir = localStateFilesystemDir / FORMAT_V2_BASE_DIRECTORY;

  return VolumeLayout{
    volumeDir / "roots",
    volumeDir / "root-content",
    objectsDir / "directories",
    objectsDir / "files",
    objectsDir / "symlinks",
    localStateDir / "accepted-root"
  };
}

DirectoryMetadata initialRootDirectoryMetadata() {
  const Timestamp zero{0, 0};
  return DirectoryMetadata{
    0700,
    0,
    0,
    zero,
    zero,
    zero
  };
}

RootOpenWithValidatedTreeResult openVolumeRoot(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  return openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
}

RootOpenWithValidatedTreeResult createOrOpenInitialEmptyVolume(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  DirectoryMetadata rootDirectoryMetadata) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);

  const RootOpenWithValidatedTreeResult existing = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (isSelectedEmptyRoot(existing)) {
    return existing;
  }
  if (existing.status != RootOpenStatus::NoAuthenticatedRoots) {
    throw std::runtime_error("Refusing to initialize format-v2 volume over existing root state");
  }

  const RootId rootId = generateNonNullId<RootId>(randomGenerator, "root id");
  const ObjectId rootDirectoryId = generateNonNullId<ObjectId>(randomGenerator, "root directory id");

  return createInitialEmptyRoot(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    filesystemId,
    rootId,
    rootDirectoryId,
    std::move(rootDirectoryMetadata));
}

RootOpenWithValidatedTreeResult publishVolumeRootDirectoryTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  DirectoryMetadata rootDirectoryMetadata,
  std::vector<DirectoryEntry> rootDirectoryEntries) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  return publishRootDirectoryTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    std::move(rootDirectoryMetadata),
    std::move(rootDirectoryEntries));
}

RootOpenWithValidatedTreeResult publishVolumeCreateRootDirectoryChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  const ObjectId childDirectoryId =
    generateNonNullId<ObjectId>(randomGenerator, "child directory id");
  return publishCreateRootDirectoryChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    childDirectoryId,
    std::move(childName),
    std::move(rootDirectoryMetadata),
    std::move(childDirectoryMetadata));
}

RootOpenWithValidatedTreeResult publishVolumeCreateRootFileChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  FileMetadata childFileMetadata) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  const ObjectId childFileId =
    generateNonNullId<ObjectId>(randomGenerator, "child file id");
  return publishCreateRootFileChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    childFileId,
    std::move(childName),
    std::move(rootDirectoryMetadata),
    std::move(childFileMetadata));
}

RootOpenWithValidatedTreeResult publishVolumeCreateRootFileWithDataChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  const ObjectId childFileId =
    generateNonNullId<ObjectId>(randomGenerator, "child file id");
  const ObjectId childFileDataId =
    generateNonNullId<ObjectId>(randomGenerator, "child file data id");
  return publishCreateRootFileWithDataChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    childFileId,
    childFileDataId,
    std::move(childName),
    std::move(rootDirectoryMetadata),
    std::move(childFileMetadata),
    std::move(childFilePayload));
}

RootOpenWithValidatedTreeResult publishVolumeCreateRootSymlinkChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  const ObjectId childSymlinkId =
    generateNonNullId<ObjectId>(randomGenerator, "child symlink id");
  return publishCreateRootSymlinkChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    childSymlinkId,
    std::move(childName),
    std::move(rootDirectoryMetadata),
    std::move(childSymlinkMetadata),
    std::move(childSymlinkTarget));
}

RootOpenWithValidatedTreeResult publishVolumeCreateNestedDirectoryChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const std::vector<DirectoryPathSegment> parentPath = loadDirectoryPathFromRoot(
    baseRoot,
    stores.directoryRecordStore,
    parentDirectoryPath);
  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  const ObjectId childDirectoryId =
    generateNonNullId<ObjectId>(randomGenerator, "child directory id");
  return publishCreateNestedDirectoryChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    parentPath,
    childDirectoryId,
    std::move(childName),
    std::move(parentDirectoryMetadata),
    std::move(childDirectoryMetadata));
}

RootOpenWithValidatedTreeResult publishVolumeCreateNestedFileChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const std::vector<DirectoryPathSegment> parentPath = loadDirectoryPathFromRoot(
    baseRoot,
    stores.directoryRecordStore,
    parentDirectoryPath);
  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  const ObjectId childFileId =
    generateNonNullId<ObjectId>(randomGenerator, "child file id");
  return publishCreateNestedFileChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    parentPath,
    childFileId,
    std::move(childName),
    std::move(parentDirectoryMetadata),
    std::move(childFileMetadata));
}

RootOpenWithValidatedTreeResult publishVolumeCreateNestedFileWithDataChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const std::vector<DirectoryPathSegment> parentPath = loadDirectoryPathFromRoot(
    baseRoot,
    stores.directoryRecordStore,
    parentDirectoryPath);
  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  const ObjectId childFileId =
    generateNonNullId<ObjectId>(randomGenerator, "child file id");
  const ObjectId childFileDataId =
    generateNonNullId<ObjectId>(randomGenerator, "child file data id");
  return publishCreateNestedFileWithDataChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    parentPath,
    childFileId,
    childFileDataId,
    std::move(childName),
    std::move(parentDirectoryMetadata),
    std::move(childFileMetadata),
    std::move(childFilePayload));
}

RootOpenWithValidatedTreeResult publishVolumeCreateNestedSymlinkChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const std::vector<DirectoryPathSegment> parentPath = loadDirectoryPathFromRoot(
    baseRoot,
    stores.directoryRecordStore,
    parentDirectoryPath);
  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  const ObjectId childSymlinkId =
    generateNonNullId<ObjectId>(randomGenerator, "child symlink id");
  return publishCreateNestedSymlinkChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    parentPath,
    childSymlinkId,
    std::move(childName),
    std::move(parentDirectoryMetadata),
    std::move(childSymlinkMetadata),
    std::move(childSymlinkTarget));
}

RootOpenWithValidatedTreeResult publishVolumeUpdateRootFileChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::string childName,
  FileUpdate update) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  return publishUpdateRootFileChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    std::move(childName),
    std::move(update));
}

RootOpenWithValidatedTreeResult publishVolumeUpdateNestedFileChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  std::string childName,
  FileUpdate update) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const std::vector<DirectoryPathSegment> parentPath = loadDirectoryPathFromRoot(
    baseRoot,
    stores.directoryRecordStore,
    parentDirectoryPath);
  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  return publishUpdateNestedFileChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    parentPath,
    std::move(childName),
    std::move(update));
}

RootOpenWithValidatedTreeResult publishVolumeUpdateRootSymlinkChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::string childName,
  SymlinkUpdate update) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  return publishUpdateRootSymlinkChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    std::move(childName),
    std::move(update));
}

RootOpenWithValidatedTreeResult publishVolumeUpdateNestedSymlinkChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  std::string childName,
  SymlinkUpdate update) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const std::vector<DirectoryPathSegment> parentPath = loadDirectoryPathFromRoot(
    baseRoot,
    stores.directoryRecordStore,
    parentDirectoryPath);
  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  return publishUpdateNestedSymlinkChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    parentPath,
    std::move(childName),
    std::move(update));
}

RootOpenWithValidatedTreeResult publishVolumeUpdateRootDirectoryMetadataTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  DirectoryMetadata rootDirectoryMetadata) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected || baseRoot.rootDirectory == boost::none) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  return publishRootDirectoryTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    std::move(rootDirectoryMetadata),
    baseRoot.rootDirectory->entries);
}

RootOpenWithValidatedTreeResult publishVolumeUpdateNestedDirectoryMetadataTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> directoryPath,
  DirectoryMetadata directoryMetadata) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const std::vector<DirectoryPathSegment> path = loadDirectoryPathFromRoot(
    baseRoot,
    stores.directoryRecordStore,
    directoryPath);
  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  return publishUpdateNestedDirectoryMetadataTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    path,
    std::move(directoryMetadata));
}

RootOpenWithValidatedTreeResult publishVolumeRemoveRootChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  DirectoryChildReference child,
  DirectoryMetadata rootDirectoryMetadata) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  return publishRemoveRootChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    std::move(child),
    std::move(rootDirectoryMetadata));
}

RootOpenWithValidatedTreeResult publishVolumeRemoveNestedChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  DirectoryChildReference child,
  DirectoryMetadata parentDirectoryMetadata) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const std::vector<DirectoryPathSegment> parentPath = loadDirectoryPathFromRoot(
    baseRoot,
    stores.directoryRecordStore,
    parentDirectoryPath);
  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  return publishRemoveNestedChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    parentPath,
    std::move(child),
    std::move(parentDirectoryMetadata));
}

RootOpenWithValidatedTreeResult publishVolumeMoveChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> sourceParentDirectoryPath,
  DirectoryChildReference sourceChild,
  std::vector<std::string> targetParentDirectoryPath,
  std::string targetChildName,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const std::vector<DirectoryPathSegment> sourceParentPath = loadDirectoryPathFromRoot(
    baseRoot,
    stores.directoryRecordStore,
    sourceParentDirectoryPath);
  const std::vector<DirectoryPathSegment> targetParentPath = loadDirectoryPathFromRoot(
    baseRoot,
    stores.directoryRecordStore,
    targetParentDirectoryPath);
  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  return publishMoveChildTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    sourceParentPath,
    std::move(sourceChild),
    targetParentPath,
    std::move(targetChildName),
    std::move(sourceParentDirectoryMetadata),
    std::move(targetParentDirectoryMetadata));
}

RootOpenWithValidatedTreeResult publishVolumeMoveChildReplacingTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> sourceParentDirectoryPath,
  DirectoryChildReference sourceChild,
  std::vector<std::string> targetParentDirectoryPath,
  DirectoryChildReference targetChild,
  std::string targetChildName,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata) {
  const VolumeStores stores = createStores(layout, filesystemId, masterKey);
  const RootOpenWithValidatedTreeResult baseRoot = openRootWithValidatedTree(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore);
  if (baseRoot.status != RootOpenStatus::Selected) {
    throw std::runtime_error("Cannot publish a format-v2 transaction without a selected base root");
  }

  const std::vector<DirectoryPathSegment> sourceParentPath = loadDirectoryPathFromRoot(
    baseRoot,
    stores.directoryRecordStore,
    sourceParentDirectoryPath);
  const std::vector<DirectoryPathSegment> targetParentPath = loadDirectoryPathFromRoot(
    baseRoot,
    stores.directoryRecordStore,
    targetParentDirectoryPath);
  const RootId nextRootId = generateNonNullId<RootId>(randomGenerator, "transaction root id");
  return publishMoveChildReplacingTransaction(
    stores.publicationStore,
    stores.acceptedRootStateStore,
    stores.rootContentStore,
    stores.directoryRecordStore,
    stores.fileRecordStore,
    stores.symlinkRecordStore,
    baseRoot,
    nextRootId,
    sourceParentPath,
    std::move(sourceChild),
    targetParentPath,
    std::move(targetChild),
    std::move(targetChildName),
    std::move(sourceParentDirectoryMetadata),
    std::move(targetParentDirectoryMetadata));
}

}
}
