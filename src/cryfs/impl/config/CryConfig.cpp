#include "CryConfig.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <sstream>
#include <cpp-utils/random/Random.h>


using boost::property_tree::ptree;
using boost::optional;
using boost::none;
using std::string;
using std::stringstream;
using cpputils::Data;
using cpputils::Random;

namespace cryfs {

constexpr const char* CryConfig::FilesystemFormatVersion;

CryConfig::CryConfig()
: _rootBlob("")
, _encKey(cpputils::EncryptionKey::Null(0))
, _cipher("")
, _version("")
, _createdWithVersion("")
, _lastOpenedWithVersion("")
, _blocksizeBytes(0)
, _filesystemId(FilesystemID::Null())
{
}

CryConfig CryConfig::load(const Data &data) {
  stringstream stream;
  data.StoreToStream(stream);

  ptree pt;
  read_json(stream, pt);

  CryConfig cfg;
  cfg._rootBlob = pt.get<string>("cryfs.rootblob");
  cfg._encKey = cpputils::EncryptionKey::FromString(pt.get<string>("cryfs.key"));
  cfg._cipher = pt.get<string>("cryfs.cipher");
  cfg._version = pt.get<string>("cryfs.version", "0.8"); // CryFS 0.8 didn't specify this field, so if the field doesn't exist, it's 0.8.
  cfg._createdWithVersion = pt.get<string>("cryfs.createdWithVersion", cfg._version); // In CryFS <= 0.9.2, we didn't have this field, but also didn't update cryfs.version, so we can use this field instead.
  cfg._lastOpenedWithVersion = pt.get<string>("cryfs.lastOpenedWithVersion", cfg._version); // In CryFS <= 0.9.8, we didn't have this field, but used the cryfs.version field for this purpose.
  cfg._blocksizeBytes = pt.get<uint64_t>("cryfs.blocksizeBytes", 32832); // CryFS <= 0.9.2 used a 32KB block size which was this physical block size.
  optional<string> filesystemIdOpt = pt.get_optional<string>("cryfs.filesystemId");
  if (filesystemIdOpt == none) {
    cfg._filesystemId = Random::Csprng()->getFixedSize<FilesystemID::BINARY_LENGTH>();
  } else {
    cfg._filesystemId = FilesystemID::FromString(*filesystemIdOpt);
  }

  return cfg;
}

Data CryConfig::save() const {
  ptree pt;

  pt.put<string>("cryfs.rootblob", _rootBlob);
  pt.put<string>("cryfs.key", _encKey.ToString());
  pt.put<string>("cryfs.cipher", _cipher);
  pt.put<string>("cryfs.version", _version);
  pt.put<string>("cryfs.createdWithVersion", _createdWithVersion);
  pt.put<string>("cryfs.lastOpenedWithVersion", _lastOpenedWithVersion);
  pt.put<uint64_t>("cryfs.blocksizeBytes", _blocksizeBytes);
  pt.put<string>("cryfs.filesystemId", _filesystemId.ToString());
  stringstream stream;
  write_json(stream, pt);
  return Data::LoadFromStream(stream);
}

const std::string &CryConfig::RootBlob() const {
  return _rootBlob;
}

void CryConfig::SetRootBlob(std::string value) {
  _rootBlob = std::move(value);
}

const cpputils::EncryptionKey &CryConfig::EncryptionKey() const {
  return _encKey;
}

void CryConfig::SetEncryptionKey(cpputils::EncryptionKey value) {
  _encKey = std::move(value);
}

const std::string &CryConfig::Cipher() const {
  return _cipher;
};

void CryConfig::SetCipher(std::string value) {
  _cipher = std::move(value);
}

const std::string &CryConfig::Version() const {
  return _version;
}

void CryConfig::SetVersion(std::string value) {
  _version = std::move(value);
}

const std::string &CryConfig::LastOpenedWithVersion() const {
  return _lastOpenedWithVersion;
}

const std::string &CryConfig::CreatedWithVersion() const {
  return _createdWithVersion;
}

void CryConfig::SetCreatedWithVersion(std::string value) {
  _createdWithVersion = std::move(value);
}

void CryConfig::SetLastOpenedWithVersion(const std::string &value) {
  _lastOpenedWithVersion = value;
}

uint64_t CryConfig::BlocksizeBytes() const {
  return _blocksizeBytes;
}

void CryConfig::SetBlocksizeBytes(uint64_t value) {
  _blocksizeBytes = value;
}

const CryConfig::FilesystemID &CryConfig::FilesystemId() const {
  return _filesystemId;
}

void CryConfig::SetFilesystemId(FilesystemID value) {
  _filesystemId = value;
}

}
