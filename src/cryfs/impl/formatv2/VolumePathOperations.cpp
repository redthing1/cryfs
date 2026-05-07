#include "VolumePathOperations.h"

#include "KeyDerivation.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bf = boost::filesystem;

namespace cryfs {
namespace formatv2 {
namespace {

constexpr uint64_t INITIAL_FILE_DATA_GENERATION = 1;

bool isValidPathComponent(const std::string &component) {
  return !component.empty()
      && component != "."
      && component != ".."
      && component.find('/') == std::string::npos
      && component.find('\0') == std::string::npos;
}

bool isRootChildPath(const VolumePath &path) {
  return path.parentDirectoryPath.empty();
}

bool isSameVolumePath(const VolumePath &lhs, const VolumePath &rhs) {
  return lhs.childName == rhs.childName
      && lhs.parentDirectoryPath == rhs.parentDirectoryPath;
}

std::vector<std::string> fullPathComponents(const VolumePath &path) {
  std::vector<std::string> result = path.parentDirectoryPath;
  result.push_back(path.childName);
  return result;
}

bool hasPathPrefix(
  const std::vector<std::string> &path,
  const std::vector<std::string> &prefix) {
  if (path.size() < prefix.size()) {
    return false;
  }
  return std::equal(prefix.begin(), prefix.end(), path.begin());
}

std::vector<std::string> splitAbsoluteVolumePathComponents(
  const bf::path &path,
  bool requireChildPath) {
  if (!path.has_root_directory() || path.has_root_name()) {
    throw std::runtime_error("Format-v2 volume paths must be absolute paths without a root name");
  }

  std::vector<std::string> components;
  for (const bf::path &component: path.relative_path()) {
    const std::string name = component.string();
    if (!isValidPathComponent(name)) {
      throw std::runtime_error("Format-v2 volume paths must not contain empty, dot, dot-dot, slash, or NUL components");
    }
    components.push_back(name);
  }

  if (requireChildPath && components.empty()) {
    throw std::runtime_error("Format-v2 volume path operation requires a child path");
  }
  return components;
}

const DirectoryEntry *findEntryByName(
  const std::vector<DirectoryEntry> &entries,
  const std::string &name) {
  for (const DirectoryEntry &entry: entries) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

VolumeNode directoryNode(DirectoryRecord directory) {
  return VolumeNode{
    ObjectType::Directory,
    directory.directoryId,
    directory.generation,
    std::move(directory),
    boost::none,
    boost::none
  };
}

VolumeNode fileNode(FileRecord file) {
  return VolumeNode{
    ObjectType::File,
    file.fileId,
    file.generation,
    boost::none,
    std::move(file),
    boost::none
  };
}

VolumeNode symlinkNode(SymlinkRecord symlink) {
  return VolumeNode{
    ObjectType::Symlink,
    symlink.symlinkId,
    symlink.generation,
    boost::none,
    boost::none,
    std::move(symlink)
  };
}

size_t checkedSize(uint64_t value, const char *description) {
  if (value > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    throw std::runtime_error(std::string("Format-v2 ") + description + " is too large for this platform");
  }
  return static_cast<size_t>(value);
}

cpputils::Data loadFileRangeFromStore(
  const FileRecordStore &fileRecordStore,
  const FileRecord &file,
  uint64_t offset,
  uint64_t size) {
  (void)serializeFileRecord(file);

  if (size == 0 || offset >= file.size) {
    return cpputils::Data(0);
  }

  const uint64_t readSize = std::min(size, file.size - offset);
  const uint64_t rangeEnd = offset + readSize;
  cpputils::Data contents(checkedSize(readSize, "file range size"));
  contents.FillWithZeroes();

  for (const FileExtent &extent: file.extents) {
    const uint64_t extentEnd = extent.offset + extent.size;
    if (extentEnd <= offset || extent.offset >= rangeEnd) {
      continue;
    }

    const boost::optional<FileDataRecord> data =
      fileRecordStore.loadData(extent.dataId, extent.generation);
    if (data == boost::none) {
      throw std::runtime_error("Cannot load format-v2 file contents with unavailable file data");
    }
    if (data->payload.size() != extent.size) {
      throw std::runtime_error("Cannot load format-v2 file contents with wrong-sized file data");
    }

    const uint64_t copyStart = std::max(offset, extent.offset);
    const uint64_t copyEnd = std::min(rangeEnd, extentEnd);
    const uint64_t sourceOffset = copyStart - extent.offset;
    const uint64_t targetOffset = copyStart - offset;
    const uint64_t copySize = copyEnd - copyStart;

    std::memcpy(
      static_cast<char*>(contents.data()) + checkedSize(targetOffset, "file range target offset"),
      static_cast<const char*>(data->payload.data()) + checkedSize(sourceOffset, "file range source offset"),
      checkedSize(copySize, "file range copy size"));
  }
  return contents;
}

ObjectId generateNonNullObjectId(
  cpputils::RandomGenerator *randomGenerator,
  const char *description) {
  if (randomGenerator == nullptr) {
    throw std::runtime_error(std::string("Missing random generator for format-v2 ") + description);
  }

  for (size_t attempt = 0; attempt != 16; ++attempt) {
    const ObjectId id = randomGenerator->getFixedSize<ObjectId::BINARY_LENGTH>();
    if (id != ObjectId::Null()) {
      return id;
    }
  }
  throw std::runtime_error(std::string("Failed to generate non-null format-v2 ") + description);
}

uint64_t checkedEndOffset(uint64_t offset, uint64_t size, const char *description) {
  if (size > std::numeric_limits<uint64_t>::max() - offset) {
    throw std::runtime_error(std::string("Format-v2 ") + description + " overflows the file offset range");
  }
  return offset + size;
}

void appendGeneratedDataExtent(
  FilesystemId filesystemId,
  cpputils::RandomGenerator *randomGenerator,
  uint64_t offset,
  cpputils::Data payload,
  std::vector<FileExtent> *extents,
  std::vector<FileDataRecord> *fileData) {
  if (payload.size() == 0) {
    return;
  }

  const ObjectId dataId = generateNonNullObjectId(randomGenerator, "file data id");
  const uint64_t size = static_cast<uint64_t>(payload.size());
  fileData->push_back(FileDataRecord{
    filesystemId,
    dataId,
    INITIAL_FILE_DATA_GENERATION,
    std::move(payload)
  });
  extents->push_back(FileExtent{
    offset,
    size,
    dataId,
    INITIAL_FILE_DATA_GENERATION
  });
}

cpputils::Data loadExtentSlice(
  const FileRecordStore &fileRecordStore,
  const FileExtent &extent,
  uint64_t offset,
  uint64_t size) {
  const uint64_t extentEnd = checkedEndOffset(extent.offset, extent.size, "file extent");
  if (offset < extent.offset || size == 0 || size > extentEnd - offset) {
    throw std::runtime_error("Cannot build a format-v2 file update with an invalid extent slice");
  }

  const boost::optional<FileDataRecord> data =
    fileRecordStore.loadData(extent.dataId, extent.generation);
  if (data == boost::none) {
    throw std::runtime_error("Cannot build a format-v2 file update with unavailable file data");
  }
  if (data->payload.size() != extent.size) {
    throw std::runtime_error("Cannot build a format-v2 file update with wrong-sized file data");
  }

  cpputils::Data slice(checkedSize(size, "file data slice size"));
  std::memcpy(
    slice.data(),
    static_cast<const char*>(data->payload.data()) + checkedSize(offset - extent.offset, "file data slice offset"),
    slice.size());
  return slice;
}

void appendGeneratedExtentSlice(
  const FileRecordStore &fileRecordStore,
  FilesystemId filesystemId,
  cpputils::RandomGenerator *randomGenerator,
  const FileExtent &extent,
  uint64_t offset,
  uint64_t size,
  std::vector<FileExtent> *extents,
  std::vector<FileDataRecord> *fileData) {
  appendGeneratedDataExtent(
    filesystemId,
    randomGenerator,
    offset,
    loadExtentSlice(fileRecordStore, extent, offset, size),
    extents,
    fileData);
}

void sortExtentsByOffset(std::vector<FileExtent> *extents) {
  std::sort(
    extents->begin(),
    extents->end(),
    [] (const FileExtent &lhs, const FileExtent &rhs) {
      return lhs.offset < rhs.offset;
    });
}

FileUpdate buildWriteFileUpdate(
  const FileRecordStore &fileRecordStore,
  FilesystemId filesystemId,
  cpputils::RandomGenerator *randomGenerator,
  const FileRecord &baseFile,
  FileMetadata fileMetadata,
  uint64_t offset,
  cpputils::Data payload) {
  (void)serializeFileRecord(baseFile);

  const uint64_t payloadSize = static_cast<uint64_t>(payload.size());
  const uint64_t writeEnd = checkedEndOffset(offset, payloadSize, "file write");
  const uint64_t nextFileSize = payloadSize == 0
      ? baseFile.size
      : std::max(baseFile.size, writeEnd);

  std::vector<FileExtent> extents;
  std::vector<FileDataRecord> fileData;

  for (const FileExtent &extent: baseFile.extents) {
    const uint64_t extentEnd = checkedEndOffset(extent.offset, extent.size, "file extent");
    if (payloadSize == 0 || extentEnd <= offset || extent.offset >= writeEnd) {
      extents.push_back(extent);
      continue;
    }

    if (extent.offset < offset) {
      appendGeneratedExtentSlice(
        fileRecordStore,
        filesystemId,
        randomGenerator,
        extent,
        extent.offset,
        offset - extent.offset,
        &extents,
        &fileData);
    }
    if (writeEnd < extentEnd) {
      appendGeneratedExtentSlice(
        fileRecordStore,
        filesystemId,
        randomGenerator,
        extent,
        writeEnd,
        extentEnd - writeEnd,
        &extents,
        &fileData);
    }
  }

  appendGeneratedDataExtent(
    filesystemId,
    randomGenerator,
    offset,
    std::move(payload),
    &extents,
    &fileData);
  sortExtentsByOffset(&extents);

  return FileUpdate{
    baseFile,
    std::move(fileMetadata),
    nextFileSize,
    std::move(extents),
    std::move(fileData)
  };
}

FileUpdate buildTruncateFileUpdate(
  const FileRecordStore &fileRecordStore,
  FilesystemId filesystemId,
  cpputils::RandomGenerator *randomGenerator,
  const FileRecord &baseFile,
  FileMetadata fileMetadata,
  uint64_t size) {
  (void)serializeFileRecord(baseFile);

  std::vector<FileExtent> extents;
  std::vector<FileDataRecord> fileData;

  for (const FileExtent &extent: baseFile.extents) {
    const uint64_t extentEnd = checkedEndOffset(extent.offset, extent.size, "file extent");
    if (extent.offset >= size) {
      continue;
    }
    if (extentEnd <= size) {
      extents.push_back(extent);
      continue;
    }

    appendGeneratedExtentSlice(
      fileRecordStore,
      filesystemId,
      randomGenerator,
      extent,
      extent.offset,
      size - extent.offset,
      &extents,
      &fileData);
  }
  sortExtentsByOffset(&extents);

  return FileUpdate{
    baseFile,
    std::move(fileMetadata),
    size,
    std::move(extents),
    std::move(fileData)
  };
}

FileUpdate buildMetadataOnlyFileUpdate(
  const FileRecord &baseFile,
  FileMetadata fileMetadata) {
  (void)serializeFileRecord(baseFile);

  return FileUpdate{
    baseFile,
    std::move(fileMetadata),
    baseFile.size,
    baseFile.extents,
    {}
  };
}

FileRecord fileFromNodeForMutation(const VolumeNode &node) {
  if (node.type != ObjectType::File || node.file == boost::none) {
    throw std::runtime_error("Cannot mutate format-v2 file contents from a non-file path");
  }
  return *node.file;
}

SymlinkRecord symlinkFromNodeForMutation(const VolumeNode &node) {
  if (node.type != ObjectType::Symlink || node.symlink == boost::none) {
    throw std::runtime_error("Cannot mutate format-v2 symlink metadata from a non-symlink path");
  }
  return *node.symlink;
}

DirectoryRecord directoryFromNodeForMutation(const VolumeNode &node) {
  if (node.type != ObjectType::Directory || node.directory == boost::none) {
    throw std::runtime_error("Cannot mutate format-v2 directory metadata from a non-directory path");
  }
  return *node.directory;
}

boost::optional<FileRecord> loadFileAtPathForMutation(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  const bf::path &path) {
  const boost::optional<VolumeNode> node =
    loadVolumeNodeAtPath(layout, filesystemId, masterKey, path);
  if (node == boost::none) {
    return boost::none;
  }
  return fileFromNodeForMutation(*node);
}

DirectoryChildReference childReferenceFromNode(
  const VolumePath &path,
  const VolumeNode &node) {
  return DirectoryChildReference{
    path.childName,
    node.type,
    node.objectId,
    node.generation
  };
}

void validateMoveTargetIsOutsideSourceSubtree(
  const VolumePath &sourcePath,
  const VolumePath &targetPath,
  const VolumeNode &sourceNode) {
  if (sourceNode.type != ObjectType::Directory) {
    return;
  }

  const std::vector<std::string> sourceComponents = fullPathComponents(sourcePath);
  if (hasPathPrefix(targetPath.parentDirectoryPath, sourceComponents)) {
    throw std::runtime_error("Cannot move format-v2 directory into itself or one of its descendants");
  }
}

void validateNodeTypeForRemoval(
  const VolumeNode &node,
  ObjectType expectedType,
  const char *description) {
  if (node.type != expectedType) {
    throw std::runtime_error(
      std::string("Cannot remove format-v2 ")
      + description
      + " from a path with a different object type");
  }
}

void validateEmptyDirectoryForRemoval(const VolumeNode &node) {
  validateNodeTypeForRemoval(node, ObjectType::Directory, "directory");
  if (node.directory == boost::none) {
    throw std::runtime_error("Cannot remove format-v2 directory without a loaded directory record");
  }
  if (!node.directory->entries.empty()) {
    throw std::runtime_error("Cannot remove non-empty format-v2 directory");
  }
}

void validateNodeCanReplaceTarget(
  const VolumeNode &sourceNode,
  const VolumeNode &targetNode) {
  const bool sourceIsDirectory = sourceNode.type == ObjectType::Directory;
  const bool targetIsDirectory = targetNode.type == ObjectType::Directory;
  if (sourceIsDirectory != targetIsDirectory) {
    throw std::runtime_error("Cannot replace format-v2 directory and non-directory paths with each other");
  }

  if (targetIsDirectory) {
    validateEmptyDirectoryForRemoval(targetNode);
  }
}

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeRemoveNodeAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  DirectoryMetadata parentDirectoryMetadata,
  ObjectType expectedType,
  const char *description) {
  const VolumePath volumePath = splitAbsoluteVolumePath(path);
  const boost::optional<VolumeNode> node =
    loadVolumeNodeAtPath(layout, filesystemId, masterKey, path);
  if (node == boost::none) {
    return boost::none;
  }

  if (expectedType == ObjectType::Directory) {
    validateEmptyDirectoryForRemoval(*node);
  } else {
    validateNodeTypeForRemoval(*node, expectedType, description);
  }

  DirectoryChildReference child = childReferenceFromNode(volumePath, *node);
  if (isRootChildPath(volumePath)) {
    return publishVolumeRemoveRootChildTransaction(
      layout,
      filesystemId,
      masterKey,
      randomGenerator,
      std::move(child),
      std::move(parentDirectoryMetadata));
  }

  return publishVolumeRemoveNestedChildTransaction(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    volumePath.parentDirectoryPath,
    std::move(child),
    std::move(parentDirectoryMetadata));
}

}

VolumePath splitAbsoluteVolumePath(const bf::path &path) {
  std::vector<std::string> components = splitAbsoluteVolumePathComponents(path, true);
  std::string childName = std::move(components.back());
  components.pop_back();
  return VolumePath{
    std::move(components),
    std::move(childName)
  };
}

boost::optional<VolumeNode> loadVolumeNodeAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  const bf::path &path) {
  const std::vector<std::string> components = splitAbsoluteVolumePathComponents(path, false);
  const RootOpenWithValidatedTreeResult root = openVolumeRoot(layout, filesystemId, masterKey);
  if (root.status != RootOpenStatus::Selected || root.rootDirectory == boost::none) {
    throw std::runtime_error("Cannot load a format-v2 volume path without a selected root");
  }

  if (components.empty()) {
    return directoryNode(*root.rootDirectory);
  }

  const cpputils::EncryptionKey objectEncryptionKey =
    deriveObjectEncryptionKey(masterKey, filesystemId);
  DirectoryRecordStore directoryRecordStore(
    layout.directoriesDirectory,
    filesystemId,
    objectEncryptionKey);
  FileRecordStore fileRecordStore(
    layout.filesDirectory,
    filesystemId,
    objectEncryptionKey);
  SymlinkRecordStore symlinkRecordStore(
    layout.symlinksDirectory,
    filesystemId,
    objectEncryptionKey);

  DirectoryRecord parent = *root.rootDirectory;
  for (size_t index = 0; index != components.size(); ++index) {
    const DirectoryEntry *entry = findEntryByName(parent.entries, components[index]);
    if (entry == nullptr) {
      return boost::none;
    }

    const bool isLeaf = index + 1 == components.size();
    if (isLeaf) {
      switch (entry->type) {
        case ObjectType::Directory: {
          const boost::optional<DirectoryRecord> directory =
            directoryRecordStore.load(entry->objectId, entry->generation);
          if (directory == boost::none) {
            throw std::runtime_error("Cannot load format-v2 path with an unavailable directory record");
          }
          return directoryNode(*directory);
        }
        case ObjectType::File: {
          const boost::optional<FileRecord> file =
            fileRecordStore.load(entry->objectId, entry->generation);
          if (file == boost::none) {
            throw std::runtime_error("Cannot load format-v2 path with an unavailable file record");
          }
          return fileNode(*file);
        }
        case ObjectType::Symlink: {
          const boost::optional<SymlinkRecord> symlink =
            symlinkRecordStore.load(entry->objectId, entry->generation);
          if (symlink == boost::none) {
            throw std::runtime_error("Cannot load format-v2 path with an unavailable symlink record");
          }
          return symlinkNode(*symlink);
        }
      }
      throw std::runtime_error("Cannot load format-v2 path with an unknown object type");
    }

    if (entry->type != ObjectType::Directory) {
      throw std::runtime_error("Cannot load format-v2 path through a non-directory component");
    }

    const boost::optional<DirectoryRecord> directory =
      directoryRecordStore.load(entry->objectId, entry->generation);
    if (directory == boost::none) {
      throw std::runtime_error("Cannot load format-v2 path with an unavailable directory record");
    }
    parent = *directory;
  }

  throw std::runtime_error("Cannot load format-v2 path from an unreachable lookup state");
}

cpputils::Data loadVolumeFileContents(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  const FileRecord &file) {
  return loadVolumeFileRange(layout, filesystemId, masterKey, file, 0, file.size);
}

cpputils::Data loadVolumeFileRange(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  const FileRecord &file,
  uint64_t offset,
  uint64_t size) {
  const cpputils::EncryptionKey objectEncryptionKey =
    deriveObjectEncryptionKey(masterKey, filesystemId);
  const FileRecordStore fileRecordStore(
    layout.filesDirectory,
    filesystemId,
    objectEncryptionKey);

  return loadFileRangeFromStore(fileRecordStore, file, offset, size);
}

boost::optional<cpputils::Data> loadVolumeFileContentsAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  const bf::path &path) {
  const boost::optional<VolumeNode> node =
    loadVolumeNodeAtPath(layout, filesystemId, masterKey, path);
  if (node == boost::none) {
    return boost::none;
  }
  if (node->type != ObjectType::File || node->file == boost::none) {
    throw std::runtime_error("Cannot load format-v2 file contents from a non-file path");
  }
  return loadVolumeFileContents(layout, filesystemId, masterKey, *node->file);
}

