#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ROOTPUBLISHER_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ROOTPUBLISHER_H_

#include "RootOpenWithDirectory.h"

namespace cryfs {
namespace formatv2 {

RootOpenWithValidatedTreeResult publishValidatedRoot(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootRecord &rootRecord,
  const RootContent &rootContent,
  const DirectoryRecord &rootDirectory);

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
  DirectoryMetadata rootDirectoryMetadata);

}
}

#endif
