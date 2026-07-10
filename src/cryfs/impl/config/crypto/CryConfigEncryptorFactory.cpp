#include "CryConfigEncryptorFactory.h"
#include "cryfs/impl/config/crypto/outer/OuterConfig.h"
#include "cryfs/impl/config/CryKeyProvider.h"

using boost::optional;
using boost::none;
using cpputils::unique_ref;
using cpputils::make_unique_ref;
using cpputils::Data;

namespace cryfs {

    optional<unique_ref<CryConfigEncryptor>> CryConfigEncryptorFactory::loadExistingKey(const Data &data,
                                                                                CryKeyProvider *keyProvider) {
        auto outerConfig = OuterConfig::deserialize(data);
        if (outerConfig == none) {
            return none;
        }
        const auto keySize = outerConfig->kdf == ConfigKdf::Argon2id
                           ? CryConfigEncryptor::CurrentKeySize
                           : CryConfigEncryptor::MaxTotalKeySize;
        auto key = keyProvider->requestKeyForExistingFilesystem(
            outerConfig->kdf, keySize, outerConfig->kdfParameters);
        return make_unique_ref<CryConfigEncryptor>(
            std::move(key), std::move(outerConfig->kdfParameters), outerConfig->kdf);
    }

    unique_ref<CryConfigEncryptor> CryConfigEncryptorFactory::deriveNewKey(CryKeyProvider *keyProvider) {
        auto keyResult = keyProvider->requestKeyForNewFilesystem(
            ConfigKdf::Argon2id, CryConfigEncryptor::CurrentKeySize);
        return make_unique_ref<CryConfigEncryptor>(
            std::move(keyResult.key), std::move(keyResult.kdfParameters),
            ConfigKdf::Argon2id);
    }
}
