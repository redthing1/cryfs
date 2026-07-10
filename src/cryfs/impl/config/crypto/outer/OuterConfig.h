#pragma once
#ifndef MESSMER_CRYFS_SRC_CONFIG_CRYPTO_OUTER_OUTERCONFIG_H
#define MESSMER_CRYFS_SRC_CONFIG_CRYPTO_OUTER_OUTERCONFIG_H

#include <cpp-utils/data/Data.h>
#include <cpp-utils/data/Serializer.h>
#include <cpp-utils/data/Deserializer.h>
#include "../ConfigKdf.h"

namespace cryfs {
    struct OuterConfig final {
        cpputils::Data kdfParameters;
        cpputils::Data encryptedInnerConfig;
        bool wasInDeprecatedConfigFormat;
        ConfigKdf kdf = ConfigKdf::Scrypt;

        cpputils::Data serialize() const;
        static boost::optional<OuterConfig> deserialize(const cpputils::Data &data);

    private:
        static OuterConfig _deserializeNewFormat(cpputils::Deserializer *deserializer);

        static const std::string SCRYPT_HEADER;
        static const std::string ARGON2ID_HEADER;
#ifndef CRYFS_NO_COMPATIBILITY
        static const std::string OLD_HEADER;
        static OuterConfig _deserializeOldFormat(cpputils::Deserializer *deserializer);
#endif
    };
}

#endif
