#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ROOTTYPES_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ROOTTYPES_H_

#include <cpp-utils/data/FixedSizeData.h>

#include <cstdint>

namespace cryfs {
namespace formatv2 {

using FilesystemId = cpputils::FixedSizeData<16>;
using RootId = cpputils::FixedSizeData<32>;
using ObjectId = cpputils::FixedSizeData<32>;

enum class ObjectType : uint8_t {
  File = 1,
  Directory = 2,
  Symlink = 3,
};

struct AuthenticatedRoot final {
  uint64_t epoch;
  RootId rootId;
};

using AcceptedRoot = AuthenticatedRoot;

bool operator==(const AuthenticatedRoot &lhs, const AuthenticatedRoot &rhs);
bool operator!=(const AuthenticatedRoot &lhs, const AuthenticatedRoot &rhs);

}
}

#endif
