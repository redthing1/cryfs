#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_VOLUME_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_VOLUME_H_

#include "RootTransaction.h"

#include <boost/filesystem/path.hpp>
#include <cpp-utils/crypto/symmetric/EncryptionKey.h>
#include <cpp-utils/data/Data.h>
#include <cpp-utils/random/RandomGenerator.h>

#include <string>
#include <vector>

namespace cryfs {
namespace formatv2 {

struct VolumeLayout final {
  boost::filesystem::path rootsDirectory;
  boost::filesystem::path rootContentDirectory;
  boost::filesystem::path directoriesDirectory;
  boost::filesystem::path filesDirectory;
  boost::filesystem::path symlinksDirectory;
  boost::filesystem::path acceptedRootFile;
};

VolumeLayout volumeLayout(
  const boost::filesystem::path &baseDir,
  const boost::filesystem::path &localStateFilesystemDir);

DirectoryMetadata initialRootDirectoryMetadata();

RootOpenWithValidatedTreeResult openVolumeRoot(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey);

RootOpenWithValidatedTreeResult createOrOpenInitialEmptyVolume(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  DirectoryMetadata rootDirectoryMetadata);

RootOpenWithValidatedTreeResult publishVolumeRootDirectoryTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  DirectoryMetadata rootDirectoryMetadata,
  std::vector<DirectoryEntry> rootDirectoryEntries);

RootOpenWithValidatedTreeResult publishVolumeCreateRootDirectoryChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata);

RootOpenWithValidatedTreeResult publishVolumeCreateRootFileChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  FileMetadata childFileMetadata);

RootOpenWithValidatedTreeResult publishVolumeCreateRootFileWithDataChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload);

RootOpenWithValidatedTreeResult publishVolumeCreateRootSymlinkChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::string childName,
  DirectoryMetadata rootDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget);

RootOpenWithValidatedTreeResult publishVolumeCreateNestedDirectoryChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata);

RootOpenWithValidatedTreeResult publishVolumeCreateNestedFileChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata);

RootOpenWithValidatedTreeResult publishVolumeCreateNestedFileWithDataChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload);

RootOpenWithValidatedTreeResult publishVolumeCreateNestedSymlinkChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  std::string childName,
  DirectoryMetadata parentDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget);

RootOpenWithValidatedTreeResult publishVolumeUpdateRootFileChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::string childName,
  FileUpdate update);

RootOpenWithValidatedTreeResult publishVolumeUpdateNestedFileChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  std::string childName,
  FileUpdate update);

RootOpenWithValidatedTreeResult publishVolumeUpdateRootSymlinkChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::string childName,
  SymlinkUpdate update);

RootOpenWithValidatedTreeResult publishVolumeUpdateNestedSymlinkChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  std::string childName,
  SymlinkUpdate update);

RootOpenWithValidatedTreeResult publishVolumeUpdateRootDirectoryMetadataTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  DirectoryMetadata rootDirectoryMetadata);

RootOpenWithValidatedTreeResult publishVolumeUpdateNestedDirectoryMetadataTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> directoryPath,
  DirectoryMetadata directoryMetadata);

RootOpenWithValidatedTreeResult publishVolumeRemoveRootChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  DirectoryChildReference child,
  DirectoryMetadata rootDirectoryMetadata);

RootOpenWithValidatedTreeResult publishVolumeRemoveNestedChildTransaction(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  std::vector<std::string> parentDirectoryPath,
  DirectoryChildReference child,
  DirectoryMetadata parentDirectoryMetadata);

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
  DirectoryMetadata targetParentDirectoryMetadata);

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
  DirectoryMetadata targetParentDirectoryMetadata);

}
}

#endif
