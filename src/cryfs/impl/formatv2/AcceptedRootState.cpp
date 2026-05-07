#include "AcceptedRootState.h"

#include <boost/filesystem.hpp>
#include <cpp-utils/data/Deserializer.h>
#include <cpp-utils/data/Serializer.h>
#include <cpp-utils/system/AtomicFile.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace bf = boost::filesystem;

namespace cryfs {
namespace formatv2 {
namespace {

const std::string HEADER = "cryfs.formatv2.accepted-root-state;0";

bool isValidAcceptedRoot(const AcceptedRoot &root) {
  return root.epoch != 0
      && root.rootId != RootId::Null();
}

bool isValidAcceptedRootState(const AcceptedRootState &state) {
  return state.filesystemId != FilesystemId::Null()
      && isValidAcceptedRoot(state.acceptedRoot);
}

size_t serializedSize() {
  return cpputils::Serializer::StringSize(HEADER)
      + FilesystemId::BINARY_LENGTH
      + sizeof(uint64_t)
      + RootId::BINARY_LENGTH;
}

std::runtime_error invalidStateFile(const bf::path &filepath) {
  return std::runtime_error("Invalid format-v2 accepted-root state: " + filepath.string());
}

}

bool operator==(const AcceptedRootState &lhs, const AcceptedRootState &rhs) {
  return lhs.filesystemId == rhs.filesystemId
      && lhs.acceptedRoot == rhs.acceptedRoot;
}

bool operator!=(const AcceptedRootState &lhs, const AcceptedRootState &rhs) {
  return !(lhs == rhs);
}

cpputils::Data serializeAcceptedRootState(const AcceptedRootState &state) {
  if (!isValidAcceptedRootState(state)) {
    throw std::runtime_error("Invalid format-v2 accepted-root state");
  }

  cpputils::Serializer serializer(serializedSize());
  serializer.writeString(HEADER);
  serializer.writeFixedSizeData<FilesystemId::BINARY_LENGTH>(state.filesystemId);
  serializer.writeUint64(state.acceptedRoot.epoch);
  serializer.writeFixedSizeData<RootId::BINARY_LENGTH>(state.acceptedRoot.rootId);
  return serializer.finished();
}

boost::optional<AcceptedRootState> deserializeAcceptedRootState(const cpputils::Data &data) {
  try {
    cpputils::Deserializer deserializer(&data);
    const std::string header = deserializer.readString();
    if (header != HEADER) {
      return boost::none;
    }

    AcceptedRootState state{
      deserializer.readFixedSizeData<FilesystemId::BINARY_LENGTH>(),
      AcceptedRoot{
        deserializer.readUint64(),
        deserializer.readFixedSizeData<RootId::BINARY_LENGTH>()
      }
    };
    deserializer.finished();

    if (!isValidAcceptedRootState(state)) {
      return boost::none;
    }
    return state;
  } catch (const std::exception&) {
    return boost::none;
  }
}

AcceptedRootStateStore::AcceptedRootStateStore(bf::path filepath, FilesystemId filesystemId)
  : _filepath(std::move(filepath)), _filesystemId(filesystemId) {
  if (_filesystemId == FilesystemId::Null()) {
    throw std::runtime_error("Invalid format-v2 filesystem id");
  }
}

boost::optional<AcceptedRoot> AcceptedRootStateStore::load() const {
  if (!bf::exists(_filepath)) {
    return boost::none;
  }

  const boost::optional<cpputils::Data> data = cpputils::Data::LoadFromFile(_filepath);
  if (data == boost::none) {
    throw invalidStateFile(_filepath);
  }

  const boost::optional<AcceptedRootState> state = deserializeAcceptedRootState(*data);
  if (state == boost::none || state->filesystemId != _filesystemId) {
    throw invalidStateFile(_filepath);
  }
  return state->acceptedRoot;
}

void AcceptedRootStateStore::store(const AcceptedRoot &root) const {
  const cpputils::Data data = serializeAcceptedRootState(AcceptedRootState{_filesystemId, root});
  const bf::path parent = _filepath.parent_path();
  if (!parent.empty()) {
    cpputils::createDirectoryTreeDurably(parent);
  }
  data.StoreToFile(_filepath);
}

void AcceptedRootStateStore::advanceTo(const AcceptedRoot &root) const {
  if (!isValidAcceptedRoot(root)) {
    throw std::runtime_error("Invalid format-v2 accepted root");
  }

  const boost::optional<AcceptedRoot> current = load();
  if (current == boost::none) {
    store(root);
    return;
  }

  if (root.epoch < current->epoch) {
    throw std::runtime_error("Refusing to roll back format-v2 accepted root");
  }
  if (root.epoch == current->epoch) {
    if (root.rootId != current->rootId) {
      throw std::runtime_error("Refusing to replace format-v2 accepted root at the same epoch");
    }
    return;
  }

  store(root);
}

}
}
