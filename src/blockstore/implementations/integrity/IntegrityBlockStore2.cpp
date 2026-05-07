#include <blockstore/interface/BlockStore2.h>
#include "IntegrityBlockStore2.h"
#include "KnownBlockVersions.h"
#include <cpp-utils/data/SerializationHelper.h>

using cpputils::Data;
using cpputils::unique_ref;
using cpputils::serialize;
using cpputils::deserialize;
using std::string;
using boost::optional;
using boost::none;
using namespace cpputils::logging;

namespace blockstore {
namespace integrity {

constexpr uint16_t IntegrityBlockStore2::FORMAT_VERSION_HEADER;
constexpr uint64_t IntegrityBlockStore2::VERSION_ZERO;
constexpr unsigned int IntegrityBlockStore2::ID_HEADER_OFFSET;
constexpr unsigned int IntegrityBlockStore2::CLIENTID_HEADER_OFFSET;
constexpr unsigned int IntegrityBlockStore2::VERSION_HEADER_OFFSET;
constexpr unsigned int IntegrityBlockStore2::HEADER_LENGTH;

Data IntegrityBlockStore2::_prependHeaderToData(const BlockId& blockId, uint32_t myClientId, uint64_t version, const Data &data) {
  static_assert(HEADER_LENGTH == sizeof(FORMAT_VERSION_HEADER) + BlockId::BINARY_LENGTH + sizeof(myClientId) + sizeof(version), "Wrong header length");
  Data result(data.size() + HEADER_LENGTH);
  serialize<uint16_t>(result.dataOffset(0), FORMAT_VERSION_HEADER);
  blockId.ToBinary(result.dataOffset(ID_HEADER_OFFSET));
  serialize<uint32_t>(result.dataOffset(CLIENTID_HEADER_OFFSET), myClientId);
  serialize<uint64_t>(result.dataOffset(VERSION_HEADER_OFFSET), version);
  std::memcpy(result.dataOffset(HEADER_LENGTH), data.data(), data.size());
  return result;
}

bool IntegrityBlockStore2::_checkHeader(const BlockId &blockId, const Data &data) const {
  if (data.size() < HEADER_LENGTH) {
    throw std::runtime_error("The versioned block is truncated.");
  }
  _checkFormatHeader(data);
  return _checkIdHeader(blockId, data) && _checkVersionHeader(blockId, data);
}

void IntegrityBlockStore2::_checkFormatHeader(const Data &data) const {
  if (FORMAT_VERSION_HEADER != _readFormatHeader(data)) {
    throw std::runtime_error("The versioned block has the wrong format. Was it created with a newer version of CryFS?");
  }
}

bool IntegrityBlockStore2::_checkVersionHeader(const BlockId &blockId, const Data &data) const {
  const uint32_t clientId = _readClientId(data);
  const uint64_t version = _readVersion(data);

  if(!_knownBlockVersions.checkAndUpdateVersion(clientId, blockId, version)) {
    integrityViolationDetected("The block version number is too low. Did an attacker try to roll back the block or to re-introduce a deleted block?");
    return false;
  }

  return true;
}

bool IntegrityBlockStore2::_checkIdHeader(const BlockId &expectedBlockId, const Data &data) const {
  // The obvious reason for this is to prevent adversaries from renaming blocks, but storing the block id in this way also
  // makes the authenticated cipher more robust, see https://libsodium.gitbook.io/doc/secret-key_cryptography/aead#robustness
  const BlockId actualBlockId = _readBlockId(data);
  if (expectedBlockId != actualBlockId) {
    integrityViolationDetected("The block id is wrong. Did an attacker try to rename some blocks?");
    return false;
  }

  return true;
}

uint16_t IntegrityBlockStore2::_readFormatHeader(const Data &data) {
  return deserialize<uint16_t>(data.data());
}

uint32_t IntegrityBlockStore2::_readClientId(const Data &data) {
  return deserialize<uint32_t>(data.dataOffset(CLIENTID_HEADER_OFFSET));
}

BlockId IntegrityBlockStore2::_readBlockId(const Data &data) {
  return BlockId::FromBinary(data.dataOffset(ID_HEADER_OFFSET));
}

uint64_t IntegrityBlockStore2::_readVersion(const Data &data) {
  return deserialize<uint64_t>(data.dataOffset(VERSION_HEADER_OFFSET));
}

Data IntegrityBlockStore2::_removeHeader(const Data &data) {
  return data.copyAndRemovePrefix(HEADER_LENGTH);
}

void IntegrityBlockStore2::integrityViolationDetected(const string &reason) const {
  LOG(WARN, "Integrity violation detected: {}", reason);
  _knownBlockVersions.setIntegrityViolationOnPreviousRun(true);
  try {
    _knownBlockVersions.save();
  } catch (...) {
    _onIntegrityViolation();
    throw;
  }
  _onIntegrityViolation();
}

IntegrityBlockStore2::IntegrityBlockStore2(unique_ref<BlockStore2> baseBlockStore, const boost::filesystem::path &integrityFilePath, uint32_t myClientId, std::function<void ()> onIntegrityViolation)
: _baseBlockStore(std::move(baseBlockStore)), _knownBlockVersions(integrityFilePath, myClientId), _onIntegrityViolation(std::move(onIntegrityViolation)) {
  if (_knownBlockVersions.integrityViolationOnPreviousRun()) {
    throw IntegrityViolationOnPreviousRun(_knownBlockVersions.path());
  }
}

bool IntegrityBlockStore2::tryCreate(const BlockId &blockId, const Data &data) {
  auto rollback = _knownBlockVersions.rollbackOnFailure(blockId);
  const uint64_t version = _knownBlockVersions.incrementVersion(blockId);
  const Data dataWithHeader = _prependHeaderToData(blockId, _knownBlockVersions.myClientId(), version, data);
  const bool created = _baseBlockStore->tryCreate(blockId, dataWithHeader);
  if (created) {
    rollback.commit();
  }
  return created;
}

bool IntegrityBlockStore2::remove(const BlockId &blockId) {
  auto rollback = _knownBlockVersions.rollbackOnFailure(blockId);
  _knownBlockVersions.markBlockAsDeleted(blockId);
  const bool removed = _baseBlockStore->remove(blockId);
  if (removed) {
    rollback.commit();
  }
  return removed;
}

optional<Data> IntegrityBlockStore2::load(const BlockId &blockId) const {
  auto loaded = _baseBlockStore->load(blockId);
  if (none == loaded) {
    if (_knownBlockVersions.blockShouldExist(blockId)) {
      integrityViolationDetected("A block that should exist wasn't found. Did an attacker delete it?");
    }
    return optional<Data>(none);
  }
  if (!_checkHeader(blockId, *loaded)) {
    return optional<Data>(none);
  }
  return optional<Data>(_removeHeader(*loaded));
}

void IntegrityBlockStore2::store(const BlockId &blockId, const Data &data) {
  auto rollback = _knownBlockVersions.rollbackOnFailure(blockId);
  const uint64_t version = _knownBlockVersions.incrementVersion(blockId);
  const Data dataWithHeader = _prependHeaderToData(blockId, _knownBlockVersions.myClientId(), version, data);
  _baseBlockStore->store(blockId, dataWithHeader);
  rollback.commit();
}

uint64_t IntegrityBlockStore2::numBlocks() const {
  return _baseBlockStore->numBlocks();
}

uint64_t IntegrityBlockStore2::estimateNumFreeBytes() const {
  return _baseBlockStore->estimateNumFreeBytes();
}

uint64_t IntegrityBlockStore2::blockSizeFromPhysicalBlockSize(uint64_t blockSize) const {
  const uint64_t baseBlockSize = _baseBlockStore->blockSizeFromPhysicalBlockSize(blockSize);
  if (baseBlockSize <= HEADER_LENGTH) {
    return 0;
  }
  return baseBlockSize - HEADER_LENGTH;
}

void IntegrityBlockStore2::forEachBlock(std::function<void (const BlockId &)> callback) const {
  std::unordered_set<blockstore::BlockId> existingBlocks = _knownBlockVersions.existingBlocks();
  _baseBlockStore->forEachBlock([&existingBlocks, callback] (const BlockId &blockId) {
    callback(blockId);

    auto found = existingBlocks.find(blockId);
    if (found != existingBlocks.end()) {
      existingBlocks.erase(found);
    }
  });
  if (!existingBlocks.empty()) {
    integrityViolationDetected("A block that should have existed wasn't found.");
  }
}

void IntegrityBlockStore2::flush() {
  _baseBlockStore->flush();
}

void IntegrityBlockStore2::sync() {
  _baseBlockStore->sync();
  _knownBlockVersions.save();
}

}
}
