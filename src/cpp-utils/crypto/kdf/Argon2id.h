#pragma once
#ifndef MESSMER_CPPUTILS_CRYPTO_KDF_ARGON2ID_H
#define MESSMER_CPPUTILS_CRYPTO_KDF_ARGON2ID_H

#include "Argon2idParameters.h"
#include "PasswordBasedKDF.h"

#include <cpp-utils/macros.h>

#include <cstddef>
#include <cstdint>

namespace cpputils {

struct Argon2idSettings final {
  size_t saltLength;
  uint32_t memoryKiB;
  uint32_t iterations;
  uint32_t parallelism;
};

class Argon2id final : public PasswordBasedKDF {
public:
  static constexpr uint32_t VERSION = 0x13;
  static constexpr Argon2idSettings DefaultSettings{
    16, 1024 * 1024, 3, 4};
  static constexpr Argon2idSettings TestSettings{
    16, 1024, 2, 1};

  explicit Argon2id(const Argon2idSettings &settingsForNewKeys);

  EncryptionKey deriveExistingKey(
    size_t keySize, const SensitivePassword &password,
    const Data &kdfParameters) override;
  KeyResult deriveNewKey(
    size_t keySize, const SensitivePassword &password) override;

private:
  Argon2idSettings _settingsForNewKeys;

  DISALLOW_COPY_AND_ASSIGN(Argon2id);
};

}

#endif
