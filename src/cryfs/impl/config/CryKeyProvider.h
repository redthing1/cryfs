#pragma once
#ifndef CRYFS_CRYKEYPROVIDER_H
#define CRYFS_CRYKEYPROVIDER_H

#include <cpp-utils/crypto/symmetric/EncryptionKey.h>
#include "crypto/ConfigKdf.h"

namespace cryfs {

class CryKeyProvider {
public:
  virtual ~CryKeyProvider() = default;

  struct KeyResult final {
    cpputils::EncryptionKey key;
    cpputils::Data kdfParameters;
  };

  virtual cpputils::EncryptionKey requestKeyForExistingFilesystem(
    ConfigKdf kdf, size_t keySize, const cpputils::Data& kdfParameters) = 0;
  virtual KeyResult requestKeyForNewFilesystem(
    ConfigKdf kdf, size_t keySize) = 0;
};

}

#endif
