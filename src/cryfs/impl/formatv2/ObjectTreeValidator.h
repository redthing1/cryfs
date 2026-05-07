#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_OBJECTTREEVALIDATOR_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_OBJECTTREEVALIDATOR_H_

#include "DirectoryRecordStore.h"
#include "FileRecordStore.h"
#include "SymlinkRecordStore.h"

#include <cstdint>
#include <string>

namespace cryfs {
namespace formatv2 {

enum class ObjectTreeValidationStatus {
  Valid,
  DirectoryCycle,
  MissingDirectory,
  MissingFile,
  MissingSymlink,
  MissingFileData,
  FileDataSizeMismatch,
};

enum class ObjectTreeRecordType {
  Directory,
  File,
  Symlink,
  FileData,
};

struct ObjectTreeValidationResult final {
  ObjectTreeValidationStatus status;
  std::string path;
  ObjectTreeRecordType expectedType;
  ObjectId objectId;
  uint64_t generation;
};

ObjectTreeValidationResult validateObjectTree(
  const DirectoryRecord &rootDirectory,
  const DirectoryRecordStore &directoryRecordStore,
  const FileRecordStore &fileRecordStore,
  const SymlinkRecordStore &symlinkRecordStore);

}
}

#endif
