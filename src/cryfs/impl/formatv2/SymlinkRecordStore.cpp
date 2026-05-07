#include "SymlinkRecordStore.h"

#include "ImmutableRecordStore.h"

#include <boost/filesystem.hpp>
#include <cpp-utils/system/AtomicFile.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace bf = boost::filesystem;

namespace cryfs {
namespace formatv2 {

SymlinkRecordStore::SymlinkRecordStore(
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

void SymlinkRecordStore::store(const SymlinkRecord &record) const {
  if (record.filesystemId != _filesystemId) {
    throw std::runtime_error("Refusing to store format-v2 symlink for a different filesystem");
  }

  const cpputils::Data serialized = serializeSymlinkRecord(record);
  const cpputils::Data encrypted = encryptObjectRecordPayload(
    serialized,
    _objectEncryptionKey,
    _filesystemId,
    ObjectRecordType::Symlink,
    record.symlinkId,
    record.generation);
  storeImmutableRecord(
    _pathForSymlink(record.symlinkId, record.generation),
    encrypted,
    [this, &record] {
      return load(record.symlinkId, record.generation);
    },
    record,
    "symlink record");
}

boost::optional<SymlinkRecord> SymlinkRecordStore::load(
  const ObjectId &symlinkId,
  uint64_t generation) const {
  const boost::optional<cpputils::Data> encrypted =
    cpputils::Data::LoadFromFile(_pathForSymlink(symlinkId, generation));
  if (encrypted == boost::none) {
    return boost::none;
  }

  const boost::optional<cpputils::Data> serialized = decryptObjectRecordPayload(
    *encrypted,
    _objectEncryptionKey,
    _filesystemId,
    ObjectRecordType::Symlink,
    symlinkId,
    generation);
  if (serialized == boost::none) {
    return boost::none;
  }

  const boost::optional<SymlinkRecord> record = deserializeSymlinkRecord(*serialized);
  if (record == boost::none) {
    return boost::none;
  }

  if (!symlinkRecordMatchesObject(*record, _filesystemId, symlinkId, generation)) {
    return boost::none;
  }
  return record;
}

bf::path SymlinkRecordStore::_pathForSymlink(const ObjectId &symlinkId, uint64_t generation) const {
  return _directory / ("symlink." + symlinkId.ToString() + "." + std::to_string(generation));
}

}
}
