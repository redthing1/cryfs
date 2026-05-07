#include "CryConfigLoader.h"
#include "CryConfigFile.h"
#include <boost/filesystem.hpp>
#include <cpp-utils/random/Random.h>
#include <gitversion/gitversion.h>
#include <gitversion/VersionCompare.h>
#include "cryfs/impl/localstate/LocalStateDir.h"
#include "cryfs/impl/localstate/LocalStateMetadata.h"
#include "cryfs/impl/CryfsException.h"

namespace bf = boost::filesystem;
using cpputils::Console;
using cpputils::RandomGenerator;
using cpputils::unique_ref;
using cpputils::either;
using boost::optional;
using boost::none;
using std::shared_ptr;
using std::string;

namespace cryfs {

CryConfigLoader::CryConfigLoader(shared_ptr<Console> console, RandomGenerator *keyGenerator, unique_ref<CryKeyProvider> keyProvider, LocalStateDir localStateDir, const optional<string> &cipherFromCommandLine, const boost::optional<uint32_t> &blocksizeBytesFromCommandLine)
    : _creator(std::move(console), keyGenerator, localStateDir), _keyProvider(std::move(keyProvider)),
      _cipherFromCommandLine(cipherFromCommandLine), _blocksizeBytesFromCommandLine(blocksizeBytesFromCommandLine),
      _localStateDir(std::move(localStateDir)) {
}

either<CryConfigFile::LoadError, CryConfigLoader::ConfigLoadResult> CryConfigLoader::_loadConfig(bf::path filename, bool allowReplacedFilesystem, CryConfigFile::Access access) {
  auto config = CryConfigFile::load(std::move(filename), _keyProvider.get(), access);
  if (config.is_left()) {
    return config.left();
  }
  auto oldConfig = *config.right()->config();
  _checkVersion(*config.right()->config());
  if (config.right()->config()->LastOpenedWithVersion() != gitversion::VersionString()) {
    config.right()->config()->SetLastOpenedWithVersion(gitversion::VersionString());
    if (access == CryConfigFile::Access::ReadWrite) {
      config.right()->save();
    }
  }
  _checkCipher(*config.right()->config());
  auto localState = LocalStateMetadata::loadOrGenerate(_localStateDir.forFilesystemId(config.right()->config()->FilesystemId()), config.right()->config()->EncryptionKey(), allowReplacedFilesystem);
  const uint32_t myClientId = localState.myClientId();
  return ConfigLoadResult {std::move(oldConfig), std::move(config.right()), myClientId};
}

void CryConfigLoader::_checkVersion(const CryConfig &config) const {
  if (config.Version() == CryConfig::FilesystemFormatVersion) {
    return;
  }

  if (gitversion::VersionCompare::isOlderThan(CryConfig::FilesystemFormatVersion, config.Version())) {
    throw CryfsException("This filesystem is for CryFS " + config.Version() + " or later. Please update your CryFS version.", ErrorCode::TooNewFilesystemFormat);
  }

  if (gitversion::VersionCompare::isOlderThan(config.Version(), CryConfig::FilesystemFormatVersion)) {
    throw CryfsException("This filesystem is for CryFS " + config.Version() + ". This hard fork does not support in-place filesystem format migration. Export the data with an older compatible tool and create a new filesystem.", ErrorCode::TooOldFilesystemFormat);
  }

  throw CryfsException("Unsupported filesystem format version: " + config.Version(), ErrorCode::InvalidFilesystem);
}

void CryConfigLoader::_checkCipher(const CryConfig &config) const {
  if (_cipherFromCommandLine != none && config.Cipher() != *_cipherFromCommandLine) {
    throw CryfsException(string() + "Filesystem uses " + config.Cipher() + " cipher and not " + *_cipherFromCommandLine + " as specified.", ErrorCode::WrongCipher);
  }
}

either<CryConfigFile::LoadError, CryConfigLoader::ConfigLoadResult> CryConfigLoader::load(bf::path filename, bool allowReplacedFilesystem, CryConfigFile::Access access) {
  return _loadConfig(std::move(filename), allowReplacedFilesystem, access);
}

either<CryConfigFile::LoadError, CryConfigLoader::ConfigLoadResult> CryConfigLoader::loadOrCreate(bf::path filename, bool allowReplacedFilesystem) {
  if (bf::exists(filename)) {
    return _loadConfig(std::move(filename), allowReplacedFilesystem, CryConfigFile::Access::ReadWrite);
  } else {
    return _createConfig(std::move(filename), allowReplacedFilesystem);
  }
}

CryConfigLoader::ConfigLoadResult CryConfigLoader::_createConfig(bf::path filename, bool allowReplacedFilesystem) {
  auto config = _creator.create(_cipherFromCommandLine, _blocksizeBytesFromCommandLine, allowReplacedFilesystem);
  auto result = CryConfigFile::create(std::move(filename), config.config, _keyProvider.get());
  return ConfigLoadResult {std::move(config.config), std::move(result), config.myClientId};
}


}
