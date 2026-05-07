#include "FileRecord.h"

#include <cpp-utils/data/Deserializer.h>
#include <cpp-utils/data/Serializer.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace cryfs {
namespace formatv2 {
namespace {

const std::string FILE_HEADER = "cryfs.formatv2.file;0";
const std::string FILE_DATA_HEADER = "cryfs.formatv2.file-data;0";

bool isValidExtent(const FileExtent &extent, uint64_t fileSize) {
  return extent.size != 0
      && extent.offset < fileSize
      && extent.size <= fileSize - extent.offset
      && extent.dataId != ObjectId::Null()
      && extent.generation != 0;
}

bool extentsAreCanonical(const std::vector<FileExtent> &extents, uint64_t fileSize) {
  uint64_t nextAllowedOffset = 0;
  for (const FileExtent &extent: extents) {
    if (!isValidExtent(extent, fileSize) || extent.offset < nextAllowedOffset) {
      return false;
    }
    nextAllowedOffset = extent.offset + extent.size;
  }
  return true;
}

bool isValidFileRecord(const FileRecord &record) {
  return record.filesystemId != FilesystemId::Null()
      && record.fileId != ObjectId::Null()
      && record.generation != 0
      && isValidObjectMetadata(record.metadata)
      && extentsAreCanonical(record.extents, record.size);
}

bool isValidFileDataRecord(const FileDataRecord &record) {
  return record.filesystemId != FilesystemId::Null()
      && record.dataId != ObjectId::Null()
      && record.generation != 0
      && record.payload.size() != 0;
}

size_t extentSize() {
  return sizeof(uint64_t)
      + sizeof(uint64_t)
      + ObjectId::BINARY_LENGTH
      + sizeof(uint64_t);
}

size_t extentsSize(const std::vector<FileExtent> &extents) {
  return sizeof(uint64_t) + extents.size() * extentSize();
}

size_t serializedFileSize(const FileRecord &record) {
  return cpputils::Serializer::StringSize(FILE_HEADER)
      + FilesystemId::BINARY_LENGTH
      + ObjectId::BINARY_LENGTH
      + sizeof(uint64_t)
      + serializedObjectMetadataSize()
      + sizeof(uint64_t)
      + extentsSize(record.extents);
}

size_t serializedFileDataSize(const FileDataRecord &record) {
  return cpputils::Serializer::StringSize(FILE_DATA_HEADER)
      + FilesystemId::BINARY_LENGTH
      + ObjectId::BINARY_LENGTH
      + sizeof(uint64_t)
      + cpputils::Serializer::DataSize(record.payload);
}

void writeExtent(cpputils::Serializer *serializer, const FileExtent &extent) {
  serializer->writeUint64(extent.offset);
  serializer->writeUint64(extent.size);
  serializer->writeFixedSizeData<ObjectId::BINARY_LENGTH>(extent.dataId);
  serializer->writeUint64(extent.generation);
}

FileExtent readExtent(cpputils::Deserializer *deserializer) {
  return FileExtent{
    deserializer->readUint64(),
    deserializer->readUint64(),
    deserializer->readFixedSizeData<ObjectId::BINARY_LENGTH>(),
    deserializer->readUint64()
  };
}

void writeExtents(cpputils::Serializer *serializer, const std::vector<FileExtent> &extents) {
  serializer->writeUint64(extents.size());
  for (const FileExtent &extent: extents) {
    writeExtent(serializer, extent);
  }
}

boost::optional<std::vector<FileExtent>> readExtents(
  cpputils::Deserializer *deserializer,
  const cpputils::Data &data) {
  const uint64_t numExtents = deserializer->readUint64();
  if (numExtents > static_cast<uint64_t>(data.size())) {
    return boost::none;
  }

  std::vector<FileExtent> extents;
  extents.reserve(static_cast<size_t>(numExtents));
  for (uint64_t i = 0; i < numExtents; ++i) {
    extents.push_back(readExtent(deserializer));
  }
  return extents;
}

}

bool operator==(const FileExtent &lhs, const FileExtent &rhs) {
  return lhs.offset == rhs.offset
      && lhs.size == rhs.size
      && lhs.dataId == rhs.dataId
      && lhs.generation == rhs.generation;
}

bool operator!=(const FileExtent &lhs, const FileExtent &rhs) {
  return !(lhs == rhs);
}

bool operator==(const FileRecord &lhs, const FileRecord &rhs) {
  return lhs.filesystemId == rhs.filesystemId
      && lhs.fileId == rhs.fileId
      && lhs.generation == rhs.generation
      && lhs.metadata == rhs.metadata
      && lhs.size == rhs.size
      && lhs.extents == rhs.extents;
}

bool operator!=(const FileRecord &lhs, const FileRecord &rhs) {
  return !(lhs == rhs);
}

bool operator==(const FileDataRecord &lhs, const FileDataRecord &rhs) {
  return lhs.filesystemId == rhs.filesystemId
      && lhs.dataId == rhs.dataId
      && lhs.generation == rhs.generation
      && lhs.payload == rhs.payload;
}

bool operator!=(const FileDataRecord &lhs, const FileDataRecord &rhs) {
  return !(lhs == rhs);
}

cpputils::Data serializeFileRecord(const FileRecord &record) {
  if (!isValidFileRecord(record)) {
    throw std::runtime_error("Invalid format-v2 file record");
  }

  cpputils::Serializer serializer(serializedFileSize(record));
  serializer.writeString(FILE_HEADER);
  serializer.writeFixedSizeData<FilesystemId::BINARY_LENGTH>(record.filesystemId);
  serializer.writeFixedSizeData<ObjectId::BINARY_LENGTH>(record.fileId);
  serializer.writeUint64(record.generation);
  writeObjectMetadata(&serializer, record.metadata);
  serializer.writeUint64(record.size);
  writeExtents(&serializer, record.extents);
  return serializer.finished();
}

boost::optional<FileRecord> deserializeFileRecord(const cpputils::Data &data) {
  try {
    cpputils::Deserializer deserializer(&data);
    const std::string header = deserializer.readString();
    if (header != FILE_HEADER) {
      return boost::none;
    }

    FileRecord record{
      deserializer.readFixedSizeData<FilesystemId::BINARY_LENGTH>(),
      deserializer.readFixedSizeData<ObjectId::BINARY_LENGTH>(),
      deserializer.readUint64(),
      readObjectMetadata(&deserializer),
      deserializer.readUint64(),
      {}
    };
    const boost::optional<std::vector<FileExtent>> extents = readExtents(&deserializer, data);
    if (extents == boost::none) {
      return boost::none;
    }
    record.extents = *extents;
    deserializer.finished();

    if (!isValidFileRecord(record)) {
      return boost::none;
    }
    return record;
  } catch (const std::exception&) {
    return boost::none;
  }
}

cpputils::Data serializeFileDataRecord(const FileDataRecord &record) {
  if (!isValidFileDataRecord(record)) {
    throw std::runtime_error("Invalid format-v2 file data record");
  }

  cpputils::Serializer serializer(serializedFileDataSize(record));
  serializer.writeString(FILE_DATA_HEADER);
  serializer.writeFixedSizeData<FilesystemId::BINARY_LENGTH>(record.filesystemId);
  serializer.writeFixedSizeData<ObjectId::BINARY_LENGTH>(record.dataId);
  serializer.writeUint64(record.generation);
  serializer.writeData(record.payload);
  return serializer.finished();
}

boost::optional<FileDataRecord> deserializeFileDataRecord(const cpputils::Data &data) {
  try {
    cpputils::Deserializer deserializer(&data);
    const std::string header = deserializer.readString();
    if (header != FILE_DATA_HEADER) {
      return boost::none;
    }

    FileDataRecord record{
      deserializer.readFixedSizeData<FilesystemId::BINARY_LENGTH>(),
      deserializer.readFixedSizeData<ObjectId::BINARY_LENGTH>(),
      deserializer.readUint64(),
      deserializer.readData()
    };
    deserializer.finished();

    if (!isValidFileDataRecord(record)) {
      return boost::none;
    }
    return record;
  } catch (const std::exception&) {
    return boost::none;
  }
}

bool fileRecordMatchesObject(
  const FileRecord &record,
  const FilesystemId &expectedFilesystemId,
  const ObjectId &expectedFileId,
  uint64_t expectedGeneration) {
  return record.filesystemId == expectedFilesystemId
      && record.fileId == expectedFileId
      && record.generation == expectedGeneration;
}

bool fileDataRecordMatchesObject(
  const FileDataRecord &record,
  const FilesystemId &expectedFilesystemId,
  const ObjectId &expectedDataId,
  uint64_t expectedGeneration) {
  return record.filesystemId == expectedFilesystemId
      && record.dataId == expectedDataId
      && record.generation == expectedGeneration;
}

}
}
