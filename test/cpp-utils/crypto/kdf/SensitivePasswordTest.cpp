#include <cpp-utils/crypto/kdf/SensitivePassword.h>

#include <gtest/gtest.h>

#include <utility>

using cpputils::SensitivePassword;

TEST(SensitivePasswordTest, ComparesEqualValues) {
  const auto first = SensitivePassword::FromString("secret");
  const auto second = SensitivePassword::FromString("secret");

  EXPECT_TRUE(first.equals(second));
}

TEST(SensitivePasswordTest, RejectsDifferentValuesAndLengths) {
  const auto password = SensitivePassword::FromString("secret");
  const auto different = SensitivePassword::FromString("secreu");
  const auto shorter = SensitivePassword::FromString("secre");

  EXPECT_FALSE(password.equals(different));
  EXPECT_FALSE(password.equals(shorter));
}

TEST(SensitivePasswordTest, IsMoveOnlyAndRetainsValueWhenMoved) {
  auto source = SensitivePassword::FromString("secret");
  const auto expected = SensitivePassword::FromString("secret");

  const SensitivePassword moved(std::move(source));

  EXPECT_TRUE(moved.equals(expected));
  EXPECT_TRUE(source.empty());
}
