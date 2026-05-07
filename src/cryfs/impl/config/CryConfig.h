#pragma once
#ifndef MESSMER_CRYFS_SRC_CONFIG_CRYCONFIG_H_
#define MESSMER_CRYFS_SRC_CONFIG_CRYCONFIG_H_

#include <cstdint>
#include <string>

#include <cpp-utils/data/FixedSizeData.h>
#include <cpp-utils/data/Data.h>
#include <cpp-utils/crypto/symmetric/EncryptionKey.h>

namespace cryfs {

class CryConfig final {
public:
  static constexpr const char* FilesystemFormatVersion = "0.10";

  //TODO No default constructor, pass in config values instead!
  CryConfig();
  CryConfig(CryConfig &&rhs) = default;
  CryConfig(const CryConfig &rhs) = default;

  const std::string &RootBlob() const;
  void SetRootBlob(std::string value);

  const cpputils::EncryptionKey &EncryptionKey() const;
  void SetEncryptionKey(cpputils::EncryptionKey value);

  const std::string &Cipher() const;
  void SetCipher(std::string value);

  const std::string &Version() const;
  void SetVersion(std::string value);

  const std::string &CreatedWithVersion() const;
  void SetCreatedWithVersion(std::string value);

  const std::string &LastOpenedWithVersion() const;
  void SetLastOpenedWithVersion(const std::string &value);

  uint64_t BlocksizeBytes() const;
  void SetBlocksizeBytes(uint64_t value);

  using FilesystemID = cpputils::FixedSizeData<16>;
  const FilesystemID &FilesystemId() const;
  void SetFilesystemId(FilesystemID value);

  static CryConfig load(const cpputils::Data &data);
  cpputils::Data save() const;

private:
  std::string _rootBlob;
  cpputils::EncryptionKey _encKey;
  std::string _cipher;
  std::string _version;
  std::string _createdWithVersion;
  std::string _lastOpenedWithVersion;
  uint64_t _blocksizeBytes;
  FilesystemID _filesystemId;

  CryConfig &operator=(const CryConfig &rhs) = delete;
};

}

#endif
