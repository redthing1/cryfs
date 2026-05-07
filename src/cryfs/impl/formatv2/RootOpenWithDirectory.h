#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ROOTOPENWITHDIRECTORY_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ROOTOPENWITHDIRECTORY_H_

#include "DirectoryRecordStore.h"
#include "ObjectTreeValidator.h"
#include "RootOpenWithContent.h"

#include <boost/optional.hpp>

namespace cryfs {
namespace formatv2 {

struct RootOpenWithDirectoryResult final {
  RootOpenStatus status;
  boost::optional<AuthenticatedRoot> selectedRoot;
  boost::optional<RootContent> rootContent;
  boost::optional<DirectoryRecord> rootDirectory;
};

struct RootOpenWithValidatedTreeResult final {
  RootOpenStatus status;
  boost::optional<AuthenticatedRoot> selectedRoot;
  boost::optional<RootContent> rootContent;
  boost::optional<DirectoryRecord> rootDirectory;
  boost::optional<ObjectTreeValidationResult> treeValidation;
};

// A Selected result means root content and root directory were loaded, but
// local accepted-root state was not advanced.
RootOpenWithDirectoryResult selectRootWithDirectory(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore);

// A Selected result means root content, root directory, and the reachable object
// tree were validated and local accepted-root state was advanced.
RootOpenWithValidatedTreeResult openRootWithValidatedTree(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore);

}
}

#endif
