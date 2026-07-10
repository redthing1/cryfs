#include "CryConfigEncryptor.h"
#include <cpp-utils/crypto/RandomPadding.h>
#include "ConfigPayload.h"

using std::string;
using cpputils::unique_ref;
using cpputils::make_unique_ref;
using cpputils::Data;
using boost::optional;
using boost::none;
using namespace cpputils::logging;

namespace cryfs {
    constexpr size_t CryConfigEncryptor::OuterKeySize;
    constexpr size_t CryConfigEncryptor::MaxTotalKeySize;
    constexpr size_t CryConfigEncryptor::CurrentKeySize;

    namespace {
        constexpr size_t CURRENT_CONFIG_SIZE = 1024;
    }

    CryConfigEncryptor::CryConfigEncryptor(cpputils::EncryptionKey derivedKey, cpputils::Data kdfParameters)
            : CryConfigEncryptor(std::move(derivedKey), std::move(kdfParameters), ConfigKdf::Scrypt) {
    }

    CryConfigEncryptor::CryConfigEncryptor(cpputils::EncryptionKey derivedKey,
                                           cpputils::Data kdfParameters,
                                           ConfigKdf kdf)
            : _derivedKey(std::move(derivedKey)),
              _kdfParameters(std::move(kdfParameters)), _kdf(kdf) {
        const auto expectedSize = _kdf == ConfigKdf::Argon2id
                                ? CurrentKeySize : MaxTotalKeySize;
        ASSERT(_derivedKey.binaryLength() == expectedSize, "Wrong key size");
    }

    Data CryConfigEncryptor::encrypt(const Data &plaintext, const string &cipherName) const {
        if (_kdf == ConfigKdf::Argon2id) {
            const ConfigPayload payload{cipherName, plaintext.copy()};
            auto padded = cpputils::RandomPadding::add(payload.serialize(), CURRENT_CONFIG_SIZE);
            auto ciphertext = cpputils::XChaCha20Poly1305::encrypt(
                static_cast<const uint8_t*>(padded.data()), padded.size(), _derivedKey);
            return OuterConfig{
                _kdfParameters.copy(), std::move(ciphertext), false, ConfigKdf::Argon2id}.serialize();
        }
        const InnerConfig innerConfig = _innerEncryptor(cipherName)->encrypt(plaintext);
        const Data serializedInnerConfig = innerConfig.serialize();
        const OuterConfig outerConfig = _outerEncryptor()->encrypt(serializedInnerConfig);
        return outerConfig.serialize();
    }

    optional<CryConfigEncryptor::Decrypted> CryConfigEncryptor::decrypt(const Data &data) const {
        auto outerConfig = OuterConfig::deserialize(data);
        if (outerConfig == none) {
            return none;
        }
        if (outerConfig->kdf != _kdf) {
            return none;
        }
        if (_kdf == ConfigKdf::Argon2id) {
            auto padded = cpputils::XChaCha20Poly1305::decrypt(
                static_cast<const uint8_t*>(outerConfig->encryptedInnerConfig.data()),
                outerConfig->encryptedInnerConfig.size(), _derivedKey);
            if (padded == none) {
                return none;
            }
            auto serializedPayload = cpputils::RandomPadding::remove(*padded);
            if (serializedPayload == none) {
                return none;
            }
            auto payload = ConfigPayload::deserialize(*serializedPayload);
            if (payload == none) {
                return none;
            }
            return Decrypted{
                std::move(payload->config), std::move(payload->cipherName),
                outerConfig->wasInDeprecatedConfigFormat};
        }
        auto serializedInnerConfig = _outerEncryptor()->decrypt(*outerConfig);
        if(serializedInnerConfig == none) {
            return none;
        }
        auto innerConfig = InnerConfig::deserialize(*serializedInnerConfig);
        if (innerConfig == none) {
            return none;
        }
        auto plaintext = _innerEncryptor(innerConfig->cipherName)->decrypt(*innerConfig);
        if (plaintext == none) {
            return none;
        }
        return Decrypted{std::move(*plaintext), innerConfig->cipherName, outerConfig->wasInDeprecatedConfigFormat};
    }

    unique_ref<OuterEncryptor> CryConfigEncryptor::_outerEncryptor() const {
        auto outerKey = _derivedKey.take(OuterKeySize);
        return make_unique_ref<OuterEncryptor>(std::move(outerKey), _kdfParameters.copy());
    }

    unique_ref<InnerEncryptor> CryConfigEncryptor::_innerEncryptor(const string &cipherName) const {
        auto innerKey = _derivedKey.drop(OuterKeySize);
        return CryCiphers::find(cipherName).createInnerConfigEncryptor(std::move(innerKey));
    }
}
