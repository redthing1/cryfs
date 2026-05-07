#include "DirectoryRecordStore.h"

#include "ImmutableRecordStore.h"

#include <boost/filesystem.hpp>
#include <cpp-utils/system/AtomicFile.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace bf = boost::filesystem;

namespace cryfs {
namespace formatv2 {

DirectoryRecordStore::DirectoryRecordStore(
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

void DirectoryRecordStore::store(const DirectoryRecord &record) const {
  if (record.filesystemId != _filesystemId) {
    throw std::runtime_error("Refusing to store format-v2 directory for a different filesystem");
  }

  const cpputils::Data serialized = serializeDirectoryRecord(record);
  const cpputils::Data encrypted = encryptObjectRecordPayload(
    serialized,
    _objectEncryptionKey,
    _filesystemId,
    ObjectRecordType::Directory,
    record.directoryId,
    record.generation);
  storeImmutableRecord(
    _pathForDirectory(record.directoryId, record.generation),
    encrypted,
    [this, &record] {
      return load(record.directoryId, record.generation);
    },
    record,
    "directory record");
}

boost::optional<DirectoryRecord> DirectoryRecordStore::load(
  const ObjectId &directoryId,
  uint64_t generation) const {
  const boost::optional<cpputils::Data> encrypted =
    cpputils::Data::LoadFromFile(_pathForDirectory(directoryId, generation));
  if (encrypted == boost::none) {
    return boost::none;
  }

  const boost::optional<cpputils::Data> serialized = decryptObjectRecordPayload(
    *encrypted,
    _objectEncryptionKey,
    _filesystemId,
    ObjectRecordType::Directory,
    directoryId,
    generation);
  if (serialized == boost::none) {
    return boost::none;
  }

  const boost::optional<DirectoryRecord> record = deserializeDirectoryRecord(*serialized);
  if (record == boost::none) {
    return boost::none;
  }

  if (!directoryRecordMatchesObject(*record, _filesystemId, directoryId, generation)) {
    return boost::none;
  }
  return record;
}

bf::path DirectoryRecordStore::_pathForDirectory(const ObjectId &directoryId, uint64_t generation) const {
  return _directory / ("directory." + directoryId.ToString() + "." + std::to_string(generation));
}

}
}
