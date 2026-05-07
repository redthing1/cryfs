#include "FormatV2Device.h"

#include <fspp/fs_interface/Dir.h>
#include <fspp/fs_interface/File.h>
#include <fspp/fs_interface/FuseErrnoException.h>
#include <fspp/fs_interface/Node.h>
#include <fspp/fs_interface/OpenFile.h>
#include <fspp/fs_interface/Symlink.h>

#include <boost/filesystem.hpp>
#include <cpp-utils/random/Random.h>
#include <cpp-utils/system/time.h>

#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace bf = boost::filesystem;

namespace cryfs {
namespace formatv2 {
namespace {

constexpr uint64_t STAT_BLOCK_SIZE = 512;
constexpr uint32_t STATFS_BLOCK_SIZE = 4096;
constexpr uint32_t FORMAT_V2_MAX_FILENAME_LENGTH = 255;
constexpr uint32_t PERMISSION_BITS = 07777;
constexpr uint32_t SYMLINK_PERMISSIONS = 0777;

fspp::fuse::FuseErrnoException fuseError(int errorNumber) {
  return fspp::fuse::FuseErrnoException(errorNumber);
}

int staleOpenFileErrno() {
#if defined(ESTALE)
  return ESTALE;
#else
  return EIO;
#endif
}

int64_t checkedStatSize(uint64_t size) {
  if (size > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    throw fuseError(EOVERFLOW);
  }
  return static_cast<int64_t>(size);
}

size_t checkedBufferSize(fspp::num_bytes_t size) {
  if (size.value() < 0) {
    throw fuseError(EINVAL);
  }
  if (static_cast<uint64_t>(size.value())
      > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    throw fuseError(EOVERFLOW);
  }
  return static_cast<size_t>(size.value());
}

uint64_t checkedOffset(fspp::num_bytes_t offset) {
  if (offset.value() < 0) {
    throw fuseError(EINVAL);
  }
  return static_cast<uint64_t>(offset.value());
}

uint64_t blocksForSize(uint64_t size) {
  if (size == 0) {
    return 0;
  }
  return ((size - 1) / STAT_BLOCK_SIZE) + 1;
}

uint64_t statfsBlocksForBytes(uintmax_t bytes) {
  return static_cast<uint64_t>(
    std::min<uintmax_t>(bytes / STATFS_BLOCK_SIZE, std::numeric_limits<uint64_t>::max()));
}

bf::path existingPathForSpaceQuery(bf::path path) {
  if (path.empty()) {
    return ".";
  }
  while (!bf::exists(path)) {
    const bf::path parent = path.parent_path();
    if (parent.empty() || parent == path) {
      return ".";
    }
    path = parent;
  }
  return path;
}

timespec timespecFromTimestamp(const Timestamp &timestamp) {
  timespec result{};
  result.tv_sec = timestamp.seconds;
  result.tv_nsec = timestamp.nanoseconds;
  return result;
}

Timestamp timestampFromTimespec(const timespec &value) {
  if (value.tv_nsec < 0 || value.tv_nsec >= 1000000000L) {
    throw fuseError(EINVAL);
  }
  return Timestamp{
    static_cast<int64_t>(value.tv_sec),
    static_cast<uint32_t>(value.tv_nsec)
  };
}

Timestamp currentTimestamp() {
  return timestampFromTimespec(cpputils::time::now());
}

uint32_t permissionsFromMode(fspp::mode_t mode) {
  return mode.value() & PERMISSION_BITS;
}

ObjectMetadata metadataForNewObject(
  fspp::mode_t mode,
  fspp::uid_t uid,
  fspp::gid_t gid) {
  const Timestamp now = currentTimestamp();
  return ObjectMetadata{
    permissionsFromMode(mode),
    uid.value(),
    gid.value(),
    now,
    now,
    now
  };
}

ObjectMetadata symlinkMetadataForNewObject(
  fspp::uid_t uid,
  fspp::gid_t gid) {
  const Timestamp now = currentTimestamp();
  return ObjectMetadata{
    SYMLINK_PERMISSIONS,
    uid.value(),
    gid.value(),
    now,
    now,
    now
  };
}

ObjectMetadata metadataForParentEntryMutation(ObjectMetadata metadata) {
  const Timestamp now = currentTimestamp();
  metadata.mtime = now;
  metadata.ctime = now;
  return metadata;
}

ObjectMetadata metadataWithMode(ObjectMetadata metadata, fspp::mode_t mode) {
  metadata.permissions = permissionsFromMode(mode);
  metadata.ctime = currentTimestamp();
  return metadata;
}

ObjectMetadata metadataWithOwner(
  ObjectMetadata metadata,
  fspp::uid_t uid,
  fspp::gid_t gid) {
  metadata.uid = uid.value();
  metadata.gid = gid.value();
  metadata.ctime = currentTimestamp();
  return metadata;
}

ObjectMetadata metadataWithTimestamps(
  ObjectMetadata metadata,
  const timespec &lastAccessTime,
  const timespec &lastModificationTime) {
  metadata.atime = timestampFromTimespec(lastAccessTime);
  metadata.mtime = timestampFromTimespec(lastModificationTime);
  metadata.ctime = currentTimestamp();
  return metadata;
}

ObjectMetadata metadataWithContentMutation(ObjectMetadata metadata) {
  const Timestamp now = currentTimestamp();
  metadata.mtime = now;
  metadata.ctime = now;
  return metadata;
}

fspp::mode_t modeFromMetadata(
  const ObjectMetadata &metadata,
  ObjectType type) {
  fspp::mode_t result(metadata.permissions);
  switch (type) {
    case ObjectType::Directory:
      return result.addDirFlag();
    case ObjectType::File:
      return result.addFileFlag();
    case ObjectType::Symlink:
      return result.addSymlinkFlag();
  }
  throw std::runtime_error("Cannot build stat metadata for unknown format-v2 object type");
}

fspp::stat_info statFromMetadata(
  const ObjectMetadata &metadata,
  ObjectType type,
  uint64_t size) {
  return fspp::stat_info{
    1,
    modeFromMetadata(metadata, type),
    fspp::uid_t(metadata.uid),
    fspp::gid_t(metadata.gid),
    fspp::num_bytes_t(checkedStatSize(size)),
    blocksForSize(size),
    timespecFromTimestamp(metadata.atime),
    timespecFromTimestamp(metadata.mtime),
    timespecFromTimestamp(metadata.ctime)
  };
}

ObjectMetadata metadataFromNode(const VolumeNode &node) {
  switch (node.type) {
    case ObjectType::Directory:
      if (node.directory == boost::none) {
        throw fuseError(EIO);
      }
      return node.directory->metadata;
    case ObjectType::File:
      if (node.file == boost::none) {
        throw fuseError(EIO);
      }
      return node.file->metadata;
    case ObjectType::Symlink:
      if (node.symlink == boost::none) {
        throw fuseError(EIO);
      }
      return node.symlink->metadata;
  }
  throw std::runtime_error("Cannot load metadata for unknown format-v2 object type");
}

uint64_t sizeFromNode(const VolumeNode &node) {
  switch (node.type) {
    case ObjectType::Directory:
      return 0;
    case ObjectType::File:
      if (node.file == boost::none) {
        throw fuseError(EIO);
      }
      return node.file->size;
    case ObjectType::Symlink:
      if (node.symlink == boost::none) {
        throw fuseError(EIO);
      }
      return node.symlink->target.size();
  }
  throw std::runtime_error("Cannot load size for unknown format-v2 object type");
}

fspp::stat_info statFromNode(const VolumeNode &node) {
  return statFromMetadata(metadataFromNode(node), node.type, sizeFromNode(node));
}

fspp::Dir::EntryType entryTypeFromObjectType(ObjectType type) {
  switch (type) {
    case ObjectType::Directory:
      return fspp::Dir::EntryType::DIR;
    case ObjectType::File:
      return fspp::Dir::EntryType::FILE;
    case ObjectType::Symlink:
      return fspp::Dir::EntryType::SYMLINK;
  }
  throw std::runtime_error("Cannot map unknown format-v2 object type");
}

bool isValidChildName(const std::string &name) {
  return !name.empty()
      && name != "."
      && name != ".."
      && name.find('/') == std::string::npos
      && name.find('\0') == std::string::npos;
}

bf::path childPath(const bf::path &parentPath, const std::string &name) {
  if (!isValidChildName(name)) {
    throw fuseError(EINVAL);
  }
  return parentPath / name;
}

VolumeNode loadRequiredNode(
  const FormatV2Device *device,
  const bf::path &path) {
  boost::optional<VolumeNode> node = loadVolumeNodeAtPath(
    device->layout(),
    device->filesystemId(),
    device->masterKey(),
    path);
  if (node == boost::none) {
    throw fuseError(ENOENT);
  }
  return *node;
}

DirectoryRecord loadRequiredDirectory(
  const FormatV2Device *device,
  const bf::path &path) {
  VolumeNode node = loadRequiredNode(device, path);
  if (node.type != ObjectType::Directory || node.directory == boost::none) {
    throw fuseError(ENOTDIR);
  }
  return *node.directory;
}

FileRecord loadRequiredFile(
  const FormatV2Device *device,
  const bf::path &path) {
  VolumeNode node = loadRequiredNode(device, path);
  if (node.type != ObjectType::File || node.file == boost::none) {
    throw fuseError(EISDIR);
  }
  return *node.file;
}

FileRecord loadRequiredOpenFile(
  const FormatV2Device *device,
  const bf::path &path,
  const ObjectId &fileId) {
  FileRecord file = loadRequiredFile(device, path);
  if (file.fileId != fileId) {
    throw fuseError(staleOpenFileErrno());
  }
  return file;
}

void ensurePathDoesNotExist(
  const FormatV2Device *device,
  const bf::path &path) {
  const boost::optional<VolumeNode> node = loadVolumeNodeAtPath(
    device->layout(),
    device->filesystemId(),
    device->masterKey(),
    path);
  if (node != boost::none) {
    throw fuseError(EEXIST);
  }
}

void ensurePublished(const RootOpenWithValidatedTreeResult &result) {
  if (result.status != RootOpenStatus::Selected) {
    throw fuseError(EIO);
  }
}

void ensurePublished(
  const boost::optional<RootOpenWithValidatedTreeResult> &result) {
  if (result == boost::none) {
    throw fuseError(ENOENT);
  }
  ensurePublished(*result);
}

void publishMetadataUpdate(
  FormatV2Device *device,
  const bf::path &path,
  ObjectType type,
  ObjectMetadata metadata) {
  switch (type) {
    case ObjectType::Directory:
      ensurePublished(publishVolumeUpdateDirectoryMetadataAtPath(
        device->layout(),
        device->filesystemId(),
        device->masterKey(),
        device->randomGenerator(),
        path,
        std::move(metadata)));
      return;
    case ObjectType::File:
      ensurePublished(publishVolumeUpdateFileMetadataAtPath(
        device->layout(),
        device->filesystemId(),
        device->masterKey(),
        device->randomGenerator(),
        path,
        std::move(metadata)));
      return;
    case ObjectType::Symlink:
      ensurePublished(publishVolumeUpdateSymlinkMetadataAtPath(
        device->layout(),
        device->filesystemId(),
        device->masterKey(),
        device->randomGenerator(),
        path,
        std::move(metadata)));
      return;
  }
  throw std::runtime_error("Cannot update metadata for unknown format-v2 object type");
}

void removeNodeAtPath(
  FormatV2Device *device,
  const bf::path &path,
  ObjectType type) {
  if (path == bf::path("/")) {
    throw fuseError(EBUSY);
  }
  const VolumeNode node = loadRequiredNode(device, path);
  const DirectoryRecord parentDirectory = loadRequiredDirectory(device, path.parent_path());
  const DirectoryMetadata parentMetadata =
    metadataForParentEntryMutation(parentDirectory.metadata);
  switch (type) {
    case ObjectType::Directory:
      if (node.directory == boost::none) {
        throw fuseError(EIO);
      }
      if (!node.directory->entries.empty()) {
        throw fuseError(ENOTEMPTY);
      }
      ensurePublished(publishVolumeRemoveEmptyDirectoryAtPath(
        device->layout(),
        device->filesystemId(),
        device->masterKey(),
        device->randomGenerator(),
        path,
        parentMetadata));
      return;
    case ObjectType::File:
      ensurePublished(publishVolumeRemoveFileAtPath(
        device->layout(),
        device->filesystemId(),
        device->masterKey(),
        device->randomGenerator(),
        path,
        parentMetadata));
      return;
    case ObjectType::Symlink:
      ensurePublished(publishVolumeRemoveSymlinkAtPath(
        device->layout(),
        device->filesystemId(),
        device->masterKey(),
        device->randomGenerator(),
        path,
        parentMetadata));
      return;
  }
  throw std::runtime_error("Cannot remove unknown format-v2 object type");
}

bool canRead(fspp::openflags_t flags) {
  const int accessMode = flags.value() & O_ACCMODE;
  return accessMode == O_RDONLY || accessMode == O_RDWR;
}

bool canWrite(fspp::openflags_t flags) {
  const int accessMode = flags.value() & O_ACCMODE;
  return accessMode == O_WRONLY || accessMode == O_RDWR;
}

class FormatV2Node: public fspp::Node {
public:
  FormatV2Node(
    FormatV2Device *device,
    bf::path path,
    ObjectType type)
  : _device(device),
    _path(std::move(path)),
    _type(type) {
  }

