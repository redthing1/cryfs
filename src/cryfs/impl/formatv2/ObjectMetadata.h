#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_OBJECTMETADATA_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_OBJECTMETADATA_H_

#include <cstddef>
#include <cstdint>

namespace cpputils {
class Deserializer;
class Serializer;
}

namespace cryfs {
namespace formatv2 {

struct Timestamp final {
  int64_t seconds;
  uint32_t nanoseconds;
};

struct ObjectMetadata final {
  uint32_t permissions;
  uint32_t uid;
  uint32_t gid;
  Timestamp atime;
  Timestamp mtime;
  Timestamp ctime;
};

bool operator==(const Timestamp &lhs, const Timestamp &rhs);
bool operator!=(const Timestamp &lhs, const Timestamp &rhs);
bool operator==(const ObjectMetadata &lhs, const ObjectMetadata &rhs);
bool operator!=(const ObjectMetadata &lhs, const ObjectMetadata &rhs);

bool isValidObjectMetadata(const ObjectMetadata &metadata);
size_t serializedObjectMetadataSize();
void writeObjectMetadata(cpputils::Serializer *serializer, const ObjectMetadata &metadata);
ObjectMetadata readObjectMetadata(cpputils::Deserializer *deserializer);

}
}

#endif
