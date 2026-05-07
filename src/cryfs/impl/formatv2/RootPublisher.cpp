#include "RootPublisher.h"

#include "RootSelection.h"

#include <stdexcept>
#include <vector>

namespace cryfs {
namespace formatv2 {
namespace {

constexpr uint64_t INITIAL_ROOT_EPOCH = 1;
constexpr uint64_t INITIAL_ROOT_DIRECTORY_GENERATION = 1;

AuthenticatedRoot authenticatedRoot(const RootRecord &record) {
  return AuthenticatedRoot{record.epoch, record.rootId};
}

bool containsRoot(
  const std::vector<AuthenticatedRoot> &roots,
  const AuthenticatedRoot &candidate) {
  for (const AuthenticatedRoot &root: roots) {
    if (root == candidate) {
      return true;
    }
  }
  return false;
}

void validateRootBindings(
  const RootRecord &rootRecord,
  const RootContent &rootContent,
  const DirectoryRecord &rootDirectory) {
  if (rootRecord.filesystemId == FilesystemId::Null()) {
    throw std::runtime_error("Refusing to publish format-v2 root for a null filesystem");
  }

  const boost::optional<RootDirectoryRef> rootDirectoryRef =
    trustedRootDirectoryFromContent(rootContent, rootRecord.filesystemId, authenticatedRoot(rootRecord));
  if (rootDirectoryRef == boost::none) {
    throw std::runtime_error("Refusing to publish format-v2 root with mismatched root content");
  }

  if (!directoryRecordMatchesObject(
        rootDirectory,
        rootRecord.filesystemId,
        rootDirectoryRef->objectId,
        rootDirectoryRef->generation)) {
    throw std::runtime_error("Refusing to publish format-v2 root with mismatched root directory");
  }
}

void ensureRootCanBecomeSelected(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const AuthenticatedRoot &candidate) {
  std::vector<AuthenticatedRoot> roots = publicationStore.loadAuthenticatedRoots();
  if (containsRoot(roots, candidate)) {
    throw std::runtime_error("Refusing to republish an existing format-v2 root");
  }

  roots.push_back(candidate);
  const RootSelectionResult selection = selectRoot(roots, acceptedRootStateStore.load());
  if (selection.status != RootSelectionStatus::Selected
      || selection.selectedRoot == boost::none
      || *selection.selectedRoot != candidate) {
    throw std::runtime_error("Refusing to publish a format-v2 root that would not become selected");
  }
}

RootOpenWithValidatedTreeResult invalidTreeResult(
  const RootRecord &rootRecord,
  const RootContent &rootContent,
  const DirectoryRecord &rootDirectory,
  const ObjectTreeValidationResult &validation) {
  return RootOpenWithValidatedTreeResult{
    RootOpenStatus::RootTreeInvalid,
    authenticatedRoot(rootRecord),
    rootContent,
    rootDirectory,
    validation
  };
}

}

RootOpenWithValidatedTreeResult publishValidatedRoot(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootRecord &rootRecord,
  const RootContent &rootContent,
  const DirectoryRecord &rootDirectory) {
  validateRootBindings(rootRecord, rootContent, rootDirectory);
  ensureRootCanBecomeSelected(publicationStore, acceptedRootStateStore, authenticatedRoot(rootRecord));

  const ObjectTreeValidationResult validation = validateObjectTree(
    rootDirectory,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore);
  if (validation.status != ObjectTreeValidationStatus::Valid) {
    return invalidTreeResult(rootRecord, rootContent, rootDirectory, validation);
  }

  directoryRecordStore.store(rootDirectory);
  rootContentStore.store(rootContent);
  publicationStore.publish(rootRecord);

  return openRootWithValidatedTree(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore);
}

RootOpenWithValidatedTreeResult createInitialEmptyRoot(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  FilesystemId filesystemId,
  RootId rootId,
  ObjectId rootDirectoryId,
  DirectoryMetadata rootDirectoryMetadata) {
  if (acceptedRootStateStore.load() != boost::none
      || !publicationStore.loadAuthenticatedRoots().empty()) {
    throw std::runtime_error("Refusing to create an initial format-v2 root for an initialized filesystem");
  }

  const RootRecord rootRecord{
    filesystemId,
    INITIAL_ROOT_EPOCH,
    rootId
  };
  const RootContent rootContent{
    filesystemId,
    INITIAL_ROOT_EPOCH,
    rootId,
    rootDirectoryId,
    INITIAL_ROOT_DIRECTORY_GENERATION
  };
  const DirectoryRecord rootDirectory{
    filesystemId,
    rootDirectoryId,
    INITIAL_ROOT_DIRECTORY_GENERATION,
    rootDirectoryMetadata,
    {}
  };

  return publishValidatedRoot(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    rootRecord,
    rootContent,
    rootDirectory);
}

}
}