  stat_info stat() const override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    return statFromNode(loadRequiredNode(_device, _path));
  }

  void chmod(fspp::mode_t mode) override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    publishMetadataUpdate(
      _device,
      _path,
      _type,
      metadataWithMode(metadataFromNode(loadRequiredNode(_device, _path)), mode));
  }

  void chown(fspp::uid_t uid, fspp::gid_t gid) override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    publishMetadataUpdate(
      _device,
      _path,
      _type,
      metadataWithOwner(metadataFromNode(loadRequiredNode(_device, _path)), uid, gid));
  }

  void access(int mask) const override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    (void)mask;
  }

  void rename(const bf::path &to) override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    if (_path == bf::path("/")) {
      throw fuseError(EBUSY);
    }
    const ObjectMetadata sourceParentMetadata =
      metadataForParentEntryMutation(loadRequiredDirectory(_device, _path.parent_path()).metadata);
    const ObjectMetadata targetParentMetadata =
      metadataForParentEntryMutation(loadRequiredDirectory(_device, to.parent_path()).metadata);
    ensurePublished(publishVolumeMoveNodeAtPathReplacingTarget(
      _device->layout(),
      _device->filesystemId(),
      _device->masterKey(),
      _device->randomGenerator(),
      _path,
      to,
      sourceParentMetadata,
      targetParentMetadata));
    _path = to;
  }

  void utimens(
    const timespec lastAccessTime,
    const timespec lastModificationTime) override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    publishMetadataUpdate(
      _device,
      _path,
      _type,
      metadataWithTimestamps(
        metadataFromNode(loadRequiredNode(_device, _path)),
        lastAccessTime,
        lastModificationTime));
  }

  void remove() override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    removeNodeAtPath(_device, _path, _type);
  }

