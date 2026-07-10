#include <cpp-utils/crypto/kdf/Argon2id.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using namespace cpputils;

namespace {

SensitivePassword password(const char *value) {
  return SensitivePassword::FromString(value);
}

bool keysEqual(const EncryptionKey &lhs, const EncryptionKey &rhs) {
  return lhs.binaryLength() == rhs.binaryLength()
      && std::memcmp(lhs.data(), rhs.data(), lhs.binaryLength()) == 0;
}

}

TEST(Argon2idTest, ReproducesGeneratedKey) {
  Argon2id argon2id(Argon2id::TestSettings);
  const auto generated = argon2id.deriveNewKey(32, password("mypassword"));

  const auto reproduced = argon2id.deriveExistingKey(
    32, password("mypassword"), generated.kdfParameters);

  EXPECT_TRUE(keysEqual(generated.key, reproduced));
}

TEST(Argon2idTest, DifferentPasswordProducesDifferentKey) {
  Argon2id argon2id(Argon2id::TestSettings);
  const auto generated = argon2id.deriveNewKey(32, password("mypassword"));

  const auto reproduced = argon2id.deriveExistingKey(
    32, password("anotherpassword"), generated.kdfParameters);

  EXPECT_FALSE(keysEqual(generated.key, reproduced));
}

TEST(Argon2idTest, UsesConfiguredSettings) {
  Argon2id argon2id(Argon2id::TestSettings);
  const auto generated = argon2id.deriveNewKey(32, password("mypassword"));
  const auto parameters =
    Argon2idParameters::deserialize(generated.kdfParameters);

  EXPECT_EQ(Argon2id::VERSION, parameters.version());
  EXPECT_EQ(Argon2id::TestSettings.saltLength, parameters.salt().size());
  EXPECT_EQ(Argon2id::TestSettings.memoryKiB, parameters.memoryKiB());
  EXPECT_EQ(Argon2id::TestSettings.iterations, parameters.iterations());
  EXPECT_EQ(Argon2id::TestSettings.parallelism, parameters.parallelism());
}

TEST(Argon2idTest, DefaultSettingsMatchSecurityPreset) {
  EXPECT_EQ(16u, Argon2id::DefaultSettings.saltLength);
  EXPECT_EQ(1048576u, Argon2id::DefaultSettings.memoryKiB);
  EXPECT_EQ(3u, Argon2id::DefaultSettings.iterations);
  EXPECT_EQ(4u, Argon2id::DefaultSettings.parallelism);
}

TEST(Argon2idTest, RejectsUnsupportedVersion) {
  Argon2id argon2id(Argon2id::TestSettings);
  const Argon2idParameters parameters(
    Data::FromString("000102030405060708090A0B0C0D0E0F"),
    0x10, 1024, 2, 1);

  EXPECT_THROW(
    argon2id.deriveExistingKey(
      32, password("mypassword"), parameters.serialize()),
    std::runtime_error);
}

TEST(Argon2idTest, MatchesKnownArgon2idVersion19Vector) {
  Argon2id argon2id(Argon2id::TestSettings);
  const Argon2idParameters parameters(
    Data::FromString("736F6D6573616C74"), 0x13, 65536, 2, 1);

  const auto derived = argon2id.deriveExistingKey(
    32, password("password"), parameters.serialize());

  EXPECT_EQ(
    "09316115D5CF24ED5A15A31A3BA326E5CF32EDC24702987C02B6566F61913CF7",
    derived.ToString());
}