boost::optional<cpputils::Data> loadVolumeFileRangeAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  const bf::path &path,
  uint64_t offset,
  uint64_t size) {
  const boost::optional<VolumeNode> node =
    loadVolumeNodeAtPath(layout, filesystemId, masterKey, path);
  if (node == boost::none) {
    return boost::none;
  }
  if (node->type != ObjectType::File || node->file == boost::none) {
    throw std::runtime_error("Cannot load format-v2 file contents from a non-file path");
  }
  return loadVolumeFileRange(layout, filesystemId, masterKey, *node->file, offset, size);
}

RootOpenWithValidatedTreeResult publishVolumeCreateDirectoryAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  DirectoryMetadata parentDirectoryMetadata,
  DirectoryMetadata childDirectoryMetadata) {
  const VolumePath volumePath = splitAbsoluteVolumePath(path);
  if (isRootChildPath(volumePath)) {
    return publishVolumeCreateRootDirectoryChildTransaction(
      layout,
      filesystemId,
      masterKey,
      randomGenerator,
      volumePath.childName,
      std::move(parentDirectoryMetadata),
      std::move(childDirectoryMetadata));
  }

  return publishVolumeCreateNestedDirectoryChildTransaction(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    volumePath.parentDirectoryPath,
    volumePath.childName,
    std::move(parentDirectoryMetadata),
    std::move(childDirectoryMetadata));
}

