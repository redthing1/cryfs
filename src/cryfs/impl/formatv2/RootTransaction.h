#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ROOTTRANSACTION_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ROOTTRANSACTION_H_

#include "RootPublisher.h"

#include <cpp-utils/data/Data.h>

#include <string>
#include <vector>

namespace cryfs {
namespace formatv2 {

struct RootDirectoryTransaction final {
  RootRecord rootRecord;
  RootContent rootContent;
  DirectoryRecord rootDirectory;
};

struct DirectoryPathSegment final {
  std::string name;
  DirectoryRecord directory;
};

struct DirectoryPathTransaction final {
  std::vector<DirectoryRecord> stagedDirectories;
  RootDirectoryTransaction rootUpdate;
};

struct DirectoryChildReference final {
  std::string name;
  ObjectType type;
  ObjectId objectId;
  uint64_t generation;
};

struct CreateRootDirectoryChildTransaction final {
  DirectoryRecord childDirectory;
  RootDirectoryTransaction rootUpdate;
};

struct CreateRootFileChildTransaction final {
  FileRecord childFile;
  RootDirectoryTransaction rootUpdate;
};

struct CreateRootFileWithDataChildTransaction final {
  FileRecord childFile;
  FileDataRecord childFileData;
  RootDirectoryTransaction rootUpdate;
};

struct CreateRootSymlinkChildTransaction final {
  SymlinkRecord childSymlink;
  RootDirectoryTransaction rootUpdate;
};

struct CreateNestedDirectoryChildTransaction final {
  DirectoryRecord childDirectory;
  DirectoryPathTransaction pathUpdate;
};

struct CreateNestedFileChildTransaction final {
  FileRecord childFile;
  DirectoryPathTransaction pathUpdate;
};

struct CreateNestedFileWithDataChildTransaction final {
  FileRecord childFile;
  FileDataRecord childFileData;
  DirectoryPathTransaction pathUpdate;
};

struct CreateNestedSymlinkChildTransaction final {
  SymlinkRecord childSymlink;
  DirectoryPathTransaction pathUpdate;
};

struct FileUpdate final {
  FileRecord baseFile;
  FileMetadata metadata;
  uint64_t size;
  std::vector<FileExtent> extents;
  std::vector<FileDataRecord> fileData;
};

struct SymlinkUpdate final {
  SymlinkRecord baseSymlink;
  SymlinkMetadata metadata;
  std::string target;
};

struct UpdateRootFileChildTransaction final {
  FileRecord file;
  std::vector<FileDataRecord> fileData;
  RootDirectoryTransaction rootUpdate;
};

struct UpdateNestedFileChildTransaction final {
  FileRecord file;
  std::vector<FileDataRecord> fileData;
  DirectoryPathTransaction pathUpdate;
};

struct UpdateRootSymlinkChildTransaction final {
  SymlinkRecord symlink;
  RootDirectoryTransaction rootUpdate;
};

struct UpdateNestedSymlinkChildTransaction final {
  SymlinkRecord symlink;
  DirectoryPathTransaction pathUpdate;
};

struct RemoveRootChildTransaction final {
  RootDirectoryTransaction rootUpdate;
};

struct RemoveNestedChildTransaction final {
  DirectoryPathTransaction pathUpdate;
};

struct MoveChildTransaction final {
  DirectoryPathTransaction pathUpdate;
};

std::vector<DirectoryPathSegment> loadDirectoryPathFromRoot(
  const RootOpenWithValidatedTreeResult &baseRoot,
  const DirectoryRecordStore &directoryRecordStore,
  const std::vector<std::string> &directoryPath);

RootDirectoryTransaction buildRootDirectoryTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  DirectoryMetadata rootDirectoryMetadata,
  std::vector<DirectoryEntry> rootDirectoryEntries);

CreateRootDirectoryChildTransaction buildCreateRootDirectoryChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childDirectoryId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata);

CreateRootFileChildTransaction buildCreateRootFileChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childFileId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  FileMetadata childFileMetadata);

CreateRootFileWithDataChildTransaction buildCreateRootFileWithDataChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childFileId,
  ObjectId childFileDataId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload);

CreateRootSymlinkChildTransaction buildCreateRootSymlinkChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childSymlinkId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget);

CreateNestedDirectoryChildTransaction buildCreateNestedDirectoryChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childDirectoryId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata);

CreateNestedFileChildTransaction buildCreateNestedFileChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childFileId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata);

