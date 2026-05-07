#include "RootContent.h"

#include <cpp-utils/data/Deserializer.h>
#include <cpp-utils/data/Serializer.h>

#include <stdexcept>
#include <string>

namespace cryfs {
namespace formatv2 {
namespace {

const std::string HEADER = "cryfs.formatv2.root-content;0";

bool isValidRootContent(const RootContent &content) {
  return content.filesystemId != FilesystemId::Null()
      && content.epoch != 0
      && content.rootId != RootId::Null()
      && content.rootDirectoryId != ObjectId::Null()
      && content.rootDirectoryGeneration != 0;
}

size_t serializedSize() {
  return cpputils::Serializer::StringSize(HEADER)
      + FilesystemId::BINARY_LENGTH
      + sizeof(uint64_t)
      + RootId::BINARY_LENGTH
      + ObjectId::BINARY_LENGTH
      + sizeof(uint64_t);
}

}

bool operator==(const RootContent &lhs, const RootContent &rhs) {
  return lhs.filesystemId == rhs.filesystemId
      && lhs.epoch == rhs.epoch
      && lhs.rootId == rhs.rootId
      && lhs.rootDirectoryId == rhs.rootDirectoryId
      && lhs.rootDirectoryGeneration == rhs.rootDirectoryGeneration;
}

bool operator!=(const RootContent &lhs, const RootContent &rhs) {
  return !(lhs == rhs);
}

bool operator==(const RootDirectoryRef &lhs, const RootDirectoryRef &rhs) {
  return lhs.objectId == rhs.objectId
      && lhs.generation == rhs.generation;
}

bool operator!=(const RootDirectoryRef &lhs, const RootDirectoryRef &rhs) {
  return !(lhs == rhs);
}

cpputils::Data serializeRootContent(const RootContent &content) {
  if (!isValidRootContent(content)) {
    throw std::runtime_error("Invalid format-v2 root content");
  }

  cpputils::Serializer serializer(serializedSize());
  serializer.writeString(HEADER);
  serializer.writeFixedSizeData<FilesystemId::BINARY_LENGTH>(content.filesystemId);
  serializer.writeUint64(content.epoch);
  serializer.writeFixedSizeData<RootId::BINARY_LENGTH>(content.rootId);
  serializer.writeFixedSizeData<ObjectId::BINARY_LENGTH>(content.rootDirectoryId);
  serializer.writeUint64(content.rootDirectoryGeneration);
  return serializer.finished();
}

boost::optional<RootContent> deserializeRootContent(const cpputils::Data &data) {
  try {
    cpputils::Deserializer deserializer(&data);
    const std::string header = deserializer.readString();
    if (header != HEADER) {
      return boost::none;
    }

    RootContent content{
      deserializer.readFixedSizeData<FilesystemId::BINARY_LENGTH>(),
      deserializer.readUint64(),
      deserializer.readFixedSizeData<RootId::BINARY_LENGTH>(),
      deserializer.readFixedSizeData<ObjectId::BINARY_LENGTH>(),
      deserializer.readUint64()
    };
    deserializer.finished();

    if (!isValidRootContent(content)) {
      return boost::none;
    }
    return content;
  } catch (const std::exception&) {
    return boost::none;
  }
}

boost::optional<RootDirectoryRef> trustedRootDirectoryFromContent(
  const RootContent &content,
  const FilesystemId &expectedFilesystemId,
  const AuthenticatedRoot &expectedRoot) {
  if (content.filesystemId != expectedFilesystemId
      || content.epoch != expectedRoot.epoch
      || content.rootId != expectedRoot.rootId) {
    return boost::none;
  }
  return RootDirectoryRef{content.rootDirectoryId, content.rootDirectoryGeneration};
}

}
}
