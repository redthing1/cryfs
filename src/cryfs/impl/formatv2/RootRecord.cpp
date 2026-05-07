#include "RootRecord.h"

#include <cpp-utils/data/Deserializer.h>
#include <cpp-utils/data/Serializer.h>

#include <stdexcept>
#include <string>

namespace cryfs {
namespace formatv2 {
namespace {

const std::string HEADER = "cryfs.formatv2.root;0";

bool isValidRootRecord(const RootRecord &record) {
  return record.epoch != 0
      && record.rootId != RootId::Null();
}

size_t serializedSize() {
  return cpputils::Serializer::StringSize(HEADER)
      + FilesystemId::BINARY_LENGTH
      + sizeof(uint64_t)
      + RootId::BINARY_LENGTH;
}

}

bool operator==(const RootRecord &lhs, const RootRecord &rhs) {
  return lhs.filesystemId == rhs.filesystemId
      && lhs.epoch == rhs.epoch
      && lhs.rootId == rhs.rootId;
}

bool operator!=(const RootRecord &lhs, const RootRecord &rhs) {
  return !(lhs == rhs);
}

cpputils::Data serializeRootRecord(const RootRecord &record) {
  if (!isValidRootRecord(record)) {
    throw std::runtime_error("Invalid format-v2 root record");
  }

  cpputils::Serializer serializer(serializedSize());
  serializer.writeString(HEADER);
  serializer.writeFixedSizeData<FilesystemId::BINARY_LENGTH>(record.filesystemId);
  serializer.writeUint64(record.epoch);
  serializer.writeFixedSizeData<RootId::BINARY_LENGTH>(record.rootId);
  return serializer.finished();
}

boost::optional<RootRecord> deserializeRootRecord(const cpputils::Data &data) {
  try {
    cpputils::Deserializer deserializer(&data);
    const std::string header = deserializer.readString();
    if (header != HEADER) {
      return boost::none;
    }

    RootRecord record{
      deserializer.readFixedSizeData<FilesystemId::BINARY_LENGTH>(),
      deserializer.readUint64(),
      deserializer.readFixedSizeData<RootId::BINARY_LENGTH>()
    };
    deserializer.finished();

    if (!isValidRootRecord(record)) {
      return boost::none;
    }
    return record;
  } catch (const std::exception&) {
    return boost::none;
  }
}

boost::optional<AuthenticatedRoot> trustedRootFromRecord(
  const RootRecord &record,
  const FilesystemId &expectedFilesystemId) {
  if (record.filesystemId != expectedFilesystemId) {
    return boost::none;
  }
  return AuthenticatedRoot{record.epoch, record.rootId};
}

}
}
