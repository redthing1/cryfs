#pragma once
#ifndef MESSMER_CRYFS_SRC_CONFIG_CRYPTO_CRYCONFIGENCRYPTORFACTORY_H
#define MESSMER_CRYFS_SRC_CONFIG_CRYPTO_CRYCONFIGENCRYPTORFACTORY_H

#include "CryConfigEncryptor.h"
#include <cpp-utils/pointer/unique_ref.h>

namespace cryfs {
    class CryKeyProvider;

    class CryConfigEncryptorFactory final {
    public:
        static cpputils::unique_ref<CryConfigEncryptor> deriveNewKey(CryKeyProvider *keyProvider);

        static boost::optional<cpputils::unique_ref<CryConfigEncryptor>> loadExistingKey(const cpputils::Data &ciphertext,
                                                                                         CryKeyProvider *keyProvider);
    };
}

#endif