RootOpenWithValidatedTreeResult publishVolumeCreateFileAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata) {
  const VolumePath volumePath = splitAbsoluteVolumePath(path);
  if (isRootChildPath(volumePath)) {
    return publishVolumeCreateRootFileChildTransaction(
      layout,
      filesystemId,
      masterKey,
      randomGenerator,
      volumePath.childName,
      std::move(parentDirectoryMetadata),
      std::move(childFileMetadata));
  }

  return publishVolumeCreateNestedFileChildTransaction(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    volumePath.parentDirectoryPath,
    volumePath.childName,
    std::move(parentDirectoryMetadata),
    std::move(childFileMetadata));
}

RootOpenWithValidatedTreeResult publishVolumeCreateFileWithDataAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  DirectoryMetadata parentDirectoryMetadata,
  FileMetadata childFileMetadata,
  cpputils::Data childFilePayload) {
  const VolumePath volumePath = splitAbsoluteVolumePath(path);
  if (isRootChildPath(volumePath)) {
    return publishVolumeCreateRootFileWithDataChildTransaction(
      layout,
      filesystemId,
      masterKey,
      randomGenerator,
      volumePath.childName,
      std::move(parentDirectoryMetadata),
      std::move(childFileMetadata),
      std::move(childFilePayload));
  }

  return publishVolumeCreateNestedFileWithDataChildTransaction(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    volumePath.parentDirectoryPath,
    volumePath.childName,
    std::move(parentDirectoryMetadata),
    std::move(childFileMetadata),
    std::move(childFilePayload));
}

