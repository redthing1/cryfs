#include "ObjectMetadata.h"

#include <cpp-utils/data/Deserializer.h>
#include <cpp-utils/data/Serializer.h>

namespace cryfs {
namespace formatv2 {
namespace {

constexpr uint32_t MAX_PERMISSIONS = 07777;
constexpr uint32_t NANOS_PER_SECOND = 1000000000;

size_t serializedTimestampSize() {
  return sizeof(int64_t) + sizeof(uint32_t);
}

bool isValidTimestamp(const Timestamp &timestamp) {
  return timestamp.nanoseconds < NANOS_PER_SECOND;
}

void writeTimestamp(cpputils::Serializer *serializer, const Timestamp &timestamp) {
  serializer->writeInt64(timestamp.seconds);
  serializer->writeUint32(timestamp.nanoseconds);
}

Timestamp readTimestamp(cpputils::Deserializer *deserializer) {
  return Timestamp{
    deserializer->readInt64(),
    deserializer->readUint32()
  };
}

}

bool operator==(const Timestamp &lhs, const Timestamp &rhs) {
  return lhs.seconds == rhs.seconds
      && lhs.nanoseconds == rhs.nanoseconds;
}

bool operator!=(const Timestamp &lhs, const Timestamp &rhs) {
  return !(lhs == rhs);
}

bool operator==(const ObjectMetadata &lhs, const ObjectMetadata &rhs) {
  return lhs.permissions == rhs.permissions
      && lhs.uid == rhs.uid
      && lhs.gid == rhs.gid
      && lhs.atime == rhs.atime
      && lhs.mtime == rhs.mtime
      && lhs.ctime == rhs.ctime;
}

bool operator!=(const ObjectMetadata &lhs, const ObjectMetadata &rhs) {
  return !(lhs == rhs);
}

bool isValidObjectMetadata(const ObjectMetadata &metadata) {
  return metadata.permissions <= MAX_PERMISSIONS
      && isValidTimestamp(metadata.atime)
      && isValidTimestamp(metadata.mtime)
      && isValidTimestamp(metadata.ctime);
}

size_t serializedObjectMetadataSize() {
  return sizeof(uint32_t)
      + sizeof(uint32_t)
      + sizeof(uint32_t)
      + 3 * serializedTimestampSize();
}

void writeObjectMetadata(cpputils::Serializer *serializer, const ObjectMetadata &metadata) {
  serializer->writeUint32(metadata.permissions);
  serializer->writeUint32(metadata.uid);
  serializer->writeUint32(metadata.gid);
  writeTimestamp(serializer, metadata.atime);
  writeTimestamp(serializer, metadata.mtime);
  writeTimestamp(serializer, metadata.ctime);
}

ObjectMetadata readObjectMetadata(cpputils::Deserializer *deserializer) {
  return ObjectMetadata{
    deserializer->readUint32(),
    deserializer->readUint32(),
    deserializer->readUint32(),
    readTimestamp(deserializer),
    readTimestamp(deserializer),
    readTimestamp(deserializer)
  };
}

}
}
