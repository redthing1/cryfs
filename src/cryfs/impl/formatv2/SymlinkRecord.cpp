#include "SymlinkRecord.h"

#include <cpp-utils/data/Deserializer.h>
#include <cpp-utils/data/Serializer.h>

#include <stdexcept>
#include <string>

namespace cryfs {
namespace formatv2 {
namespace {

const std::string HEADER = "cryfs.formatv2.symlink;0";

bool isValidTarget(const std::string &target) {
  return !target.empty()
      && target.find('\0') == std::string::npos;
}

bool isValidSymlinkRecord(const SymlinkRecord &record) {
  return record.filesystemId != FilesystemId::Null()
      && record.symlinkId != ObjectId::Null()
      && record.generation != 0
      && isValidObjectMetadata(record.metadata)
      && isValidTarget(record.target);
}

size_t serializedSize(const SymlinkRecord &record) {
  return cpputils::Serializer::StringSize(HEADER)
      + FilesystemId::BINARY_LENGTH
      + ObjectId::BINARY_LENGTH
      + sizeof(uint64_t)
      + serializedObjectMetadataSize()
      + cpputils::Serializer::StringSize(record.target);
}

}

bool operator==(const SymlinkRecord &lhs, const SymlinkRecord &rhs) {
  return lhs.filesystemId == rhs.filesystemId
      && lhs.symlinkId == rhs.symlinkId
      && lhs.generation == rhs.generation
      && lhs.metadata == rhs.metadata
      && lhs.target == rhs.target;
}

bool operator!=(const SymlinkRecord &lhs, const SymlinkRecord &rhs) {
  return !(lhs == rhs);
}

cpputils::Data serializeSymlinkRecord(const SymlinkRecord &record) {
  if (!isValidSymlinkRecord(record)) {
    throw std::runtime_error("Invalid format-v2 symlink record");
  }

  cpputils::Serializer serializer(serializedSize(record));
  serializer.writeString(HEADER);
  serializer.writeFixedSizeData<FilesystemId::BINARY_LENGTH>(record.filesystemId);
  serializer.writeFixedSizeData<ObjectId::BINARY_LENGTH>(record.symlinkId);
  serializer.writeUint64(record.generation);
  writeObjectMetadata(&serializer, record.metadata);
  serializer.writeString(record.target);
  return serializer.finished();
}

boost::optional<SymlinkRecord> deserializeSymlinkRecord(const cpputils::Data &data) {
  try {
    cpputils::Deserializer deserializer(&data);
    const std::string header = deserializer.readString();
    if (header != HEADER) {
      return boost::none;
    }

    SymlinkRecord record{
      deserializer.readFixedSizeData<FilesystemId::BINARY_LENGTH>(),
      deserializer.readFixedSizeData<ObjectId::BINARY_LENGTH>(),
      deserializer.readUint64(),
      readObjectMetadata(&deserializer),
      deserializer.readString()
    };
    deserializer.finished();

    if (!isValidSymlinkRecord(record)) {
      return boost::none;
    }
    return record;
  } catch (const std::exception&) {
    return boost::none;
  }
}

bool symlinkRecordMatchesObject(
  const SymlinkRecord &record,
  const FilesystemId &expectedFilesystemId,
  const ObjectId &expectedSymlinkId,
  uint64_t expectedGeneration) {
  return record.filesystemId == expectedFilesystemId
      && record.symlinkId == expectedSymlinkId
      && record.generation == expectedGeneration;
}

}
}
