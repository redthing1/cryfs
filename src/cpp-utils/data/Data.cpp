#include "Data.h"
#include <stdexcept>
#include <vendor_cryptopp/hex.h>

#include <boost/filesystem.hpp>

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

using std::istream;
using std::ofstream;
using std::ifstream;
using std::ios;
using boost::optional;

namespace bf = boost::filesystem;

namespace cpputils {

namespace {

class RemoveOnFailure final {
public:
  explicit RemoveOnFailure(bf::path path)
    : _path(std::move(path)), _enabled(true) {
  }

  ~RemoveOnFailure() {
    if (_enabled) {
      boost::system::error_code error;
      bf::remove(_path, error);
    }
  }

  void release() {
    _enabled = false;
  }

private:
  bf::path _path;
  bool _enabled;
};

bf::path temporaryPathFor(const bf::path &filepath) {
  const auto parent = filepath.parent_path().empty()
                    ? bf::path(".") : filepath.parent_path();
  return parent / bf::unique_path(
    filepath.filename().string() + ".tmp-%%%%-%%%%-%%%%");
}

#ifndef _WIN32
void writeAll(int fd, const void *data, size_t size) {
  const auto *position = static_cast<const uint8_t*>(data);
  size_t remaining = size;
  while (remaining > 0) {
    const auto written = ::write(fd, position, remaining);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      throw std::runtime_error(
        std::string("Error writing temporary file: ") + std::strerror(errno));
    }
    position += written;
    remaining -= static_cast<size_t>(written);
  }
}
#endif

}

void Data::StoreToFileAtomically(const bf::path &filepath) const {
  const auto temporary = temporaryPathFor(filepath);
  RemoveOnFailure cleanup(temporary);

#ifdef _WIN32
  StoreToFile(temporary);
  if (!MoveFileExW(
        temporary.wstring().c_str(), filepath.wstring().c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw std::runtime_error("Could not atomically replace file");
  }
#else
  const int fd = ::open(
    temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0) {
    throw std::runtime_error(
      std::string("Could not create temporary file: ") + std::strerror(errno));
  }
  try {
    writeAll(fd, data(), size());
    if (::fsync(fd) != 0) {
      throw std::runtime_error(
        std::string("Could not sync temporary file: ") + std::strerror(errno));
    }
  } catch (...) {
    ::close(fd);
    throw;
  }
  if (::close(fd) != 0) {
    throw std::runtime_error(
      std::string("Could not close temporary file: ") + std::strerror(errno));
  }
  if (::rename(temporary.c_str(), filepath.c_str()) != 0) {
    throw std::runtime_error(
      std::string("Could not atomically replace file: ") + std::strerror(errno));
  }

  const auto parent = filepath.parent_path().empty()
                    ? bf::path(".") : filepath.parent_path();
  const int directory = ::open(parent.c_str(), O_RDONLY | O_CLOEXEC);
  if (directory >= 0) {
    ::fsync(directory);
    ::close(directory);
  }
#endif

  cleanup.release();
}

optional<Data> Data::LoadFromFile(const bf::path &filepath) {
  ifstream file(filepath.string().c_str(), ios::binary);
  if (!file.good()) {
    return boost::none;
  }
  optional<Data> result(LoadFromStream(file));
  if (!file.good()) {
    throw std::runtime_error("Error reading from file");
  }
  return result;
}

std::streampos Data::_getStreamSize(istream &stream) {
  auto current_pos = stream.tellg();

  //Retrieve length
  stream.seekg(0, stream.end);
  auto endpos = stream.tellg();

  //Restore old position
  stream.seekg(current_pos, stream.beg);

  return endpos - current_pos;
}

Data Data::LoadFromStream(istream &stream, size_t size) {
  Data result(size);
  stream.read(static_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
  return result;
}

Data Data::FromString(const std::string &data, unique_ref<Allocator> allocator) {
  ASSERT(data.size() % 2 == 0, "hex encoded data cannot have odd number of characters");
  Data result(data.size() / 2, std::move(allocator));
  {
    const CryptoPP::StringSource _1(data, true,
      new CryptoPP::HexDecoder(
        new CryptoPP::ArraySink(static_cast<CryptoPP::byte*>(result._data), result.size())
      )
    );
  }
  return result;
}

std::string Data::ToString() const {
  std::string result;
  {
    const CryptoPP::ArraySource _1(static_cast<const CryptoPP::byte*>(_data), _size, true,
      new CryptoPP::HexEncoder(
          new CryptoPP::StringSink(result)
      )
    );
  }
  ASSERT(result.size() == 2 * _size, "Created wrongly sized string");
  return result;
}

}
