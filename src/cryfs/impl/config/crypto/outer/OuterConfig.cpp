#include "OuterConfig.h"
#include <cpp-utils/crypto/kdf/SCryptParameters.h>

using std::string;
using std::exception;
using cpputils::Data;
using cpputils::Serializer;
using cpputils::Deserializer;
using cpputils::SCryptParameters;
using boost::optional;
using boost::none;
using namespace cpputils::logging;

namespace cryfs {
#ifndef CRYFS_NO_COMPATIBILITY
    const string OuterConfig::OLD_HEADER = "cryfs.config;0;scrypt";
#endif
    const string OuterConfig::SCRYPT_HEADER = "cryfs.config;1;scrypt";
    const string OuterConfig::ARGON2ID_HEADER = "cryfs.config;2;argon2id";

    Data OuterConfig::serialize() const {
        try {
            const auto &header = kdf == ConfigKdf::Argon2id
                               ? ARGON2ID_HEADER : SCRYPT_HEADER;
            Serializer serializer(Serializer::StringSize(header)
                                  + Serializer::DataSize(kdfParameters)
                                  + encryptedInnerConfig.size());
            serializer.writeString(header);
            serializer.writeData(kdfParameters);
            serializer.writeTailData(encryptedInnerConfig);
            return serializer.finished();
        } catch (const exception &e) {
            LOG(ERR, "Error serializing CryConfigEncryptor: {}", e.what());
            throw; // This is a programming logic error. Pass through exception.
        }
    }

    optional<OuterConfig> OuterConfig::deserialize(const Data &data) {
        Deserializer deserializer(&data);
        try {
            const string header = deserializer.readString();
#ifndef CRYFS_NO_COMPATIBILITY
            if (header == OLD_HEADER) {
                return _deserializeOldFormat(&deserializer);
            }
#endif
            if (header == SCRYPT_HEADER) {
                auto result = _deserializeNewFormat(&deserializer);
                result.wasInDeprecatedConfigFormat = true;
                result.kdf = ConfigKdf::Scrypt;
                return result;
            } else if (header == ARGON2ID_HEADER) {
                auto result = _deserializeNewFormat(&deserializer);
                result.kdf = ConfigKdf::Argon2id;
                return result;
            } else {
                throw std::runtime_error("Invalid header");
            }
        } catch (const exception &e) {
            LOG(ERR, "Error deserializing outer configuration: {}", e.what());
            return none; // This can be caused by invalid input data and does not have to be a programming error. Don't throw exception.
        }
    }

#ifndef CRYFS_NO_COMPATIBILITY
    OuterConfig OuterConfig::_deserializeOldFormat(Deserializer *deserializer) {
        auto kdfParameters = SCryptParameters::deserializeOldFormat(deserializer);
        auto kdfParametersSerialized = kdfParameters.serialize();
        auto encryptedInnerConfig = deserializer->readTailData();
        deserializer->finished();
        return OuterConfig {std::move(kdfParametersSerialized), std::move(encryptedInnerConfig), true, ConfigKdf::Scrypt};
    }
#endif

    OuterConfig OuterConfig::_deserializeNewFormat(Deserializer *deserializer) {
        auto kdfParameters = deserializer->readData();
        auto encryptedInnerConfig = deserializer->readTailData();
        deserializer->finished();
        return OuterConfig {std::move(kdfParameters), std::move(encryptedInnerConfig), false};
    }
}
