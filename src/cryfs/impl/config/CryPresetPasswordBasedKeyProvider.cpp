#include "CryPresetPasswordBasedKeyProvider.h"

using cpputils::unique_ref;
using cpputils::EncryptionKey;
using cpputils::PasswordBasedKDF;
using cpputils::Data;

namespace cryfs {

CryPresetPasswordBasedKeyProvider::CryPresetPasswordBasedKeyProvider(
    cpputils::SensitivePassword password, unique_ref<PasswordBasedKDF> scrypt,
    unique_ref<PasswordBasedKDF> argon2id)
: _password(std::move(password)), _scrypt(std::move(scrypt)),
  _argon2id(std::move(argon2id)) {}

CryPresetPasswordBasedKeyProvider::CryPresetPasswordBasedKeyProvider(
    std::string password, unique_ref<PasswordBasedKDF> scrypt,
    unique_ref<PasswordBasedKDF> argon2id)
: CryPresetPasswordBasedKeyProvider(
    cpputils::SensitivePassword::FromString(std::move(password)),
    std::move(scrypt), std::move(argon2id)) {}

EncryptionKey CryPresetPasswordBasedKeyProvider::requestKeyForExistingFilesystem(
    ConfigKdf kdf, size_t keySize, const Data& kdfParameters) {
    return _kdf(kdf)->deriveExistingKey(keySize, _password, kdfParameters);
}

CryPresetPasswordBasedKeyProvider::KeyResult CryPresetPasswordBasedKeyProvider::requestKeyForNewFilesystem(
    ConfigKdf kdf, size_t keySize) {
    auto keyResult = _kdf(kdf)->deriveNewKey(keySize, _password);
    return {std::move(keyResult.key), std::move(keyResult.kdfParameters)};
}

PasswordBasedKDF *CryPresetPasswordBasedKeyProvider::_kdf(ConfigKdf kdf) {
    switch (kdf) {
        case ConfigKdf::Scrypt:
            return _scrypt.get();
        case ConfigKdf::Argon2id:
            return _argon2id.get();
    }
    ASSERT(false, "Unknown config KDF");
}

}
