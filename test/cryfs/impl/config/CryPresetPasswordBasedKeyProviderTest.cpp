#include <cryfs/impl/config/CryPresetPasswordBasedKeyProvider.h>
#include <gmock/gmock.h>
#include "../../impl/testutils/MockConsole.h"
#include <cpp-utils/data/DataFixture.h>

using cpputils::make_unique_ref;
using cpputils::EncryptionKey;
using cpputils::PasswordBasedKDF;
using cpputils::Data;
using cpputils::DataFixture;
using cpputils::SensitivePassword;
using std::string;
using cryfs::CryPresetPasswordBasedKeyProvider;
using testing::Invoke;
using testing::Eq;
using testing::StrEq;

namespace {

class MockKDF : public PasswordBasedKDF {
public:
    MOCK_METHOD(EncryptionKey, deriveExistingKey, (size_t keySize, const SensitivePassword& password, const Data& kdfParameters), (override));
    MOCK_METHOD(KeyResult, deriveNewKey, (size_t keySize, const SensitivePassword& password), (override));
};

TEST(CryPresetPasswordBasedKeyProviderTest, requestKeyForNewFilesystem) {
    constexpr size_t keySize = 512;
    constexpr const char* password = "mypassword";
    const EncryptionKey key = EncryptionKey::FromString(DataFixture::generate(keySize).ToString());
    auto kdf = make_unique_ref<MockKDF>();
    const Data kdfParameters = DataFixture::generate(100);

    EXPECT_CALL(*kdf, deriveNewKey(Eq(keySize), testing::_)).Times(1).WillOnce(Invoke([&] (auto, const SensitivePassword& suppliedPassword) {
        const auto expected = SensitivePassword::FromString(password);
        EXPECT_TRUE(suppliedPassword.equals(expected));
        return PasswordBasedKDF::KeyResult{key, kdfParameters.copy()};
    }));

    CryPresetPasswordBasedKeyProvider keyProvider(
      password, make_unique_ref<testing::NiceMock<MockKDF>>(), std::move(kdf));
    auto returned_key = keyProvider.requestKeyForNewFilesystem(
      cryfs::ConfigKdf::Argon2id, keySize);

    EXPECT_EQ(key.ToString(), returned_key.key.ToString());
    EXPECT_EQ(kdfParameters, returned_key.kdfParameters);
}

TEST(CryPresetPasswordBasedKeyProviderTest, requestKeyForExistingFilesystem) {
    constexpr size_t keySize = 512;
    constexpr const char* password = "mypassword";
    const EncryptionKey key = EncryptionKey::FromString(DataFixture::generate(keySize).ToString());
    auto kdf = make_unique_ref<MockKDF>();
    const Data kdfParameters = DataFixture::generate(100);

    EXPECT_CALL(*kdf, deriveExistingKey(Eq(keySize), testing::_, testing::_)).Times(1).WillOnce(Invoke([&] (auto, const SensitivePassword& suppliedPassword, const auto& kdfParams) {
        const auto expected = SensitivePassword::FromString(password);
        EXPECT_TRUE(suppliedPassword.equals(expected));
        EXPECT_EQ(kdfParameters, kdfParams);
        return key;
    }));

    CryPresetPasswordBasedKeyProvider keyProvider(
      password, make_unique_ref<testing::NiceMock<MockKDF>>(), std::move(kdf));
    const EncryptionKey returned_key = keyProvider.requestKeyForExistingFilesystem(
      cryfs::ConfigKdf::Argon2id, keySize, kdfParameters);

    EXPECT_EQ(key.ToString(), returned_key.ToString());
}

}
