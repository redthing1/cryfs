#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_VOLUMEPATHOPERATIONS_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_VOLUMEPATHOPERATIONS_H_

#include "Volume.h"

#include <boost/optional.hpp>
#include <boost/filesystem/path.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace cryfs {
namespace formatv2 {

struct VolumePath final {
  std::vector<std::string> parentDirectoryPath;
  std::string childName;
};

struct VolumeNode final {
  ObjectType type;
  ObjectId objectId;
  uint64_t generation;
  boost::optional<DirectoryRecord> directory;
  boost::optional<FileRecord> file;
  boost::optional<SymlinkRecord> symlink;
};

VolumePath splitAbsoluteVolumePath(const boost::filesystem::path &path);

boost::optional<VolumeNode> loadVolumeNodeAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  const boost::filesystem::path &path);

cpputils::Data loadVolumeFileContents(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  const FileRecord &file);

cpputils::Data loadVolumeFileRange(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  const FileRecord &file,
  uint64_t offset,
  uint64_t size);

boost::optional<cpputils::Data> loadVolumeFileContentsAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  const boost::filesystem::path &path);

boost::optional<cpputils::Data> loadVolumeFileRangeAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  const boost::filesystem::path &path,
  uint64_t offset,
  uint64_t size);

RootOpenWithValidatedTreeResult publishVolumeCreateDirectoryAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &path,
  DirectoryMetadata parentDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata);

RootOpenWithValidatedTreeResult publishVolumeCreateFileAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &path,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata);

RootOpenWithValidatedTreeResult publishVolumeCreateFileWithDataAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &path,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload);

RootOpenWithValidatedTreeResult publishVolumeCreateSymlinkAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &path,
  DirectoryMetadata parentDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget);

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeWriteFileAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &path,
  FileMetadata fileMetadata,
  uint64_t offset,
  cpputils::Data payload);

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeTruncateFileAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &path,
  FileMetadata fileMetadata,
  uint64_t size);

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeUpdateFileMetadataAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &path,
  FileMetadata fileMetadata);

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeUpdateSymlinkMetadataAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &path,
  SymlinkMetadata symlinkMetadata);

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeUpdateDirectoryMetadataAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &path,
  DirectoryMetadata directoryMetadata);

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeRemoveFileAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &path,
  DirectoryMetadata parentDirectoryMetadata);

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeRemoveSymlinkAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &path,
  DirectoryMetadata parentDirectoryMetadata);

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeRemoveEmptyDirectoryAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &path,
  DirectoryMetadata parentDirectoryMetadata);

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeMoveNodeAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &sourcePath,
  const boost::filesystem::path &targetPath,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata);

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeMoveNodeAtPathReplacingTarget(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const boost::filesystem::path &sourcePath,
  const boost::filesystem::path &targetPath,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata);

}
}

#endif
