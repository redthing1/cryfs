#include "KeyDerivation.h"

#include "RootAuthentication.h"

#include <cpp-utils/data/Data.h>
#include <cpp-utils/data/Serializer.h>
#include <cpp-utils/crypto/symmetric/ciphers.h>
#include <vendor_cryptopp/hkdf.h>
#include <vendor_cryptopp/sha.h>

#include <stdexcept>
#include <string>

namespace cryfs {
namespace formatv2 {
namespace {

const std::string SALT_CONTEXT = "cryfs.formatv2.key-derivation-salt;0";
const std::string ROOT_AUTHENTICATION_KEY_CONTEXT = "cryfs.formatv2.root-authentication-key;0";
const std::string OBJECT_ENCRYPTION_KEY_CONTEXT = "cryfs.formatv2.object-encryption-key;0";

void validateMasterKey(const cpputils::EncryptionKey &masterKey) {
  if (masterKey.binaryLength() != FORMAT_V2_MASTER_KEY_SIZE) {
    throw std::runtime_error("Invalid format-v2 master key size");
  }
}

void validateFilesystemId(const FilesystemId &filesystemId) {
  if (filesystemId == FilesystemId::Null()) {
    throw std::runtime_error("Invalid format-v2 filesystem id");
  }
}

cpputils::Data deriveSalt(const FilesystemId &filesystemId) {
  cpputils::Serializer serializer(
    cpputils::Serializer::StringSize(SALT_CONTEXT)
    + FilesystemId::BINARY_LENGTH);
  serializer.writeString(SALT_CONTEXT);
  serializer.writeFixedSizeData<FilesystemId::BINARY_LENGTH>(filesystemId);
  return serializer.finished();
}

cpputils::EncryptionKey deriveSubkey(
  const cpputils::EncryptionKey &masterKey,
  const FilesystemId &filesystemId,
  const std::string &context,
  size_t keySize) {
  validateMasterKey(masterKey);
  validateFilesystemId(filesystemId);

  const cpputils::Data salt = deriveSalt(filesystemId);
  cpputils::EncryptionKey derivedKey = cpputils::EncryptionKey::Null(keySize);

  CryptoPP::HKDF<CryptoPP::SHA256> hkdf;
  hkdf.DeriveKey(
    static_cast<CryptoPP::byte*>(derivedKey.data()),
    derivedKey.binaryLength(),
    static_cast<const CryptoPP::byte*>(masterKey.data()),
    masterKey.binaryLength(),
    static_cast<const CryptoPP::byte*>(salt.data()),
    salt.size(),
    reinterpret_cast<const CryptoPP::byte*>(context.data()),
    context.size());

  return derivedKey;
}

}

cpputils::EncryptionKey deriveRootAuthenticationKey(
  const cpputils::EncryptionKey &masterKey,
  const FilesystemId &filesystemId) {
  return deriveSubkey(
    masterKey,
    filesystemId,
    ROOT_AUTHENTICATION_KEY_CONTEXT,
    ROOT_AUTHENTICATION_KEY_SIZE);
}

cpputils::EncryptionKey deriveObjectEncryptionKey(
  const cpputils::EncryptionKey &masterKey,
  const FilesystemId &filesystemId) {
  return deriveSubkey(
    masterKey,
    filesystemId,
    OBJECT_ENCRYPTION_KEY_CONTEXT,
    cpputils::AES256_GCM::KEYSIZE);
}

}
}
