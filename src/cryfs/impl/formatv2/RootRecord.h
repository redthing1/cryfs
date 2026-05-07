#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ROOTRECORD_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ROOTRECORD_H_

#include "RootTypes.h"

#include <boost/optional.hpp>
#include <cpp-utils/data/Data.h>

#include <cstdint>

namespace cryfs {
namespace formatv2 {

struct RootRecord final {
  FilesystemId filesystemId;
  uint64_t epoch;
  RootId rootId;
};

bool operator==(const RootRecord &lhs, const RootRecord &rhs);
bool operator!=(const RootRecord &lhs, const RootRecord &rhs);

cpputils::Data serializeRootRecord(const RootRecord &record);
boost::optional<RootRecord> deserializeRootRecord(const cpputils::Data &data);

// The caller must authenticate the serialized record before treating this as a
// selectable root. This function only checks filesystem binding.
boost::optional<AuthenticatedRoot> trustedRootFromRecord(
  const RootRecord &record,
  const FilesystemId &expectedFilesystemId);

}
}

#endif
