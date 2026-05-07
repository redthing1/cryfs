#include "DirectoryRecord.h"

#include <cpp-utils/data/Deserializer.h>
#include <cpp-utils/data/Serializer.h>

#include <stdexcept>
#include <string>

namespace cryfs {
namespace formatv2 {
namespace {

const std::string HEADER = "cryfs.formatv2.directory;0";

bool isKnownObjectType(ObjectType type) {
  return type == ObjectType::File
      || type == ObjectType::Directory
      || type == ObjectType::Symlink;
}

bool isValidEntryName(const std::string &name) {
  return !name.empty()
      && name != "."
      && name != ".."
      && name.find('/') == std::string::npos
      && name.find('\0') == std::string::npos;
}

bool isValidEntry(const DirectoryEntry &entry) {
  return isValidEntryName(entry.name)
      && isKnownObjectType(entry.type)
      && entry.objectId != ObjectId::Null()
      && entry.generation != 0;
}

bool entriesAreCanonical(const std::vector<DirectoryEntry> &entries) {
  for (size_t i = 0; i < entries.size(); ++i) {
    if (!isValidEntry(entries[i])) {
      return false;
    }
    if (i != 0 && !(entries[i - 1].name < entries[i].name)) {
      return false;
    }
  }
  return true;
}

bool isValidDirectoryRecord(const DirectoryRecord &record) {
  return record.filesystemId != FilesystemId::Null()
      && record.directoryId != ObjectId::Null()
      && record.generation != 0
      && isValidObjectMetadata(record.metadata)
      && entriesAreCanonical(record.entries);
}

size_t entrySize(const DirectoryEntry &entry) {
  return cpputils::Serializer::StringSize(entry.name)
      + sizeof(uint8_t)
      + ObjectId::BINARY_LENGTH
      + sizeof(uint64_t);
}

size_t entriesSize(const std::vector<DirectoryEntry> &entries) {
  size_t size = sizeof(uint64_t);
  for (const DirectoryEntry &entry: entries) {
    size += entrySize(entry);
  }
  return size;
}

size_t serializedSize(const DirectoryRecord &record) {
  return cpputils::Serializer::StringSize(HEADER)
      + FilesystemId::BINARY_LENGTH
      + ObjectId::BINARY_LENGTH
      + sizeof(uint64_t)
      + serializedObjectMetadataSize()
      + entriesSize(record.entries);
}

void writeEntry(cpputils::Serializer *serializer, const DirectoryEntry &entry) {
  serializer->writeString(entry.name);
  serializer->writeUint8(static_cast<uint8_t>(entry.type));
  serializer->writeFixedSizeData<ObjectId::BINARY_LENGTH>(entry.objectId);
  serializer->writeUint64(entry.generation);
}

DirectoryEntry readEntry(cpputils::Deserializer *deserializer) {
  return DirectoryEntry{
    deserializer->readString(),
    static_cast<ObjectType>(deserializer->readUint8()),
    deserializer->readFixedSizeData<ObjectId::BINARY_LENGTH>(),
    deserializer->readUint64()
  };
}

void writeEntries(cpputils::Serializer *serializer, const std::vector<DirectoryEntry> &entries) {
  serializer->writeUint64(entries.size());
  for (const DirectoryEntry &entry: entries) {
    writeEntry(serializer, entry);
  }
}

boost::optional<std::vector<DirectoryEntry>> readEntries(
  cpputils::Deserializer *deserializer,
  const cpputils::Data &data) {
  const uint64_t numEntries = deserializer->readUint64();
  if (numEntries > static_cast<uint64_t>(data.size())) {
    return boost::none;
  }

  std::vector<DirectoryEntry> entries;
  entries.reserve(static_cast<size_t>(numEntries));
  for (uint64_t i = 0; i < numEntries; ++i) {
    entries.push_back(readEntry(deserializer));
  }
  return entries;
}

}

bool operator==(const DirectoryEntry &lhs, const DirectoryEntry &rhs) {
  return lhs.name == rhs.name
      && lhs.type == rhs.type
      && lhs.objectId == rhs.objectId
      && lhs.generation == rhs.generation;
}

bool operator!=(const DirectoryEntry &lhs, const DirectoryEntry &rhs) {
  return !(lhs == rhs);
}

bool operator==(const DirectoryRecord &lhs, const DirectoryRecord &rhs) {
  return lhs.filesystemId == rhs.filesystemId
      && lhs.directoryId == rhs.directoryId
      && lhs.generation == rhs.generation
      && lhs.metadata == rhs.metadata
      && lhs.entries == rhs.entries;
}

bool operator!=(const DirectoryRecord &lhs, const DirectoryRecord &rhs) {
  return !(lhs == rhs);
}

cpputils::Data serializeDirectoryRecord(const DirectoryRecord &record) {
  if (!isValidDirectoryRecord(record)) {
    throw std::runtime_error("Invalid format-v2 directory record");
  }

  cpputils::Serializer serializer(serializedSize(record));
  serializer.writeString(HEADER);
  serializer.writeFixedSizeData<FilesystemId::BINARY_LENGTH>(record.filesystemId);
  serializer.writeFixedSizeData<ObjectId::BINARY_LENGTH>(record.directoryId);
  serializer.writeUint64(record.generation);
  writeObjectMetadata(&serializer, record.metadata);
  writeEntries(&serializer, record.entries);
  return serializer.finished();
}

boost::optional<DirectoryRecord> deserializeDirectoryRecord(const cpputils::Data &data) {
  try {
    cpputils::Deserializer deserializer(&data);
    const std::string header = deserializer.readString();
    if (header != HEADER) {
      return boost::none;
    }

    DirectoryRecord record{
      deserializer.readFixedSizeData<FilesystemId::BINARY_LENGTH>(),
      deserializer.readFixedSizeData<ObjectId::BINARY_LENGTH>(),
      deserializer.readUint64(),
      readObjectMetadata(&deserializer),
      {}
    };
    const boost::optional<std::vector<DirectoryEntry>> entries = readEntries(&deserializer, data);
    if (entries == boost::none) {
      return boost::none;
    }
    record.entries = *entries;
    deserializer.finished();

    if (!isValidDirectoryRecord(record)) {
      return boost::none;
    }
    return record;
  } catch (const std::exception&) {
    return boost::none;
  }
}

bool directoryRecordMatchesObject(
  const DirectoryRecord &record,
  const FilesystemId &expectedFilesystemId,
  const ObjectId &expectedDirectoryId,
  uint64_t expectedGeneration) {
  return record.filesystemId == expectedFilesystemId
      && record.directoryId == expectedDirectoryId
      && record.generation == expectedGeneration;
}

}
}
