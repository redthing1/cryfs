#include <boost/filesystem.hpp>
#include <fstream>
#include <cpp-utils/random/Random.h>
#include <unordered_set>
#include "KnownBlockVersions.h"

namespace bf = boost::filesystem;
using std::pair;
using std::string;
using std::unique_lock;
using std::mutex;
using boost::optional;
using boost::none;
using cpputils::Data;
using cpputils::Serializer;
using cpputils::Deserializer;

namespace blockstore {
namespace integrity {

const string KnownBlockVersions::HEADER = "cryfs.integritydata.knownblockversions;1";
constexpr uint32_t KnownBlockVersions::CLIENT_ID_FOR_DELETED_BLOCK;

KnownBlockVersions::KnownBlockVersions(const bf::path &stateFilePath, uint32_t myClientId)
        :_integrityViolationOnPreviousRun(false), _knownVersions(), _lastUpdateClientId(), _stateFilePath(stateFilePath), _myClientId(myClientId), _mutex(), _dirty(false), _valid(true) {
    const unique_lock<mutex> lock(_mutex);
    ASSERT(_myClientId != CLIENT_ID_FOR_DELETED_BLOCK, "This is not a valid client id");
    _loadStateFile();
}

KnownBlockVersions::KnownBlockVersions(KnownBlockVersions &&rhs) // NOLINT (intentionally not noexcept)
        : _integrityViolationOnPreviousRun(false), _knownVersions(), _lastUpdateClientId(), _stateFilePath(), _myClientId(0), _mutex(), _dirty(false), _valid(true) {
    const unique_lock<mutex> rhsLock(rhs._mutex);
    const unique_lock<mutex> lock(_mutex);
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer) -- we need to initialize those within the mutexes
    _integrityViolationOnPreviousRun = rhs._integrityViolationOnPreviousRun;
    _knownVersions = std::move(rhs._knownVersions);
    _lastUpdateClientId = std::move(rhs._lastUpdateClientId);
    _stateFilePath = std::move(rhs._stateFilePath);
    _myClientId = rhs._myClientId;
    _dirty = rhs._dirty;
    rhs._valid = false;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

KnownBlockVersions::~KnownBlockVersions() = default;

KnownBlockVersions::BlockStateRollback::BlockStateRollback(KnownBlockVersions *knownVersions, const BlockId &blockId)
        : _knownVersions(knownVersions), _blockId(blockId), _myVersion(), _lastUpdateClientId(), _dirty(false), _committed(false) {
    const unique_lock<mutex> lock(_knownVersions->_mutex);
    ASSERT(_knownVersions->_valid, "Object not valid due to a std::move");
    _dirty = _knownVersions->_dirty;

    const auto version = _knownVersions->_knownVersions.find({_knownVersions->_myClientId, blockId});
    if (version != _knownVersions->_knownVersions.end()) {
        _myVersion = version->second;
    }

    const auto lastUpdateClientId = _knownVersions->_lastUpdateClientId.find(blockId);
    if (lastUpdateClientId != _knownVersions->_lastUpdateClientId.end()) {
        _lastUpdateClientId = lastUpdateClientId->second;
    }
}

KnownBlockVersions::BlockStateRollback::~BlockStateRollback() {
    if (!_committed) {
        _knownVersions->_restoreBlockState(_blockId, _myVersion, _lastUpdateClientId, _dirty);
    }
}

void KnownBlockVersions::BlockStateRollback::commit() {
    _committed = true;
}

KnownBlockVersions::BlockStateRollback KnownBlockVersions::rollbackOnFailure(const BlockId &blockId) {
    return BlockStateRollback(this, blockId);
}

void KnownBlockVersions::setIntegrityViolationOnPreviousRun(bool value) {
    const unique_lock<mutex> lock(_mutex);
    ASSERT(_valid, "Object not valid due to a std::move");
    if (_integrityViolationOnPreviousRun == value) {
        return;
    }
    _integrityViolationOnPreviousRun = value;
    _dirty = true;
}

bool KnownBlockVersions::integrityViolationOnPreviousRun() const {
    const unique_lock<mutex> lock(_mutex);
    ASSERT(_valid, "Object not valid due to a std::move");
    return _integrityViolationOnPreviousRun;
}

void KnownBlockVersions::save() const {
    const unique_lock<mutex> lock(_mutex);
    ASSERT(_valid, "Object not valid due to a std::move");
    if (!_dirty) {
        return;
    }
    _saveStateFile();
    _dirty = false;
}

bool KnownBlockVersions::checkAndUpdateVersion(uint32_t clientId, const BlockId &blockId, uint64_t version) {
    const unique_lock<mutex> lock(_mutex);
    ASSERT(clientId != CLIENT_ID_FOR_DELETED_BLOCK, "This is not a valid client id");

    ASSERT(version > 0, "Version has to be >0"); // Otherwise we wouldn't handle notexisting entries correctly.
    ASSERT(_valid, "Object not valid due to a std::move");

    const ClientIdAndBlockId versionKey{clientId, blockId};
    const auto foundVersion = _knownVersions.find(versionKey);
    const uint64_t currentVersion = foundVersion == _knownVersions.end() ? 0 : foundVersion->second;
    if (currentVersion > version) {
        // This client already published a newer block version. Rollbacks are not allowed.
        return false;
    }

    const auto foundLastUpdateClientId = _lastUpdateClientId.find(blockId);
    const uint32_t currentLastUpdateClientId = foundLastUpdateClientId == _lastUpdateClientId.end() ? CLIENT_ID_FOR_DELETED_BLOCK : foundLastUpdateClientId->second;
    if (currentVersion == version && currentLastUpdateClientId != clientId) {
        // This is a roll back to the "newest" block of client [clientId], which was since then superseded by a version from client _lastUpdateClientId[blockId].
        // This is not allowed.
        return false;
    }

    if (currentVersion == version && currentLastUpdateClientId == clientId) {
        return true;
    }

    _knownVersions[versionKey] = version;
    _lastUpdateClientId[blockId] = clientId;
    _dirty = true;
    return true;
}

uint64_t KnownBlockVersions::incrementVersion(const BlockId &blockId) {
    const unique_lock<mutex> lock(_mutex);
    uint64_t &found = _knownVersions[{_myClientId, blockId}]; // If the entry doesn't exist, this creates it with value 0.
    const uint64_t newVersion = found + 1;
    if (newVersion == std::numeric_limits<uint64_t>::max()) {
        // It's *very* unlikely we ever run out of version numbers in 64bit...but just to be sure...
        throw std::runtime_error("Version overflow");
    }
    found = newVersion;
    _lastUpdateClientId[blockId] = _myClientId;
    _dirty = true;
    return found;
}

void KnownBlockVersions::_loadStateFile() {
    if (!bf::exists(_stateFilePath)) {
        // File doesn't exist means we loaded empty state.
        return;
    }
    if (!bf::is_regular_file(_stateFilePath)) {
        throw std::runtime_error("Invalid local state: Integrity file is not a regular file.");
    }
    optional<Data> file = Data::LoadFromFile(_stateFilePath);
    if (file == none) {
        throw std::runtime_error("Invalid local state: Could not read integrity file.");
    }
    Deserializer deserializer(&*file);
    const string loaded_header = deserializer.readString();

    if (HEADER != loaded_header) {
        throw std::runtime_error("Invalid local state: Invalid integrity file header.");
    }
    _integrityViolationOnPreviousRun = deserializer.readBool();
    _knownVersions = _deserializeKnownVersions(&deserializer);
    _lastUpdateClientId = _deserializeLastUpdateClientIds(&deserializer);

    deserializer.finished();
};


void KnownBlockVersions::_saveStateFile() const {
    Serializer serializer(
            Serializer::StringSize(HEADER) +
            Serializer::BoolSize() +
            sizeof(uint64_t) + _knownVersions.size() * (sizeof(uint32_t) + BlockId::BINARY_LENGTH + sizeof(uint64_t)) +
            sizeof(uint64_t) + _lastUpdateClientId.size() * (BlockId::BINARY_LENGTH + sizeof(uint32_t)));
    serializer.writeString(HEADER);
    serializer.writeBool(_integrityViolationOnPreviousRun);
    _serializeKnownVersions(&serializer, _knownVersions);
    _serializeLastUpdateClientIds(&serializer, _lastUpdateClientId);

    serializer.finished().StoreToFile(_stateFilePath);
}

void KnownBlockVersions::_restoreBlockState(
    const BlockId &blockId,
    const optional<uint64_t> &myVersion,
    const optional<uint32_t> &lastUpdateClientId,
    bool dirty) {
    const unique_lock<mutex> lock(_mutex);
    ASSERT(_valid, "Object not valid due to a std::move");

    const ClientIdAndBlockId versionKey{_myClientId, blockId};
    if (myVersion == none) {
        _knownVersions.erase(versionKey);
    } else {
        _knownVersions[versionKey] = *myVersion;
    }

    if (lastUpdateClientId == none) {
        _lastUpdateClientId.erase(blockId);
    } else {
        _lastUpdateClientId[blockId] = *lastUpdateClientId;
    }
    _dirty = dirty;
}

std::unordered_map<ClientIdAndBlockId, uint64_t> KnownBlockVersions::_deserializeKnownVersions(Deserializer *deserializer) {
    const uint64_t numEntries = deserializer->readUint64();
    std::unordered_map<ClientIdAndBlockId, uint64_t> result;
    result.reserve(static_cast<uint64_t>(1.2 * numEntries)); // Reserve for factor 1.2 more, so the file system doesn't immediately have to resize it on the first new block.
    for (uint64_t i = 0 ; i < numEntries; ++i) {
        auto entry = _deserializeKnownVersionsEntry(deserializer);
        result.insert(entry);
    }

    return result;
}

void KnownBlockVersions::_serializeKnownVersions(Serializer *serializer, const std::unordered_map<ClientIdAndBlockId, uint64_t>& knownVersions) {
    const uint64_t numEntries = knownVersions.size();
    serializer->writeUint64(numEntries);

    for (const auto &entry : knownVersions) {
        _serializeKnownVersionsEntry(serializer, entry);
    }
}

pair<ClientIdAndBlockId, uint64_t> KnownBlockVersions::_deserializeKnownVersionsEntry(Deserializer *deserializer) {
    const uint32_t clientId = deserializer->readUint32();
    const BlockId blockId(deserializer->readFixedSizeData<BlockId::BINARY_LENGTH>());
    const uint64_t version = deserializer->readUint64();

    return {{clientId, blockId}, version};
};

void KnownBlockVersions::_serializeKnownVersionsEntry(Serializer *serializer, const pair<ClientIdAndBlockId, uint64_t> &entry) {
    serializer->writeUint32(entry.first.clientId);
    serializer->writeFixedSizeData<BlockId::BINARY_LENGTH>(entry.first.blockId.data());
    serializer->writeUint64(entry.second);
}

std::unordered_map<BlockId, uint32_t> KnownBlockVersions::_deserializeLastUpdateClientIds(Deserializer *deserializer) {
    const uint64_t numEntries = deserializer->readUint64();
    std::unordered_map<BlockId, uint32_t> result;
    result.reserve(static_cast<uint64_t>(1.2 * numEntries)); // Reserve for factor 1.2 more, so the file system doesn't immediately have to resize it on the first new block.
    for (uint64_t i = 0 ; i < numEntries; ++i) {
        auto entry = _deserializeLastUpdateClientIdEntry(deserializer);
        result.insert(entry);
    }
    return result;
}

void KnownBlockVersions::_serializeLastUpdateClientIds(Serializer *serializer, const std::unordered_map<BlockId, uint32_t>& lastUpdateClientId) {
    const uint64_t numEntries = lastUpdateClientId.size();
    serializer->writeUint64(numEntries);

    for (const auto &entry : lastUpdateClientId) {
        _serializeLastUpdateClientIdEntry(serializer, entry);
    }
}

pair<BlockId, uint32_t> KnownBlockVersions::_deserializeLastUpdateClientIdEntry(Deserializer *deserializer) {
    const BlockId blockId(deserializer->readFixedSizeData<BlockId::BINARY_LENGTH>());
    const uint32_t clientId = deserializer->readUint32();

    return {blockId, clientId};
};

void KnownBlockVersions::_serializeLastUpdateClientIdEntry(Serializer *serializer, const pair<BlockId, uint32_t> &entry) {
    serializer->writeFixedSizeData<BlockId::BINARY_LENGTH>(entry.first.data());
    serializer->writeUint32(entry.second);
}

uint32_t KnownBlockVersions::myClientId() const {
    return _myClientId;
}

uint64_t KnownBlockVersions::getBlockVersion(uint32_t clientId, const BlockId &blockId) const {
    const unique_lock<mutex> lock(_mutex);
    return _knownVersions.at({clientId, blockId});
}

void KnownBlockVersions::markBlockAsDeleted(const BlockId &blockId) {
    const unique_lock<mutex> lock(_mutex);
    ASSERT(_valid, "Object not valid due to a std::move");
    const auto found = _lastUpdateClientId.find(blockId);
    if (found != _lastUpdateClientId.end() && found->second == CLIENT_ID_FOR_DELETED_BLOCK) {
        return;
    }
    _lastUpdateClientId[blockId] = CLIENT_ID_FOR_DELETED_BLOCK;
    _dirty = true;
}

bool KnownBlockVersions::blockShouldExist(const BlockId &blockId) const {
    const unique_lock<mutex> lock(_mutex);
    ASSERT(_valid, "Object not valid due to a std::move");
    auto found = _lastUpdateClientId.find(blockId);
    if (found == _lastUpdateClientId.end()) {
        // We've never seen (i.e. loaded) this block. So we can't say it has to exist.
        return false;
    }
    // We've seen the block before. If we didn't delete it, the hard-fork core treats its absence as an integrity violation.
    return found->second != CLIENT_ID_FOR_DELETED_BLOCK;
}

std::unordered_set<BlockId> KnownBlockVersions::existingBlocks() const {
    const unique_lock<mutex> lock(_mutex);
    ASSERT(_valid, "Object not valid due to a std::move");
    std::unordered_set<BlockId> result;
    for (const auto &entry : _lastUpdateClientId) {
        if (entry.second != CLIENT_ID_FOR_DELETED_BLOCK) {
            result.insert(entry.first);
        }
    }
    return result;
}

const bf::path &KnownBlockVersions::path() const {
    return _stateFilePath;
}

}
}