protected:
  FormatV2Device *device() const {
    return _device;
  }

  const bf::path &path() const {
    return _path;
  }

private:
  FormatV2Device *_device;
  bf::path _path;
  ObjectType _type;
};

class FormatV2OpenFile final: public fspp::OpenFile {
public:
  FormatV2OpenFile(
    FormatV2Device *device,
    bf::path path,
    ObjectId fileId,
    fspp::openflags_t flags)
  : _device(device),
    _path(std::move(path)),
    _fileId(std::move(fileId)),
    _canRead(canRead(flags)),
    _canWrite(canWrite(flags)) {
    if (!_canRead && !_canWrite) {
      throw fuseError(EINVAL);
    }
  }

  stat_info stat() const override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    const FileRecord file = loadRequiredOpenFile(_device, _path, _fileId);
    return statFromMetadata(file.metadata, ObjectType::File, file.size);
  }

  void truncate(fspp::num_bytes_t size) const override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    if (!_canWrite) {
      throw fuseError(EBADF);
    }
    const FileRecord file = loadRequiredOpenFile(_device, _path, _fileId);
    ensurePublished(publishVolumeTruncateFileAtPath(
      _device->layout(),
      _device->filesystemId(),
      _device->masterKey(),
      _device->randomGenerator(),
      _path,
      metadataWithContentMutation(file.metadata),
      checkedOffset(size)));
  }

  fspp::num_bytes_t read(
    void *buf,
    fspp::num_bytes_t count,
    fspp::num_bytes_t offset) const override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    if (!_canRead) {
      throw fuseError(EBADF);
    }

    const size_t readSize = checkedBufferSize(count);
    const uint64_t readOffset = checkedOffset(offset);
    if (readSize != 0 && buf == nullptr) {
      throw fuseError(EFAULT);
    }
    const FileRecord file = loadRequiredOpenFile(_device, _path, _fileId);
    cpputils::Data data = loadVolumeFileRange(
      _device->layout(),
      _device->filesystemId(),
      _device->masterKey(),
      file,
      readOffset,
      static_cast<uint64_t>(readSize));
    std::memcpy(buf, data.data(), data.size());
    return fspp::num_bytes_t(static_cast<int64_t>(data.size()));
  }

  void write(
    const void *buf,
    fspp::num_bytes_t count,
    fspp::num_bytes_t offset) override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    if (!_canWrite) {
      throw fuseError(EBADF);
    }

    const size_t writeSize = checkedBufferSize(count);
    const uint64_t writeOffset = checkedOffset(offset);
    if (writeSize == 0) {
      return;
    }
    if (buf == nullptr) {
      throw fuseError(EFAULT);
    }

    cpputils::Data payload(writeSize);
    std::memcpy(payload.data(), buf, writeSize);
    const FileRecord file = loadRequiredOpenFile(_device, _path, _fileId);
    ensurePublished(publishVolumeWriteFileAtPath(
      _device->layout(),
      _device->filesystemId(),
      _device->masterKey(),
      _device->randomGenerator(),
      _path,
      metadataWithContentMutation(file.metadata),
      writeOffset,
      std::move(payload)));
  }

  void flush() override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    // There is no dirty open-file buffer; write publication errors surface in write/truncate.
  }

  void fsync() override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    // Successful format-v2 writes publish durably before returning.
  }

  void fdatasync() override {
    [[maybe_unused]] auto operationLock = _device->lockOperation();
    // Successful format-v2 writes publish durably before returning.
  }

