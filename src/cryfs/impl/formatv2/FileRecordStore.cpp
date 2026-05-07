#include "FileRecordStore.h"

#include "ImmutableRecordStore.h"

#include <boost/filesystem.hpp>
#include <cpp-utils/system/AtomicFile.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace bf = boost::filesystem;

namespace cryfs {
namespace formatv2 {

FileRecordStore::FileRecordStore(
  bf::path directory,
  FilesystemId filesystemId,
  cpputils::EncryptionKey objectEncryptionKey)
  : _directory(std::move(directory)),
    _filesystemId(filesystemId),
    _objectEncryptionKey(std::move(objectEncryptionKey)) {
  if (_filesystemId == FilesystemId::Null()) {
    throw std::runtime_error("Invalid format-v2 filesystem id");
  }
  cpputils::createDirectoryDurably(_directory);
}

void FileRecordStore::store(const FileRecord &record) const {
  if (record.filesystemId != _filesystemId) {
    throw std::runtime_error("Refusing to store format-v2 file for a different filesystem");
  }

  const cpputils::Data serialized = serializeFileRecord(record);
  const cpputils::Data encrypted = encryptObjectRecordPayload(
    serialized,
    _objectEncryptionKey,
    _filesystemId,
    ObjectRecordType::File,
    record.fileId,
    record.generation);
  storeImmutableRecord(
    _pathForFile(record.fileId, record.generation),
    encrypted,
    [this, &record] {
      return load(record.fileId, record.generation);
    },
    record,
    "file record");
}

boost::optional<FileRecord> FileRecordStore::load(const ObjectId &fileId, uint64_t generation) const {
  const boost::optional<cpputils::Data> encrypted = cpputils::Data::LoadFromFile(_pathForFile(fileId, generation));
  if (encrypted == boost::none) {
    return boost::none;
  }

  const boost::optional<cpputils::Data> serialized = decryptObjectRecordPayload(
    *encrypted,
    _objectEncryptionKey,
    _filesystemId,
    ObjectRecordType::File,
    fileId,
    generation);
  if (serialized == boost::none) {
    return boost::none;
  }

  const boost::optional<FileRecord> record = deserializeFileRecord(*serialized);
  if (record == boost::none) {
    return boost::none;
  }

  if (!fileRecordMatchesObject(*record, _filesystemId, fileId, generation)) {
    return boost::none;
  }
  return record;
}

void FileRecordStore::storeData(const FileDataRecord &record) const {
  if (record.filesystemId != _filesystemId) {
    throw std::runtime_error("Refusing to store format-v2 file data for a different filesystem");
  }

  const cpputils::Data serialized = serializeFileDataRecord(record);
  const cpputils::Data encrypted = encryptObjectRecordPayload(
    serialized,
    _objectEncryptionKey,
    _filesystemId,
    ObjectRecordType::FileData,
    record.dataId,
    record.generation);
  storeImmutableRecord(
    _pathForFileData(record.dataId, record.generation),
    encrypted,
    [this, &record] {
      return loadData(record.dataId, record.generation);
    },
    record,
    "file data record");
}

boost::optional<FileDataRecord> FileRecordStore::loadData(
  const ObjectId &dataId,
  uint64_t generation) const {
  const boost::optional<cpputils::Data> encrypted = cpputils::Data::LoadFromFile(_pathForFileData(dataId, generation));
  if (encrypted == boost::none) {
    return boost::none;
  }

  const boost::optional<cpputils::Data> serialized = decryptObjectRecordPayload(
    *encrypted,
    _objectEncryptionKey,
    _filesystemId,
    ObjectRecordType::FileData,
    dataId,
    generation);
  if (serialized == boost::none) {
    return boost::none;
  }

  boost::optional<FileDataRecord> record = deserializeFileDataRecord(*serialized);
  if (record == boost::none) {
    return boost::none;
  }

  if (!fileDataRecordMatchesObject(*record, _filesystemId, dataId, generation)) {
    return boost::none;
  }
  return record;
}

bf::path FileRecordStore::_pathForFile(const ObjectId &fileId, uint64_t generation) const {
  return _directory / ("file." + fileId.ToString() + "." + std::to_string(generation));
}

bf::path FileRecordStore::_pathForFileData(const ObjectId &dataId, uint64_t generation) const {
  return _directory / ("file-data." + dataId.ToString() + "." + std::to_string(generation));
}

}
}
