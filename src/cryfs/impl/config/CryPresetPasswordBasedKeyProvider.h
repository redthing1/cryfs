#pragma once
#ifndef CRYFS_CRYPRESETPASSWORDFROMCONSOLEKEYPROVIDER_H
#define CRYFS_CRYPRESETPASSWORDFROMCONSOLEKEYPROVIDER_H

#include "CryKeyProvider.h"
#include <cpp-utils/crypto/kdf/PasswordBasedKDF.h>

namespace cryfs {

    class CryPresetPasswordBasedKeyProvider final : public CryKeyProvider {
    public:
        explicit CryPresetPasswordBasedKeyProvider(
          cpputils::SensitivePassword password,
          cpputils::unique_ref<cpputils::PasswordBasedKDF> scrypt,
          cpputils::unique_ref<cpputils::PasswordBasedKDF> argon2id);
        explicit CryPresetPasswordBasedKeyProvider(
          std::string password,
          cpputils::unique_ref<cpputils::PasswordBasedKDF> scrypt,
          cpputils::unique_ref<cpputils::PasswordBasedKDF> argon2id);

        cpputils::EncryptionKey requestKeyForExistingFilesystem(
          ConfigKdf kdf, size_t keySize,
          const cpputils::Data& kdfParameters) override;
        KeyResult requestKeyForNewFilesystem(
          ConfigKdf kdf, size_t keySize) override;

    private:
        cpputils::SensitivePassword _password;
        cpputils::PasswordBasedKDF *_kdf(ConfigKdf kdf);

        cpputils::unique_ref<cpputils::PasswordBasedKDF> _scrypt;
        cpputils::unique_ref<cpputils::PasswordBasedKDF> _argon2id;

        DISALLOW_COPY_AND_ASSIGN(CryPresetPasswordBasedKeyProvider);
    };

}

#endif