private:
  FormatV2Device *_device;
  bf::path _path;
  ObjectId _fileId;
  bool _canRead;
  bool _canWrite;
};

class FormatV2File final: public fspp::File, public FormatV2Node {
public:
  FormatV2File(
    FormatV2Device *device,
    bf::path path)
  : FormatV2Node(
      device,
      std::move(path),
      ObjectType::File) {
  }

  cpputils::unique_ref<fspp::OpenFile> open(fspp::openflags_t flags) override {
    [[maybe_unused]] auto operationLock = device()->lockOperation();
    const FileRecord file = loadRequiredFile(device(), path());
    return cpputils::make_unique_ref<FormatV2OpenFile>(
      device(),
      path(),
      file.fileId,
      flags);
  }

  void truncate(fspp::num_bytes_t size) override {
    [[maybe_unused]] auto operationLock = device()->lockOperation();
    const FileRecord file = loadRequiredFile(device(), path());
    ensurePublished(publishVolumeTruncateFileAtPath(
      device()->layout(),
      device()->filesystemId(),
      device()->masterKey(),
      device()->randomGenerator(),
      path(),
      metadataWithContentMutation(file.metadata),
      checkedOffset(size)));
  }
};

class FormatV2Dir final: public fspp::Dir, public FormatV2Node {
public:
  FormatV2Dir(
    FormatV2Device *device,
    bf::path path)
  : FormatV2Node(
      device,
      std::move(path),
      ObjectType::Directory) {
  }