RootOpenWithValidatedTreeResult publishVolumeCreateSymlinkAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  DirectoryMetadata parentDirectoryMetadata,
  SymlinkMetadata childSymlinkMetadata,
  std::string childSymlinkTarget) {
  const VolumePath volumePath = splitAbsoluteVolumePath(path);
  if (isRootChildPath(volumePath)) {
    return publishVolumeCreateRootSymlinkChildTransaction(
      layout,
      filesystemId,
      masterKey,
      randomGenerator,
      volumePath.childName,
      std::move(parentDirectoryMetadata),
      std::move(childSymlinkMetadata),
      std::move(childSymlinkTarget));
  }

  return publishVolumeCreateNestedSymlinkChildTransaction(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    volumePath.parentDirectoryPath,
    volumePath.childName,
    std::move(parentDirectoryMetadata),
    std::move(childSymlinkMetadata),
    std::move(childSymlinkTarget));
}

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeWriteFileAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  FileMetadata fileMetadata,
  uint64_t offset,
  cpputils::Data payload) {
  const VolumePath volumePath = splitAbsoluteVolumePath(path);
  const boost::optional<FileRecord> baseFile =
    loadFileAtPathForMutation(layout, filesystemId, masterKey, path);
  if (baseFile == boost::none) {
    return boost::none;
  }

  const cpputils::EncryptionKey objectEncryptionKey =
    deriveObjectEncryptionKey(masterKey, filesystemId);
  const FileRecordStore fileRecordStore(
    layout.filesDirectory,
    filesystemId,
    objectEncryptionKey);
  FileUpdate update = buildWriteFileUpdate(
    fileRecordStore,
    filesystemId,
    randomGenerator,
    *baseFile,
    std::move(fileMetadata),
    offset,
    std::move(payload));

  if (isRootChildPath(volumePath)) {
    return publishVolumeUpdateRootFileChildTransaction(
      layout,
      filesystemId,
      masterKey,
      randomGenerator,
      volumePath.childName,
      std::move(update));
  }

  return publishVolumeUpdateNestedFileChildTransaction(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    volumePath.parentDirectoryPath,
    volumePath.childName,
    std::move(update));
}

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeTruncateFileAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  FileMetadata fileMetadata,
  uint64_t size) {
  const VolumePath volumePath = splitAbsoluteVolumePath(path);
  const boost::optional<FileRecord> baseFile =
    loadFileAtPathForMutation(layout, filesystemId, masterKey, path);
  if (baseFile == boost::none) {
    return boost::none;
  }

  const cpputils::EncryptionKey objectEncryptionKey =
    deriveObjectEncryptionKey(masterKey, filesystemId);
  const FileRecordStore fileRecordStore(
    layout.filesDirectory,
    filesystemId,
    objectEncryptionKey);
  FileUpdate update = buildTruncateFileUpdate(
    fileRecordStore,
    filesystemId,
    randomGenerator,
    *baseFile,
    std::move(fileMetadata),
    size);

  if (isRootChildPath(volumePath)) {
    return publishVolumeUpdateRootFileChildTransaction(
      layout,
      filesystemId,
      masterKey,
      randomGenerator,
      volumePath.childName,
      std::move(update));
  }

  return publishVolumeUpdateNestedFileChildTransaction(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    volumePath.parentDirectoryPath,
    volumePath.childName,
    std::move(update));
}

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeUpdateFileMetadataAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  FileMetadata fileMetadata) {
  const VolumePath volumePath = splitAbsoluteVolumePath(path);
  const boost::optional<FileRecord> baseFile =
    loadFileAtPathForMutation(layout, filesystemId, masterKey, path);
  if (baseFile == boost::none) {
    return boost::none;
  }

  FileUpdate update = buildMetadataOnlyFileUpdate(
    *baseFile,
    std::move(fileMetadata));
  if (isRootChildPath(volumePath)) {
    return publishVolumeUpdateRootFileChildTransaction(
      layout,
      filesystemId,
      masterKey,
      randomGenerator,
      volumePath.childName,
      std::move(update));
  }

  return publishVolumeUpdateNestedFileChildTransaction(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    volumePath.parentDirectoryPath,
    volumePath.childName,
    std::move(update));
}

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeUpdateSymlinkMetadataAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  SymlinkMetadata symlinkMetadata) {
  const VolumePath volumePath = splitAbsoluteVolumePath(path);
  const boost::optional<VolumeNode> node =
    loadVolumeNodeAtPath(layout, filesystemId, masterKey, path);
  if (node == boost::none) {
    return boost::none;
  }

  const SymlinkRecord baseSymlink = symlinkFromNodeForMutation(*node);
  SymlinkUpdate update{
    baseSymlink,
    std::move(symlinkMetadata),
    baseSymlink.target
  };
  if (isRootChildPath(volumePath)) {
    return publishVolumeUpdateRootSymlinkChildTransaction(
      layout,
      filesystemId,
      masterKey,
      randomGenerator,
      volumePath.childName,
      std::move(update));
  }

  return publishVolumeUpdateNestedSymlinkChildTransaction(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    volumePath.parentDirectoryPath,
    volumePath.childName,
    std::move(update));
}

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeUpdateDirectoryMetadataAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  DirectoryMetadata directoryMetadata) {
  const std::vector<std::string> directoryPath =
    splitAbsoluteVolumePathComponents(path, false);
  const boost::optional<VolumeNode> node =
    loadVolumeNodeAtPath(layout, filesystemId, masterKey, path);
  if (node == boost::none) {
    return boost::none;
  }
  (void)directoryFromNodeForMutation(*node);

  if (directoryPath.empty()) {
    return publishVolumeUpdateRootDirectoryMetadataTransaction(
      layout,
      filesystemId,
      masterKey,
      randomGenerator,
      std::move(directoryMetadata));
  }

  return publishVolumeUpdateNestedDirectoryMetadataTransaction(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    directoryPath,
    std::move(directoryMetadata));
}

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeRemoveFileAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  DirectoryMetadata parentDirectoryMetadata) {
  return publishVolumeRemoveNodeAtPath(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    path,
    std::move(parentDirectoryMetadata),
    ObjectType::File,
    "file");
}

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeRemoveSymlinkAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  DirectoryMetadata parentDirectoryMetadata) {
  return publishVolumeRemoveNodeAtPath(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    path,
    std::move(parentDirectoryMetadata),
    ObjectType::Symlink,
    "symlink");
}

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeRemoveEmptyDirectoryAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &path,
  DirectoryMetadata parentDirectoryMetadata) {
  return publishVolumeRemoveNodeAtPath(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    path,
    std::move(parentDirectoryMetadata),
    ObjectType::Directory,
    "directory");
}

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeMoveNodeAtPath(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &sourcePath,
  const bf::path &targetPath,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata) {
  const VolumePath sourceVolumePath = splitAbsoluteVolumePath(sourcePath);
  const VolumePath targetVolumePath = splitAbsoluteVolumePath(targetPath);
  const boost::optional<VolumeNode> sourceNode =
    loadVolumeNodeAtPath(layout, filesystemId, masterKey, sourcePath);
  if (sourceNode == boost::none) {
    return boost::none;
  }

  if (isSameVolumePath(sourceVolumePath, targetVolumePath)) {
    return openVolumeRoot(layout, filesystemId, masterKey);
  }

  validateMoveTargetIsOutsideSourceSubtree(
    sourceVolumePath,
    targetVolumePath,
    *sourceNode);

  const boost::optional<VolumeNode> targetNode =
    loadVolumeNodeAtPath(layout, filesystemId, masterKey, targetPath);
  if (targetNode != boost::none) {
    throw std::runtime_error("Cannot perform a non-overwriting format-v2 move onto an existing path");
  }

  return publishVolumeMoveChildTransaction(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    sourceVolumePath.parentDirectoryPath,
    childReferenceFromNode(sourceVolumePath, *sourceNode),
    targetVolumePath.parentDirectoryPath,
    targetVolumePath.childName,
    std::move(sourceParentDirectoryMetadata),
    std::move(targetParentDirectoryMetadata));
}

