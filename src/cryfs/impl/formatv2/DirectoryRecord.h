#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_DIRECTORYRECORD_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_DIRECTORYRECORD_H_

#include "ObjectMetadata.h"
#include "RootTypes.h"

#include <boost/optional.hpp>
#include <cpp-utils/data/Data.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cryfs {
namespace formatv2 {

using DirectoryMetadata = ObjectMetadata;

struct DirectoryEntry final {
  std::string name;
  ObjectType type;
  ObjectId objectId;
  uint64_t generation;
};

struct DirectoryRecord final {
  FilesystemId filesystemId;
  ObjectId directoryId;
  uint64_t generation;
  DirectoryMetadata metadata;
  std::vector<DirectoryEntry> entries;
};

bool operator==(const DirectoryEntry &lhs, const DirectoryEntry &rhs);
bool operator!=(const DirectoryEntry &lhs, const DirectoryEntry &rhs);
bool operator==(const DirectoryRecord &lhs, const DirectoryRecord &rhs);
bool operator!=(const DirectoryRecord &lhs, const DirectoryRecord &rhs);

cpputils::Data serializeDirectoryRecord(const DirectoryRecord &record);
boost::optional<DirectoryRecord> deserializeDirectoryRecord(const cpputils::Data &data);

// The caller must authenticate the serialized record before trusting this
// binding. This function only checks the directory record against the expected
// object identity.
bool directoryRecordMatchesObject(
  const DirectoryRecord &record,
  const FilesystemId &expectedFilesystemId,
  const ObjectId &expectedDirectoryId,
  uint64_t expectedGeneration);

}
}

#endif