CreateNestedFileWithDataChildTransaction buildCreateNestedFileWithDataChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childFileId,
  ObjectId childFileDataId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload);

CreateNestedSymlinkChildTransaction buildCreateNestedSymlinkChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childSymlinkId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget);

UpdateRootFileChildTransaction buildUpdateRootFileChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::string childName,
  FileUpdate update);

UpdateNestedFileChildTransaction buildUpdateNestedFileChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  std::string childName,
  FileUpdate update);

UpdateRootSymlinkChildTransaction buildUpdateRootSymlinkChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::string childName,
  SymlinkUpdate update);

UpdateNestedSymlinkChildTransaction buildUpdateNestedSymlinkChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  std::string childName,
  SymlinkUpdate update);

DirectoryPathTransaction buildUpdateNestedDirectoryMetadataTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> directoryPath,
  DirectoryMetadata directoryMetadata);

RemoveRootChildTransaction buildRemoveRootChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  DirectoryChildReference child,
  DirectoryMetadata rootDirectoryMetadata);

RemoveNestedChildTransaction buildRemoveNestedChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  DirectoryChildReference child,
  DirectoryMetadata parentDirectoryMetadata);

MoveChildTransaction buildMoveChildTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> sourceParentPath,
  DirectoryChildReference sourceChild,
  std::vector<DirectoryPathSegment> targetParentPath,
  std::string targetChildName,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata);

MoveChildTransaction buildMoveChildReplacingTransaction(
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> sourceParentPath,
  DirectoryChildReference sourceChild,
  std::vector<DirectoryPathSegment> targetParentPath,
  DirectoryChildReference targetChild,
  std::string targetChildName,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata);

RootOpenWithValidatedTreeResult publishRootDirectoryTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  DirectoryMetadata rootDirectoryMetadata,
  std::vector<DirectoryEntry> rootDirectoryEntries);

RootOpenWithValidatedTreeResult publishCreateRootDirectoryChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childDirectoryId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata);

RootOpenWithValidatedTreeResult publishCreateRootFileChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childFileId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  FileMetadata childFileMetadata);

RootOpenWithValidatedTreeResult publishCreateRootFileWithDataChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childFileId,
  ObjectId childFileDataId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload);

RootOpenWithValidatedTreeResult publishCreateRootSymlinkChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  ObjectId childSymlinkId,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget);

RootOpenWithValidatedTreeResult publishCreateNestedDirectoryChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childDirectoryId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata);

RootOpenWithValidatedTreeResult publishCreateNestedFileChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childFileId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata);

RootOpenWithValidatedTreeResult publishCreateNestedFileWithDataChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childFileId,
  ObjectId childFileDataId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload);

RootOpenWithValidatedTreeResult publishCreateNestedSymlinkChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  ObjectId childSymlinkId,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget);

RootOpenWithValidatedTreeResult publishUpdateRootFileChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::string childName,
  FileUpdate update);

RootOpenWithValidatedTreeResult publishUpdateNestedFileChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  std::string childName,
  FileUpdate update);

RootOpenWithValidatedTreeResult publishUpdateRootSymlinkChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::string childName,
  SymlinkUpdate update);

RootOpenWithValidatedTreeResult publishUpdateNestedSymlinkChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  std::string childName,
  SymlinkUpdate update);

RootOpenWithValidatedTreeResult publishUpdateNestedDirectoryMetadataTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> directoryPath,
  DirectoryMetadata directoryMetadata);

RootOpenWithValidatedTreeResult publishRemoveRootChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  DirectoryChildReference child,
  DirectoryMetadata rootDirectoryMetadata);

RootOpenWithValidatedTreeResult publishRemoveNestedChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> parentPath,
  DirectoryChildReference child,
  DirectoryMetadata parentDirectoryMetadata);

RootOpenWithValidatedTreeResult publishMoveChildTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> sourceParentPath,
  DirectoryChildReference sourceChild,
  std::vector<DirectoryPathSegment> targetParentPath,
  std::string targetChildName,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata);

RootOpenWithValidatedTreeResult publishMoveChildReplacingTransaction(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  const RootOpenWithValidatedTreeResult &baseRoot,
  RootId nextRootId,
  std::vector<DirectoryPathSegment> sourceParentPath,
  DirectoryChildReference sourceChild,
  std::vector<DirectoryPathSegment> targetParentPath,
  DirectoryChildReference targetChild,
  std::string targetChildName,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata);

}
}

#endif