  cpputils::unique_ref<fspp::OpenFile> createAndOpenFile(
    const std::string &name,
    fspp::mode_t mode,
    fspp::uid_t uid,
    fspp::gid_t gid) override {
    [[maybe_unused]] auto operationLock = device()->lockOperation();
    const bf::path newPath = childPath(path(), name);
    ensurePathDoesNotExist(device(), newPath);
    const DirectoryRecord parentDirectory = loadRequiredDirectory(device(), path());
    ensurePublished(publishVolumeCreateFileAtPath(
      device()->layout(),
      device()->filesystemId(),
      device()->masterKey(),
      device()->randomGenerator(),
      newPath,
      metadataForParentEntryMutation(parentDirectory.metadata),
      metadataForNewObject(mode, uid, gid)));
    const FileRecord file = loadRequiredFile(device(), newPath);
    return cpputils::make_unique_ref<FormatV2OpenFile>(
      device(),
      newPath,
      file.fileId,
      fspp::openflags_t::RDWR());
  }

  void createDir(
    const std::string &name,
    fspp::mode_t mode,
    fspp::uid_t uid,
    fspp::gid_t gid) override {
    [[maybe_unused]] auto operationLock = device()->lockOperation();
    const bf::path newPath = childPath(path(), name);
    ensurePathDoesNotExist(device(), newPath);
    const DirectoryRecord parentDirectory = loadRequiredDirectory(device(), path());
    ensurePublished(publishVolumeCreateDirectoryAtPath(
      device()->layout(),
      device()->filesystemId(),
      device()->masterKey(),
      device()->randomGenerator(),
      newPath,
      metadataForParentEntryMutation(parentDirectory.metadata),
      metadataForNewObject(mode, uid, gid)));
  }

