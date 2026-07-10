#include "CryPasswordBasedKeyProvider.h"

using std::shared_ptr;
using cpputils::Console;
using cpputils::unique_ref;
using cpputils::EncryptionKey;
using cpputils::PasswordBasedKDF;

namespace cryfs {

CryPasswordBasedKeyProvider::CryPasswordBasedKeyProvider(
    shared_ptr<Console> console,
    std::function<cpputils::SensitivePassword()> askPasswordForExistingFilesystem,
    std::function<cpputils::SensitivePassword()> askPasswordForNewFilesystem,
    unique_ref<PasswordBasedKDF> scrypt,
    unique_ref<PasswordBasedKDF> argon2id)
    : _console(std::move(console)),
      _askPasswordForExistingFilesystem(std::move(askPasswordForExistingFilesystem)),
      _askPasswordForNewFilesystem(std::move(askPasswordForNewFilesystem)),
      _cachedPassword(boost::none), _scrypt(std::move(scrypt)),
      _argon2id(std::move(argon2id)) {}

EncryptionKey CryPasswordBasedKeyProvider::requestKeyForExistingFilesystem(
    ConfigKdf kdf, size_t keySize, const cpputils::Data& kdfParameters) {
  _console->print("Deriving encryption key (this can take some time)...");
  auto key = _kdf(kdf)->deriveExistingKey(
    keySize, _password(false), kdfParameters);
  _console->print("done\n");
  return key;
}

CryKeyProvider::KeyResult CryPasswordBasedKeyProvider::requestKeyForNewFilesystem(
    ConfigKdf kdf, size_t keySize) {
  _console->print("Deriving encryption key (this can take some time)...");
  auto keyResult = _kdf(kdf)->deriveNewKey(keySize, _password(true));
  _console->print("done\n");
  return {std::move(keyResult.key), std::move(keyResult.kdfParameters)};
}

PasswordBasedKDF *CryPasswordBasedKeyProvider::_kdf(ConfigKdf kdf) {
  switch (kdf) {
    case ConfigKdf::Scrypt:
      return _scrypt.get();
    case ConfigKdf::Argon2id:
      return _argon2id.get();
  }
  ASSERT(false, "Unknown config KDF");
}

const cpputils::SensitivePassword &CryPasswordBasedKeyProvider::_password(
    bool forNewFilesystem) {
  if (_cachedPassword == boost::none) {
    _cachedPassword = forNewFilesystem
                    ? _askPasswordForNewFilesystem()
                    : _askPasswordForExistingFilesystem();
  }
  return *_cachedPassword;
}

}
