#pragma once
#ifndef CRYFS_CRYPASSWORDFROMCONSOLEKEYPROVIDER_H
#define CRYFS_CRYPASSWORDFROMCONSOLEKEYPROVIDER_H

#include "CryKeyProvider.h"
#include <functional>
#include <cpp-utils/crypto/kdf/PasswordBasedKDF.h>
#include <cpp-utils/io/Console.h>

namespace cryfs {

// TODO Remove duplication with CryPresetPasswordBasedKeyProvider
class CryPasswordBasedKeyProvider final : public CryKeyProvider {
public:
  explicit CryPasswordBasedKeyProvider(
    std::shared_ptr<cpputils::Console> console,
    std::function<cpputils::SensitivePassword()> askPasswordForExistingFilesystem,
    std::function<cpputils::SensitivePassword()> askPasswordForNewFilesystem,
    cpputils::unique_ref<cpputils::PasswordBasedKDF> scrypt,
    cpputils::unique_ref<cpputils::PasswordBasedKDF> argon2id);

  cpputils::EncryptionKey requestKeyForExistingFilesystem(
    ConfigKdf kdf, size_t keySize,
    const cpputils::Data& kdfParameters) override;
  KeyResult requestKeyForNewFilesystem(ConfigKdf kdf, size_t keySize) override;

private:
  std::shared_ptr<cpputils::Console> _console;
  std::function<cpputils::SensitivePassword()> _askPasswordForExistingFilesystem;
  std::function<cpputils::SensitivePassword()> _askPasswordForNewFilesystem;
  cpputils::PasswordBasedKDF *_kdf(ConfigKdf kdf);
  const cpputils::SensitivePassword &_password(bool forNewFilesystem);

  boost::optional<cpputils::SensitivePassword> _cachedPassword;
  cpputils::unique_ref<cpputils::PasswordBasedKDF> _scrypt;
  cpputils::unique_ref<cpputils::PasswordBasedKDF> _argon2id;

  DISALLOW_COPY_AND_ASSIGN(CryPasswordBasedKeyProvider);
};

}

#endif