  void createSymlink(
    const std::string &name,
    const bf::path &target,
    fspp::uid_t uid,
    fspp::gid_t gid) override {
    [[maybe_unused]] auto operationLock = device()->lockOperation();
    const bf::path newPath = childPath(path(), name);
    ensurePathDoesNotExist(device(), newPath);
    const DirectoryRecord parentDirectory = loadRequiredDirectory(device(), path());
    ensurePublished(publishVolumeCreateSymlinkAtPath(
      device()->layout(),
      device()->filesystemId(),
      device()->masterKey(),
      device()->randomGenerator(),
      newPath,
      metadataForParentEntryMutation(parentDirectory.metadata),
      symlinkMetadataForNewObject(uid, gid),
      target.string()));
  }

  std::vector<fspp::Dir::Entry> children() override {
    [[maybe_unused]] auto operationLock = device()->lockOperation();
    const DirectoryRecord directory = loadRequiredDirectory(device(), path());
    std::vector<fspp::Dir::Entry> result;
    result.emplace_back(fspp::Dir::EntryType::DIR, ".");
    result.emplace_back(fspp::Dir::EntryType::DIR, "..");
    for (const DirectoryEntry &entry: directory.entries) {
      result.emplace_back(entryTypeFromObjectType(entry.type), entry.name);
    }
    return result;
  }

  void fsync() override {
    [[maybe_unused]] auto operationLock = device()->lockOperation();
    // Directory mutations publish durably before returning.
  }
};

class FormatV2Symlink final: public fspp::Symlink, public FormatV2Node {
public:
  FormatV2Symlink(
    FormatV2Device *device,
    bf::path path)
  : FormatV2Node(
      device,
      std::move(path),
      ObjectType::Symlink) {
  }

  bf::path target() override {
    [[maybe_unused]] auto operationLock = device()->lockOperation();
    VolumeNode node = loadRequiredNode(device(), path());
    if (node.type != ObjectType::Symlink || node.symlink == boost::none) {
      throw fuseError(EINVAL);
    }
    return bf::path(node.symlink->target);
  }
};

cpputils::unique_ref<FormatV2Dir> makeDirectoryNode(
  FormatV2Device *device,
  const bf::path &path,
  const VolumeNode &node) {
  if (node.type != ObjectType::Directory || node.directory == boost::none) {
    throw fuseError(ENOTDIR);
  }
  return cpputils::make_unique_ref<FormatV2Dir>(device, path);
}

cpputils::unique_ref<FormatV2File> makeFileNode(
  FormatV2Device *device,
  const bf::path &path,
  const VolumeNode &node) {
  if (node.type != ObjectType::File || node.file == boost::none) {
    throw fuseError(EISDIR);
  }
  return cpputils::make_unique_ref<FormatV2File>(device, path);
}

cpputils::unique_ref<FormatV2Symlink> makeSymlinkNode(
  FormatV2Device *device,
  const bf::path &path,
  const VolumeNode &node) {
  if (node.type != ObjectType::Symlink || node.symlink == boost::none) {
    throw fuseError(ENOTDIR);
  }
  return cpputils::make_unique_ref<FormatV2Symlink>(device, path);
}

cpputils::unique_ref<fspp::Node> makeNode(
  FormatV2Device *device,
  const bf::path &path,
  const VolumeNode &node) {
  switch (node.type) {
    case ObjectType::Directory:
      return makeDirectoryNode(device, path, node);
    case ObjectType::File:
      return makeFileNode(device, path, node);
    case ObjectType::Symlink:
      return makeSymlinkNode(device, path, node);
  }
  throw std::runtime_error("Cannot build a format-v2 device node with unknown object type");
}

}

