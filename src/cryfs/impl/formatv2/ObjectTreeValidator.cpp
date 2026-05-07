#include "ObjectTreeValidator.h"

#include <boost/optional.hpp>

#include <set>
#include <string>
#include <utility>

namespace cryfs {
namespace formatv2 {
namespace {

using DirectoryKey = std::string;

ObjectTreeValidationResult validResult() {
  return ObjectTreeValidationResult{
    ObjectTreeValidationStatus::Valid,
    "/",
    ObjectTreeRecordType::Directory,
    ObjectId::Null(),
    0
  };
}

ObjectTreeValidationResult invalidResult(
  ObjectTreeValidationStatus status,
  const std::string &path,
  ObjectTreeRecordType expectedType,
  const ObjectId &objectId,
  uint64_t generation) {
  return ObjectTreeValidationResult{
    status,
    path,
    expectedType,
    objectId,
    generation
  };
}

DirectoryKey directoryKey(const ObjectId &directoryId, uint64_t generation) {
  return directoryId.ToString() + "." + std::to_string(generation);
}

std::string childPath(const std::string &parentPath, const std::string &name) {
  if (parentPath == "/") {
    return "/" + name;
  }
  return parentPath + "/" + name;
}

ObjectTreeValidationResult validateFile(
  const FileRecord &file,
  const std::string &path,
  const FileRecordStore &fileRecordStore) {
  for (const FileExtent &extent: file.extents) {
    const boost::optional<FileDataRecord> data = fileRecordStore.loadData(extent.dataId, extent.generation);
    if (data == boost::none) {
      return invalidResult(
        ObjectTreeValidationStatus::MissingFileData,
        path,
        ObjectTreeRecordType::FileData,
        extent.dataId,
        extent.generation);
    }
    if (data->payload.size() != extent.size) {
      return invalidResult(
        ObjectTreeValidationStatus::FileDataSizeMismatch,
        path,
        ObjectTreeRecordType::FileData,
        extent.dataId,
        extent.generation);
    }
  }
  return validResult();
}

ObjectTreeValidationResult validateDirectory(
  const DirectoryRecord &directory,
  const std::string &path,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore,
  std::set<DirectoryKey> *activeDirectories) {
  const DirectoryKey currentKey = directoryKey(directory.directoryId, directory.generation);
  if (activeDirectories->count(currentKey) != 0) {
    return invalidResult(
      ObjectTreeValidationStatus::DirectoryCycle,
      path,
      ObjectTreeRecordType::Directory,
      directory.directoryId,
      directory.generation);
  }
  activeDirectories->insert(currentKey);

  for (const DirectoryEntry &entry: directory.entries) {
    const std::string entryPath = childPath(path, entry.name);
    if (entry.type == ObjectType::Directory) {
      const DirectoryKey childKey = directoryKey(entry.objectId, entry.generation);
      if (activeDirectories->count(childKey) != 0) {
        return invalidResult(
          ObjectTreeValidationStatus::DirectoryCycle,
          entryPath,
          ObjectTreeRecordType::Directory,
          entry.objectId,
          entry.generation);
      }

      const boost::optional<DirectoryRecord> child =
        directoryRecordStore.load(entry.objectId, entry.generation);
      if (child == boost::none) {
        return invalidResult(
          ObjectTreeValidationStatus::MissingDirectory,
          entryPath,
          ObjectTreeRecordType::Directory,
          entry.objectId,
          entry.generation);
      }

      const ObjectTreeValidationResult result = validateDirectory(
        *child,
        entryPath,
        directoryRecordStore,
        fileRecordStore,
        symlinkRecordStore,
        activeDirectories);
      if (result.status != ObjectTreeValidationStatus::Valid) {
        return result;
      }
    } else if (entry.type == ObjectType::File) {
      const boost::optional<FileRecord> file = fileRecordStore.load(entry.objectId, entry.generation);
      if (file == boost::none) {
        return invalidResult(
          ObjectTreeValidationStatus::MissingFile,
          entryPath,
          ObjectTreeRecordType::File,
          entry.objectId,
          entry.generation);
      }

      const ObjectTreeValidationResult result = validateFile(*file, entryPath, fileRecordStore);
      if (result.status != ObjectTreeValidationStatus::Valid) {
        return result;
      }
    } else if (entry.type == ObjectType::Symlink) {
      const boost::optional<SymlinkRecord> symlink =
        symlinkRecordStore.load(entry.objectId, entry.generation);
      if (symlink == boost::none) {
        return invalidResult(
          ObjectTreeValidationStatus::MissingSymlink,
          entryPath,
          ObjectTreeRecordType::Symlink,
          entry.objectId,
          entry.generation);
      }
    }
  }

  activeDirectories->erase(currentKey);
  return validResult();
}

}

ObjectTreeValidationResult validateObjectTree(
  const DirectoryRecord &rootDirectory,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore) {
  std::set<DirectoryKey> activeDirectories;
  return validateDirectory(
    rootDirectory,
    "/",
    directoryRecordStore,
    fileRecordStore,
    symlinkRecordStore,
    &activeDirectories);
}

}
}
