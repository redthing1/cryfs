#include <cpp-utils/crypto/kdf/Argon2idParameters.h>
#include <cpp-utils/data/DataFixture.h>

#include <gtest/gtest.h>

using namespace cpputils;

TEST(Argon2idParametersTest, SerializesAndDeserializesAllFields) {
  const Argon2idParameters parameters(
    DataFixture::generate(16), 0x13, 1048576, 3, 4);

  const auto loaded = Argon2idParameters::deserialize(parameters.serialize());

  EXPECT_EQ(parameters, loaded);
}

TEST(Argon2idParametersTest, UsesStableSerializedRepresentation) {
  const Argon2idParameters parameters(
    Data::FromString("000102030405060708090A0B0C0D0E0F"),
    0x13, 1048576, 3, 4);

  EXPECT_EQ(
    "13000000000010000300000004000000000102030405060708090A0B0C0D0E0F",
    parameters.serialize().ToString());
}
