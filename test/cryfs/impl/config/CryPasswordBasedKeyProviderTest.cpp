#include <cryfs/impl/config/CryPasswordBasedKeyProvider.h>
#include <gmock/gmock.h>
#include "../../impl/testutils/MockConsole.h"
#include <cpp-utils/data/DataFixture.h>

using cpputils::unique_ref;
using cpputils::make_unique_ref;
using cpputils::EncryptionKey;
using cpputils::PasswordBasedKDF;
using cpputils::Data;
using cpputils::DataFixture;
using cpputils::SensitivePassword;
using std::shared_ptr;
using std::make_shared;
using std::string;
using cryfs::CryPasswordBasedKeyProvider;
using testing::Return;
using testing::Invoke;
using testing::Eq;
using testing::StrEq;
using testing::NiceMock;

namespace {

class MockCallable {
public:
  MOCK_METHOD(SensitivePassword, call, ());
};

class MockKDF : public PasswordBasedKDF {
public:
  MOCK_METHOD(EncryptionKey, deriveExistingKey, (size_t keySize, const SensitivePassword& password, const Data& kdfParameters), (override));
  MOCK_METHOD(KeyResult, deriveNewKey, (size_t keySize, const SensitivePassword& password), (override));
};

class CryPasswordBasedKeyProviderTest : public ::testing::Test {
public:
  CryPasswordBasedKeyProviderTest()
  : mockConsole(make_shared<NiceMock<MockConsole>>())
  , askPasswordForNewFilesystem()
  , askPasswordForExistingFilesystem()
  , kdf_(make_unique_ref<MockKDF>())
  , kdf(kdf_.get())
  , keyProvider(
      mockConsole,
      [this] () {return askPasswordForExistingFilesystem.call();},
      [this] () {return askPasswordForNewFilesystem.call(); },
      make_unique_ref<NiceMock<MockKDF>>(), std::move(kdf_)) {}

  shared_ptr<NiceMock<MockConsole>> mockConsole;
  MockCallable askPasswordForNewFilesystem;
  MockCallable askPasswordForExistingFilesystem;
  unique_ref<MockKDF> kdf_;
  MockKDF* kdf;

  CryPasswordBasedKeyProvider keyProvider;
};

TEST_F(CryPasswordBasedKeyProviderTest, requestKeyForNewFilesystem) {
  constexpr size_t keySize = 512;
  constexpr const char* password = "mypassword";
  const EncryptionKey key = EncryptionKey::FromString(DataFixture::generate(keySize).ToString());
  const Data kdfParameters = DataFixture::generate(100);

  EXPECT_CALL(askPasswordForNewFilesystem, call()).Times(1).WillOnce(Invoke([] { return SensitivePassword::FromString("mypassword"); }));
  EXPECT_CALL(askPasswordForExistingFilesystem, call()).Times(0);
  EXPECT_CALL(*kdf, deriveNewKey(Eq(keySize), testing::_)).Times(1).WillOnce(Invoke([&] (auto, const SensitivePassword& suppliedPassword) {
    const auto expected = SensitivePassword::FromString(password);
    EXPECT_TRUE(suppliedPassword.equals(expected));
    return PasswordBasedKDF::KeyResult{key, kdfParameters.copy()};
  }));

  auto returned_key = keyProvider.requestKeyForNewFilesystem(
    cryfs::ConfigKdf::Argon2id, keySize);

  EXPECT_EQ(key.ToString(), returned_key.key.ToString());
  EXPECT_EQ(kdfParameters, returned_key.kdfParameters);
}

TEST_F(CryPasswordBasedKeyProviderTest, requestKeyForExistingFilesystem) {
  constexpr size_t keySize = 512;
  constexpr const char* password = "mypassword";
  const EncryptionKey key = EncryptionKey::FromString(DataFixture::generate(keySize).ToString());
  const Data kdfParameters = DataFixture::generate(100);

  EXPECT_CALL(askPasswordForNewFilesystem, call()).Times(0);
  EXPECT_CALL(askPasswordForExistingFilesystem, call()).Times(1).WillOnce(Invoke([] { return SensitivePassword::FromString("mypassword"); }));
  EXPECT_CALL(*kdf, deriveExistingKey(Eq(keySize), testing::_, testing::_)).Times(1).WillOnce(Invoke([&] (auto, const SensitivePassword& suppliedPassword, const auto& kdfParams) {
    const auto expected = SensitivePassword::FromString(password);
    EXPECT_TRUE(suppliedPassword.equals(expected));
    EXPECT_EQ(kdfParameters, kdfParams);
    return key;
  }));

  const EncryptionKey returned_key = keyProvider.requestKeyForExistingFilesystem(
    cryfs::ConfigKdf::Argon2id, keySize, kdfParameters);

  EXPECT_EQ(key.ToString(), returned_key.ToString());
}

TEST_F(CryPasswordBasedKeyProviderTest, ReusesPasswordForMigration) {
  EXPECT_CALL(askPasswordForExistingFilesystem, call())
    .WillOnce(Invoke([] { return SensitivePassword::FromString("mypassword"); }));
  EXPECT_CALL(askPasswordForNewFilesystem, call()).Times(0);
  EXPECT_CALL(*kdf, deriveExistingKey).WillOnce(Invoke(
    [] (size_t size, const SensitivePassword&, const Data&) {
      return EncryptionKey::Null(size);
    }));
  EXPECT_CALL(*kdf, deriveNewKey).WillOnce(Invoke(
    [] (size_t size, const SensitivePassword&) {
      return PasswordBasedKDF::KeyResult{EncryptionKey::Null(size), Data(0)};
    }));

  keyProvider.requestKeyForExistingFilesystem(
    cryfs::ConfigKdf::Argon2id, 32, Data(0));
  keyProvider.requestKeyForNewFilesystem(cryfs::ConfigKdf::Argon2id, 32);
}

}
