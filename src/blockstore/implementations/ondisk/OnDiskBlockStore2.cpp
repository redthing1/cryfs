#include "OnDiskBlockStore2.h"
#include <boost/filesystem.hpp>
#include <cpp-utils/system/diskspace.h>
#include <cpp-utils/system/AtomicFile.h>

#include <vector>

using std::string;
using boost::optional;
using boost::none;
using cpputils::Data;

namespace blockstore {
namespace ondisk {

const string OnDiskBlockStore2::FORMAT_VERSION_HEADER_PREFIX = "cryfs;block;";
const string OnDiskBlockStore2::FORMAT_VERSION_HEADER = OnDiskBlockStore2::FORMAT_VERSION_HEADER_PREFIX + "0";
namespace {
constexpr size_t PREFIX_LENGTH = 3;
constexpr size_t POSTFIX_LENGTH = BlockId::STRING_LENGTH - PREFIX_LENGTH;
constexpr const char* ALLOWED_BLOCKID_CHARACTERS = "0123456789ABCDEF";

bool isValidBlockIdPart(const std::string &value, size_t expectedLength) {
  return value.size() == expectedLength
      && std::string::npos == value.find_first_not_of(ALLOWED_BLOCKID_CHARACTERS);
}

bool isTemporaryBlockFile(const boost::filesystem::path &filepath) {
  const std::string filename = filepath.filename().string();
  if (filename.size() <= POSTFIX_LENGTH) {
    return false;
  }

  const std::string blockIdPostfix = filename.substr(0, POSTFIX_LENGTH);
  if (!isValidBlockIdPart(blockIdPostfix, POSTFIX_LENGTH)) {
    return false;
  }

  return cpputils::isAtomicFileTemporaryFileFor(filepath.parent_path() / blockIdPostfix, filepath);
}
}

boost::filesystem::path OnDiskBlockStore2::_getFilepath(const BlockId &blockId) const {
  const std::string blockIdStr = blockId.ToString();
  return _rootDir / blockIdStr.substr(0, PREFIX_LENGTH) / blockIdStr.substr(PREFIX_LENGTH);
}

Data OnDiskBlockStore2::_checkAndRemoveHeader(const Data &data) {
  if (!_isAcceptedCryfsHeader(data)) {
    if (_isOtherCryfsHeader(data)) {
      throw std::runtime_error("This block is not supported yet. Maybe it was created with a newer version of CryFS?");
    } else {
      throw std::runtime_error("This is not a valid block.");
    }
  }
  Data result(data.size() - formatVersionHeaderSize());
  std::memcpy(result.data(), data.dataOffset(formatVersionHeaderSize()), result.size());
  return result;
}

bool OnDiskBlockStore2::_isAcceptedCryfsHeader(const Data &data) {
  if (data.size() < formatVersionHeaderSize()) {
    return false;
  }
  return 0 == std::memcmp(data.data(), FORMAT_VERSION_HEADER.c_str(), formatVersionHeaderSize());
}

bool OnDiskBlockStore2::_isOtherCryfsHeader(const Data &data) {
  if (data.size() < FORMAT_VERSION_HEADER_PREFIX.size()) {
    return false;
  }
  return 0 == std::memcmp(data.data(), FORMAT_VERSION_HEADER_PREFIX.c_str(), FORMAT_VERSION_HEADER_PREFIX.size());
}

unsigned int OnDiskBlockStore2::formatVersionHeaderSize() {
  return FORMAT_VERSION_HEADER.size() + 1; // +1 because of the null byte
}

OnDiskBlockStore2::OnDiskBlockStore2(const boost::filesystem::path& path)
    : _rootDir(path) {
  _removeStaleTemporaryBlockFiles();
}

bool OnDiskBlockStore2::tryCreate(const BlockId &blockId, const Data &data) {
  auto filepath = _getFilepath(blockId);
  cpputils::createDirectoryDurably(filepath.parent_path()); // TODO Instead create all of them once at fs creation time?

  const Data fileContent = _addHeader(data);
  return cpputils::storeFileAtomicallyIfAbsent(filepath, fileContent.data(), fileContent.size());
}

bool OnDiskBlockStore2::remove(const BlockId &blockId) {
  auto filepath = _getFilepath(blockId);
  if (!boost::filesystem::is_regular_file(filepath)) { // TODO Is this branch necessary?
    return false;
  }
  const bool removed = cpputils::removeFileDurably(filepath);
  if (!removed) {
    cpputils::logging::LOG(cpputils::logging::ERR, "Couldn't find block {} to remove", blockId.ToString());
    return false;
  }
  if (boost::filesystem::is_empty(filepath.parent_path())) {
    cpputils::removeDirectoryIfEmptyDurably(filepath.parent_path());
  }
  return true;
}

optional<Data> OnDiskBlockStore2::load(const BlockId &blockId) const {
  auto fileContent = Data::LoadFromFile(_getFilepath(blockId));
  if (fileContent == none) {
    return boost::none;
  }
  return _checkAndRemoveHeader(*fileContent);
}

void OnDiskBlockStore2::store(const BlockId &blockId, const Data &data) {
  const Data fileContent = _addHeader(data);
  auto filepath = _getFilepath(blockId);
  cpputils::createDirectoryDurably(filepath.parent_path()); // TODO Instead create all of them once at fs creation time?
  fileContent.StoreToFile(filepath);
}

void OnDiskBlockStore2::_removeStaleTemporaryBlockFiles() {
  if (!boost::filesystem::is_directory(_rootDir)) {
    return;
  }

  for (auto prefixDir = boost::filesystem::directory_iterator(_rootDir); prefixDir != boost::filesystem::directory_iterator(); ++prefixDir) {
    if (!boost::filesystem::is_directory(prefixDir->path())) {
      continue;
    }

    const std::string blockIdPrefix = prefixDir->path().filename().string();
    if (!isValidBlockIdPart(blockIdPrefix, PREFIX_LENGTH)) {
      continue;
    }

    std::vector<boost::filesystem::path> temporaryFiles;
    for (auto block = boost::filesystem::directory_iterator(prefixDir->path()); block != boost::filesystem::directory_iterator(); ++block) {
      if (boost::filesystem::is_regular_file(block->path()) && isTemporaryBlockFile(block->path())) {
        temporaryFiles.push_back(block->path());
      }
    }

    for (const auto &temporaryFile: temporaryFiles) {
      cpputils::removeFileDurably(temporaryFile);
    }
    if (!temporaryFiles.empty() && boost::filesystem::is_directory(prefixDir->path()) && boost::filesystem::is_empty(prefixDir->path())) {
      cpputils::removeDirectoryIfEmptyDurably(prefixDir->path());
    }
  }
}

Data OnDiskBlockStore2::_addHeader(const Data &data) {
  Data fileContent(formatVersionHeaderSize() + data.size());
  std::memcpy(fileContent.data(), FORMAT_VERSION_HEADER.c_str(), formatVersionHeaderSize());
  std::memcpy(fileContent.dataOffset(formatVersionHeaderSize()), data.data(), data.size());
  return fileContent;
}

uint64_t OnDiskBlockStore2::numBlocks() const {
  uint64_t count = 0;
  forEachBlock([&count] (const BlockId&) {
    ++count;
  });
  return count;
}

uint64_t OnDiskBlockStore2::estimateNumFreeBytes() const {
	return cpputils::free_disk_space_in_bytes(_rootDir);
}

uint64_t OnDiskBlockStore2::blockSizeFromPhysicalBlockSize(uint64_t blockSize) const {
  if(blockSize <= formatVersionHeaderSize()) {
    return 0;
  }
  return blockSize - formatVersionHeaderSize();
}

void OnDiskBlockStore2::forEachBlock(std::function<void (const BlockId &)> callback) const {
  for (auto prefixDir = boost::filesystem::directory_iterator(_rootDir); prefixDir != boost::filesystem::directory_iterator(); ++prefixDir) {
    if (!boost::filesystem::is_directory(prefixDir->path())) {
      continue;
    }

    const std::string blockIdPrefix = prefixDir->path().filename().string();
    if (!isValidBlockIdPart(blockIdPrefix, PREFIX_LENGTH)) {
      // directory has wrong length or an invalid character
      continue;
    }

    for (auto block = boost::filesystem::directory_iterator(prefixDir->path()); block != boost::filesystem::directory_iterator(); ++block) {
      if (!boost::filesystem::is_regular_file(block->path())) {
        continue;
      }

      const std::string blockIdPostfix = block->path().filename().string();
      if (!isValidBlockIdPart(blockIdPostfix, POSTFIX_LENGTH)) {
        // filename has wrong length or an invalid character
        continue;
      }

      callback(BlockId::FromString(blockIdPrefix + blockIdPostfix));
    }
  }
}

void OnDiskBlockStore2::flush() {
}

void OnDiskBlockStore2::sync() {
  for (auto prefixDir = boost::filesystem::directory_iterator(_rootDir); prefixDir != boost::filesystem::directory_iterator(); ++prefixDir) {
    if (boost::filesystem::is_directory(prefixDir->path())) {
      cpputils::syncDirectory(prefixDir->path());
    }
  }
  cpputils::syncDirectory(_rootDir);
}

}
}
