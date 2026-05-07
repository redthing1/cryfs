#include "AtomicFile.h"

#include <boost/filesystem.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>

#if !defined(_WIN32)
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace bf = boost::filesystem;

namespace cpputils {
namespace {

constexpr const char *TEMPORARY_FILE_MARKER = ".tmp.";

bf::path parentPath(const bf::path &filepath) {
  const bf::path parent = filepath.parent_path();
  return parent.empty() ? bf::path(".") : parent;
}

bool isNonEmptyDecimalRange(const std::string &value, size_t begin, size_t end) {
  if (begin == end) {
    return false;
  }
  for (size_t pos = begin; pos != end; ++pos) {
    if (value[pos] < '0' || value[pos] > '9') {
      return false;
    }
  }
  return true;
}

#if !defined(_WIN32)
class FileDescriptor final {
public:
  explicit FileDescriptor(int fd): _fd(fd) {
  }

  FileDescriptor(FileDescriptor &&rhs) noexcept: _fd(rhs._fd) {
    rhs._fd = -1;
  }

  ~FileDescriptor() {
    if (_fd != -1) {
      ::close(_fd);
    }
  }

  int get() const {
    return _fd;
  }

  int release() {
    const int result = _fd;
    _fd = -1;
    return result;
  }

private:
  int _fd;
};

struct TemporaryFile final {
  FileDescriptor fd;
  bf::path path;
};

struct ReplacementMetadata final {
  mode_t mode;
  bool fileExists;
};

std::string operationMessage(const char *operation, const bf::path &path) {
  return std::string(operation) + ": " + path.string();
}

[[noreturn]] void throwSystemError(const char *operation, const bf::path &path, int error = errno) {
  throw std::system_error(error, std::generic_category(), operationMessage(operation, path));
}

ReplacementMetadata replacementMetadata(const bf::path &filepath) {
  struct stat status{};
  if (::stat(filepath.string().c_str(), &status) == 0) {
    return ReplacementMetadata{static_cast<mode_t>(status.st_mode & 0777), true};
  }
  if (errno == ENOENT) {
    return ReplacementMetadata{0666, false};
  }
  throwSystemError("stat", filepath);
}

bf::path temporaryPathFor(const bf::path &filepath, unsigned int attempt) {
  const std::string filename = filepath.filename().string();
  const std::string suffix = std::string(TEMPORARY_FILE_MARKER) + std::to_string(::getpid()) + "." + std::to_string(attempt);
  return parentPath(filepath) / (filename + suffix);
}

void removeTemporaryFile(const bf::path &tempPath) {
  boost::system::error_code ignored;
  bf::remove(tempPath, ignored);
}

void removeTemporaryFileDurably(const bf::path &tempPath) {
  if (!removeFileDurably(tempPath)) {
    throw std::runtime_error("Temporary file disappeared before cleanup: " + tempPath.string());
  }
}

TemporaryFile openTemporaryFile(const bf::path &filepath) {
  const ReplacementMetadata metadata = replacementMetadata(filepath);
  for (unsigned int attempt = 0; attempt != 100; ++attempt) {
    const bf::path tempPath = temporaryPathFor(filepath, attempt);
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
    const int fd = ::open(tempPath.string().c_str(), flags, metadata.mode);
    if (fd >= 0) {
      if (metadata.fileExists && ::fchmod(fd, metadata.mode) != 0) {
        const int error = errno;
        ::close(fd);
        removeTemporaryFile(tempPath);
        throwSystemError("fchmod", tempPath, error);
      }
      return TemporaryFile{FileDescriptor(fd), tempPath};
    }
    if (errno != EEXIST) {
      throwSystemError("open", tempPath);
    }
  }
  throw std::runtime_error("Could not create a unique temporary file for " + filepath.string());
}

void writeAll(int fd, const void *data, size_t size, const bf::path &filepath) {
  const auto *current = static_cast<const char*>(data);
  size_t remaining = size;
  while (remaining != 0) {
    const size_t chunkSize = std::min(remaining, static_cast<size_t>(SSIZE_MAX));
    const ssize_t written = ::write(fd, current, chunkSize);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throwSystemError("write", filepath);
    }
    if (written == 0) {
      throw std::runtime_error("write made no progress for " + filepath.string());
    }
    current += written;
    remaining -= static_cast<size_t>(written);
  }
}

void fsyncDescriptor(int fd, const char *operation, const bf::path &path) {
  while (::fsync(fd) != 0) {
    if (errno == EINTR) {
      continue;
    }
    throwSystemError(operation, path);
  }
}

void closeDescriptor(FileDescriptor *fd, const bf::path &path) {
  if (::close(fd->release()) != 0) {
    throwSystemError("close", path);
  }
}

void syncDirectoryImpl(const bf::path &directory) {
  int flags = O_RDONLY;
#if defined(O_DIRECTORY)
  flags |= O_DIRECTORY;
#endif
#if defined(O_CLOEXEC)
  flags |= O_CLOEXEC;
#endif
  FileDescriptor dir(::open(directory.string().c_str(), flags));
  if (dir.get() < 0) {
    throwSystemError("open directory", directory);
  }
  fsyncDescriptor(dir.get(), "fsync directory", directory);
  closeDescriptor(&dir, directory);
}

#endif

}

