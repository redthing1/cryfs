#pragma once
#ifndef MESSMER_CPPUTILS_CRYPTO_KDF_ARGON2IDPARAMETERS_H
#define MESSMER_CPPUTILS_CRYPTO_KDF_ARGON2IDPARAMETERS_H

#include <cpp-utils/data/Data.h>

#include <cstdint>

namespace cpputils {

class Argon2idParameters final {
public:
  Argon2idParameters(Data salt, uint32_t version, uint32_t memoryKiB,
                     uint32_t iterations, uint32_t parallelism);

  const Data &salt() const;
  uint32_t version() const;
  uint32_t memoryKiB() const;
  uint32_t iterations() const;
  uint32_t parallelism() const;

  Data serialize() const;
  static Argon2idParameters deserialize(const Data &data);

private:
  Data _salt;
  uint32_t _version;
  uint32_t _memoryKiB;
  uint32_t _iterations;
  uint32_t _parallelism;
};

bool operator==(const Argon2idParameters &lhs, const Argon2idParameters &rhs);
bool operator!=(const Argon2idParameters &lhs, const Argon2idParameters &rhs);

}

#endif
