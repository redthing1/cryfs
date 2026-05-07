#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_SYMLINKRECORD_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_SYMLINKRECORD_H_

#include "ObjectMetadata.h"
#include "RootTypes.h"

#include <boost/optional.hpp>
#include <cpp-utils/data/Data.h>

#include <cstdint>
#include <string>

namespace cryfs {
namespace formatv2 {

using SymlinkMetadata = ObjectMetadata;

struct SymlinkRecord final {
  FilesystemId filesystemId;
  ObjectId symlinkId;
  uint64_t generation;
  SymlinkMetadata metadata;
  std::string target;
};

bool operator==(const SymlinkRecord &lhs, const SymlinkRecord &rhs);
bool operator!=(const SymlinkRecord &lhs, const SymlinkRecord &rhs);

cpputils::Data serializeSymlinkRecord(const SymlinkRecord &record);
boost::optional<SymlinkRecord> deserializeSymlinkRecord(const cpputils::Data &data);

bool symlinkRecordMatchesObject(
  const SymlinkRecord &record,
  const FilesystemId &expectedFilesystemId,
  const ObjectId &expectedSymlinkId,
  uint64_t expectedGeneration);

}
}

#endif
