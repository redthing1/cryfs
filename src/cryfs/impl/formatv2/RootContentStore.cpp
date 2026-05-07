#include "RootContentStore.h"

#include "ImmutableRecordStore.h"

#include <boost/filesystem.hpp>
#include <cpp-utils/system/AtomicFile.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace bf = boost::filesystem;

namespace cryfs {
namespace formatv2 {

RootContentStore::RootContentStore(
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

void RootContentStore::store(const RootContent &content) const {
  if (content.filesystemId != _filesystemId) {
    throw std::runtime_error("Refusing to store format-v2 root content for a different filesystem");
  }

  const cpputils::Data serialized = serializeRootContent(content);
  const cpputils::Data encrypted = encryptObjectRecordPayload(
    serialized,
    _objectEncryptionKey,
    _filesystemId,
    ObjectRecordType::RootContent,
    content.rootId,
    content.epoch);
  storeImmutableRecord(
    _pathForRoot(content.rootId),
    encrypted,
    [this, &content] {
      return load(AuthenticatedRoot{content.epoch, content.rootId});
    },
    content,
    "root content");
}

boost::optional<RootContent> RootContentStore::load(const AuthenticatedRoot &root) const {
  const boost::optional<cpputils::Data> encrypted = cpputils::Data::LoadFromFile(_pathForRoot(root.rootId));
  if (encrypted == boost::none) {
    return boost::none;
  }

  const boost::optional<cpputils::Data> serialized = decryptObjectRecordPayload(
    *encrypted,
    _objectEncryptionKey,
    _filesystemId,
    ObjectRecordType::RootContent,
    root.rootId,
    root.epoch);
  if (serialized == boost::none) {
    return boost::none;
  }

  const boost::optional<RootContent> content = deserializeRootContent(*serialized);
  if (content == boost::none) {
    return boost::none;
  }

  if (trustedRootDirectoryFromContent(*content, _filesystemId, root) == boost::none) {
    return boost::none;
  }
  return content;
}

bf::path RootContentStore::_pathForRoot(const RootId &rootId) const {
  return _directory / ("root-content." + rootId.ToString());
}

}
}
