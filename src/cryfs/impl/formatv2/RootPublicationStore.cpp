#include "RootPublicationStore.h"

#include <boost/filesystem.hpp>
#include <cpp-utils/system/AtomicFile.h>

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace bf = boost::filesystem;

namespace cryfs {
namespace formatv2 {
namespace {

const std::array<const char*, 2> ROOT_SLOTS = {{"root.0", "root.1"}};

const char *slotNameForEpoch(uint64_t epoch) {
  return ROOT_SLOTS[epoch % ROOT_SLOTS.size()];
}

}

RootPublicationStore::RootPublicationStore(
  bf::path directory,
  FilesystemId filesystemId,
  cpputils::EncryptionKey authenticationKey)
  : _directory(std::move(directory)),
    _filesystemId(filesystemId),
    _authenticationKey(std::move(authenticationKey)) {
  if (_filesystemId == FilesystemId::Null()) {
    throw std::runtime_error("Invalid format-v2 filesystem id");
  }
  cpputils::createDirectoryTreeDurably(_directory);
}

void RootPublicationStore::publish(const RootRecord &record) const {
  if (record.filesystemId != _filesystemId) {
    throw std::runtime_error("Refusing to publish format-v2 root for a different filesystem");
  }

  const cpputils::Data authenticatedRecord = authenticateRootRecord(record, _authenticationKey);
  authenticatedRecord.StoreToFile(_slotPathForEpoch(record.epoch));
}

std::vector<AuthenticatedRoot> RootPublicationStore::loadAuthenticatedRoots() const {
  std::vector<AuthenticatedRoot> roots;
  for (const char *slotName: ROOT_SLOTS) {
    const bf::path slotPath = _directory / slotName;
    const boost::optional<cpputils::Data> data = cpputils::Data::LoadFromFile(slotPath);
    if (data == boost::none) {
      continue;
    }

    const boost::optional<AuthenticatedRoot> root = verifyRootRecord(
      *data,
      _authenticationKey,
      _filesystemId);
    if (root != boost::none) {
      roots.push_back(*root);
    }
  }
  return roots;
}

bf::path RootPublicationStore::_slotPathForEpoch(uint64_t epoch) const {
  return _directory / slotNameForEpoch(epoch);
}

}
}
