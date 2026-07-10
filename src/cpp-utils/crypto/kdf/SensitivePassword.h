#pragma once
#ifndef MESSMER_CPPUTILS_CRYPTO_KDF_SENSITIVEPASSWORD_H
#define MESSMER_CPPUTILS_CRYPTO_KDF_SENSITIVEPASSWORD_H

#include <cpp-utils/data/Data.h>
#include <cpp-utils/macros.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace cpputils {

class SensitivePassword final {
public:
  SensitivePassword();
  SensitivePassword(SensitivePassword &&rhs) noexcept = default;
  SensitivePassword &operator=(SensitivePassword &&rhs) noexcept = default;

  static SensitivePassword FromString(std::string &&value);

  const uint8_t *data() const;
  size_t size() const;
  bool empty() const;
  bool equals(const SensitivePassword &rhs) const;

private:
  explicit SensitivePassword(Data data);

  Data _data;

  DISALLOW_COPY_AND_ASSIGN(SensitivePassword);
};

}

#endif
