#include "RootOpenWithDirectory.h"

namespace cryfs {
namespace formatv2 {

RootOpenWithDirectoryResult selectRootWithDirectory(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore) {
  const RootOpenResult contentResult = selectRootWithContent(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore);
  if (contentResult.status != RootOpenStatus::Selected) {
    return RootOpenWithDirectoryResult{
      contentResult.status,
      contentResult.selectedRoot,
      contentResult.rootContent,
      boost::none
    };
  }

  const RootDirectoryRef directoryRef{
    contentResult.rootContent->rootDirectoryId,
    contentResult.rootContent->rootDirectoryGeneration
  };
  const boost::optional<DirectoryRecord> rootDirectory =
    directoryRecordStore.load(directoryRef.objectId, directoryRef.generation);
  if (rootDirectory == boost::none) {
    return RootOpenWithDirectoryResult{
      RootOpenStatus::RootDirectoryUnavailable,
      contentResult.selectedRoot,
      contentResult.rootContent,
      boost::none
    };
  }

  return RootOpenWithDirectoryResult{
    RootOpenStatus::Selected,
    contentResult.selectedRoot,
    contentResult.rootContent,
    rootDirectory
  };
}

RootOpenWithValidatedTreeResult openRootWithValidatedTree(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore) {
  const RootOpenWithDirectoryResult directoryResult = selectRootWithDirectory(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore,
    directoryRecordStore);
  if (directoryResult.status != RootOpenStatus::Selected) {
    return RootOpenWithValidatedTreeResult{
      directoryResult.status,
      directoryResult.selectedRoot,
      directoryResult.rootContent,
      directoryResult.rootDirectory,
      boost::none
    };
  }

  const ObjectTreeValidationResult validation = validateObjectTree(
    *directoryResult.rootDirectory,
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore);
  if (validation.status != ObjectTreeValidationStatus::Valid) {
    return RootOpenWithValidatedTreeResult{
      RootOpenStatus::RootTreeInvalid,
      directoryResult.selectedRoot,
      directoryResult.rootContent,
      directoryResult.rootDirectory,
      validation
    };
  }

  acceptedRootStateStore.advanceTo(*directoryResult.selectedRoot);
  return RootOpenWithValidatedTreeResult{
    RootOpenStatus::Selected,
    directoryResult.selectedRoot,
    directoryResult.rootContent,
    directoryResult.rootDirectory,
    validation
  };
}

}
}
