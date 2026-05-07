#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ROOTCONTENT_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ROOTCONTENT_H_

#include "RootTypes.h"

#include <boost/optional.hpp>
#include <cpp-utils/data/Data.h>

#include <cstdint>

namespace cryfs {
namespace formatv2 {

struct RootContent final {
  FilesystemId filesystemId;
  uint64_t epoch;
  RootId rootId;
  ObjectId rootDirectoryId;
  uint64_t rootDirectoryGeneration;
};

struct RootDirectoryRef final {
  ObjectId objectId;
  uint64_t generation;
};

bool operator==(const RootContent &lhs, const RootContent &rhs);
bool operator!=(const RootContent &lhs, const RootContent &rhs);
bool operator==(const RootDirectoryRef &lhs, const RootDirectoryRef &rhs);
bool operator!=(const RootDirectoryRef &lhs, const RootDirectoryRef &rhs);

cpputils::Data serializeRootContent(const RootContent &content);
boost::optional<RootContent> deserializeRootContent(const cpputils::Data &data);

// The caller must authenticate the serialized content before trusting this
// binding. This function only checks that the root content matches the already
// accepted root identity.
boost::optional<RootDirectoryRef> trustedRootDirectoryFromContent(
  const RootContent &content,
  const FilesystemId &expectedFilesystemId,
  const AuthenticatedRoot &expectedRoot);

}
}

#endif