bool isAtomicFileTemporaryFileFor(const bf::path &filepath, const bf::path &candidatePath) {
  if (parentPath(filepath) != candidatePath.parent_path()) {
    return false;
  }

  const std::string filename = filepath.filename().string();
  const std::string candidateFilename = candidatePath.filename().string();
  const std::string expectedPrefix = filename + TEMPORARY_FILE_MARKER;
  if (candidateFilename.compare(0, expectedPrefix.size(), expectedPrefix) != 0) {
    return false;
  }

  const size_t pidBegin = expectedPrefix.size();
  const size_t separator = candidateFilename.find('.', pidBegin);
  if (separator == std::string::npos) {
    return false;
  }

  return isNonEmptyDecimalRange(candidateFilename, pidBegin, separator)
      && isNonEmptyDecimalRange(candidateFilename, separator + 1, candidateFilename.size());
}

void storeFileAtomically(const bf::path &filepath, const void *data, size_t size) {
#if defined(_WIN32)
  (void)filepath;
  (void)data;
  (void)size;
  throw std::runtime_error("Atomic file replacement is not implemented on Windows");
#else
  TemporaryFile tempFile = openTemporaryFile(filepath);

  try {
    writeAll(tempFile.fd.get(), data, size, tempFile.path);
    fsyncDescriptor(tempFile.fd.get(), "fsync", tempFile.path);
    closeDescriptor(&tempFile.fd, tempFile.path);
    if (::rename(tempFile.path.string().c_str(), filepath.string().c_str()) != 0) {
      throwSystemError("rename", filepath);
    }
    syncDirectory(parentPath(filepath));
  } catch (...) {
    removeTemporaryFile(tempFile.path);
    throw;
  }
#endif
}

bool storeFileAtomicallyIfAbsent(const bf::path &filepath, const void *data, size_t size) {
#if defined(_WIN32)
  (void)filepath;
  (void)data;
  (void)size;
  throw std::runtime_error("Atomic file creation is not implemented on Windows");
#else
  TemporaryFile tempFile = openTemporaryFile(filepath);

  try {
    writeAll(tempFile.fd.get(), data, size, tempFile.path);
    fsyncDescriptor(tempFile.fd.get(), "fsync", tempFile.path);
    closeDescriptor(&tempFile.fd, tempFile.path);

    while (::link(tempFile.path.string().c_str(), filepath.string().c_str()) != 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EEXIST) {
        removeTemporaryFileDurably(tempFile.path);
        return false;
      }
      throwSystemError("link", filepath);
    }
    removeTemporaryFileDurably(tempFile.path);
    return true;
  } catch (...) {
    removeTemporaryFile(tempFile.path);
    throw;
  }
#endif
}

bool removeFileDurably(const bf::path &filepath) {
#if defined(_WIN32)
  throw std::runtime_error("Durable file removal is not implemented on Windows");
#else
  while (::unlink(filepath.string().c_str()) != 0) {
    if (errno == EINTR) {
      continue;
    }
    if (errno == ENOENT) {
      return false;
    }
    throwSystemError("unlink", filepath);
  }
  syncDirectory(parentPath(filepath));
  return true;
#endif
}

bool createDirectoryDurably(const bf::path &directory) {
#if defined(_WIN32)
  throw std::runtime_error("Durable directory creation is not implemented on Windows");
#else
  while (::mkdir(directory.string().c_str(), 0777) != 0) {
    if (errno == EINTR) {
      continue;
    }
    if (errno == EEXIST && bf::is_directory(directory)) {
      return false;
    }
    throwSystemError("mkdir", directory);
  }
  syncDirectory(parentPath(directory));
  return true;
#endif
}

bool createDirectoryTreeDurably(const bf::path &directory) {
  if (directory.empty() || bf::is_directory(directory)) {
    return false;
  }
  if (bf::exists(directory)) {
    return createDirectoryDurably(directory);
  }

  const bf::path parent = directory.parent_path();
  if (!parent.empty() && parent != directory && !bf::exists(parent)) {
    createDirectoryTreeDurably(parent);
  }
  return createDirectoryDurably(directory);
}

bool removeDirectoryIfEmptyDurably(const bf::path &directory) {
#if defined(_WIN32)
  throw std::runtime_error("Durable directory removal is not implemented on Windows");
#else
  while (::rmdir(directory.string().c_str()) != 0) {
    if (errno == EINTR) {
      continue;
    }
    if (errno == ENOENT || errno == ENOTEMPTY || errno == EEXIST) {
      return false;
    }
    throwSystemError("rmdir", directory);
  }
  syncDirectory(parentPath(directory));
  return true;
#endif
}

void syncDirectory(const bf::path &directory) {
#if defined(_WIN32)
  throw std::runtime_error("Durable directory sync is not implemented on Windows");
#else
  syncDirectoryImpl(directory);
#endif
}

}