FormatV2Device::FormatV2Device(
  VolumeLayout layout,
  FilesystemId filesystemId,
  cpputils::EncryptionKey masterKey)
  : FormatV2Device(
      std::move(layout),
      filesystemId,
      std::move(masterKey),
      cpputils::Random::Csprng()) {
}

FormatV2Device::FormatV2Device(
  VolumeLayout layout,
  FilesystemId filesystemId,
  cpputils::EncryptionKey masterKey,
  cpputils::RandomGenerator *randomGenerator)
: _layout(std::move(layout)),
  _filesystemId(filesystemId),
  _masterKey(std::move(masterKey)),
  _randomGenerator(randomGenerator) {
  if (_randomGenerator == nullptr) {
    throw std::runtime_error("Format-v2 device requires a random generator");
  }
}

FormatV2Device::statvfs FormatV2Device::statfs() {
  [[maybe_unused]] auto operationLock = lockOperation();
  const bf::space_info space = bf::space(existingPathForSpaceQuery(_layout.rootsDirectory));
  const uint64_t totalBlocks = statfsBlocksForBytes(space.capacity);
  const uint64_t freeBlocks = statfsBlocksForBytes(space.free);
  const uint64_t availableBlocks = statfsBlocksForBytes(space.available);
  return statvfs{
    FORMAT_V2_MAX_FILENAME_LENGTH,
    STATFS_BLOCK_SIZE,
    totalBlocks,
    freeBlocks,
    availableBlocks,
    totalBlocks,
    freeBlocks,
    availableBlocks
  };
}

boost::optional<cpputils::unique_ref<fspp::Node>> FormatV2Device::Load(
  const bf::path &path) {
  [[maybe_unused]] auto operationLock = lockOperation();
  boost::optional<VolumeNode> node =
    loadVolumeNodeAtPath(_layout, _filesystemId, _masterKey, path);
  if (node == boost::none) {
    return boost::none;
  }
  return makeNode(this, path, *node);
}

boost::optional<cpputils::unique_ref<fspp::File>> FormatV2Device::LoadFile(
  const bf::path &path) {
  [[maybe_unused]] auto operationLock = lockOperation();
  boost::optional<VolumeNode> node =
    loadVolumeNodeAtPath(_layout, _filesystemId, _masterKey, path);
  if (node == boost::none) {
    return boost::none;
  }
  cpputils::unique_ref<fspp::File> result = makeFileNode(this, path, *node);
  return boost::optional<cpputils::unique_ref<fspp::File>>(std::move(result));
}

boost::optional<cpputils::unique_ref<fspp::Dir>> FormatV2Device::LoadDir(
  const bf::path &path) {
  [[maybe_unused]] auto operationLock = lockOperation();
  boost::optional<VolumeNode> node =
    loadVolumeNodeAtPath(_layout, _filesystemId, _masterKey, path);
  if (node == boost::none) {
    return boost::none;
  }
  cpputils::unique_ref<fspp::Dir> result = makeDirectoryNode(this, path, *node);
  return boost::optional<cpputils::unique_ref<fspp::Dir>>(std::move(result));
}

boost::optional<cpputils::unique_ref<fspp::Symlink>> FormatV2Device::LoadSymlink(
  const bf::path &path) {
  [[maybe_unused]] auto operationLock = lockOperation();
  boost::optional<VolumeNode> node =
    loadVolumeNodeAtPath(_layout, _filesystemId, _masterKey, path);
  if (node == boost::none) {
    return boost::none;
  }
  cpputils::unique_ref<fspp::Symlink> result = makeSymlinkNode(this, path, *node);
  return boost::optional<cpputils::unique_ref<fspp::Symlink>>(std::move(result));
}

void FormatV2Device::sync() const {
  [[maybe_unused]] auto operationLock = lockOperation();
  // Adapter mutations publish all format-v2 state durably before returning.
}

const VolumeLayout &FormatV2Device::layout() const {
  return _layout;
}

FilesystemId FormatV2Device::filesystemId() const {
  return _filesystemId;
}

const cpputils::EncryptionKey &FormatV2Device::masterKey() const {
  return _masterKey;
}

cpputils::RandomGenerator *FormatV2Device::randomGenerator() const {
  return _randomGenerator;
}

std::unique_lock<std::mutex> FormatV2Device::lockOperation() const {
  return std::unique_lock<std::mutex>(_operationMutex);
}

}
}