boost::optional<RootOpenWithValidatedTreeResult> publishVolumeMoveNodeAtPathReplacingTarget(
  const VolumeLayout &layout,
  FilesystemId filesystemId,
  const cpputils::EncryptionKey &masterKey,
  cpputils::RandomGenerator *randomGenerator,
  const bf::path &sourcePath,
  const bf::path &targetPath,
  DirectoryMetadata sourceParentDirectoryMetadata,
  DirectoryMetadata targetParentDirectoryMetadata) {
  const VolumePath sourceVolumePath = splitAbsoluteVolumePath(sourcePath);
  const VolumePath targetVolumePath = splitAbsoluteVolumePath(targetPath);
  const boost::optional<VolumeNode> sourceNode =
    loadVolumeNodeAtPath(layout, filesystemId, masterKey, sourcePath);
  if (sourceNode == boost::none) {
    return boost::none;
  }

  if (isSameVolumePath(sourceVolumePath, targetVolumePath)) {
    return openVolumeRoot(layout, filesystemId, masterKey);
  }

  validateMoveTargetIsOutsideSourceSubtree(
    sourceVolumePath,
    targetVolumePath,
    *sourceNode);

  const boost::optional<VolumeNode> targetNode =
    loadVolumeNodeAtPath(layout, filesystemId, masterKey, targetPath);
  if (targetNode == boost::none) {
    return publishVolumeMoveChildTransaction(
      layout,
      filesystemId,
      masterKey,
      randomGenerator,
      sourceVolumePath.parentDirectoryPath,
      childReferenceFromNode(sourceVolumePath, *sourceNode),
      targetVolumePath.parentDirectoryPath,
      targetVolumePath.childName,
      std::move(sourceParentDirectoryMetadata),
      std::move(targetParentDirectoryMetadata));
  }

  validateNodeCanReplaceTarget(*sourceNode, *targetNode);
  return publishVolumeMoveChildReplacingTransaction(
    layout,
    filesystemId,
    masterKey,
    randomGenerator,
    sourceVolumePath.parentDirectoryPath,
    childReferenceFromNode(sourceVolumePath, *sourceNode),
    targetVolumePath.parentDirectoryPath,
    childReferenceFromNode(targetVolumePath, *targetNode),
    targetVolumePath.childName,
    std::move(sourceParentDirectoryMetadata),
    std::move(targetParentDirectoryMetadata));
}

}
}
