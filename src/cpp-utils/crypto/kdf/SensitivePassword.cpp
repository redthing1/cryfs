#include "SensitivePassword.h"

#include <cpp-utils/pointer/unique_ref.h>
#include <cpp-utils/system/memory.h>
#include <vendor_cryptopp/misc.h>

#include <cstring>
#include <utility>

namespace cpputils {

SensitivePassword::SensitivePassword()
  : _data(0, make_unique_ref<UnswappableAllocator>()) {
}

SensitivePassword::SensitivePassword(Data data)
  : _data(std::move(data)) {
}

SensitivePassword SensitivePassword::FromString(std::string &&value) {
  Data protectedValue(value.size(), make_unique_ref<UnswappableAllocator>());
  if (!value.empty()) {
    std::memcpy(protectedValue.data(), value.data(), value.size());
    CryptoPP::SecureWipeBuffer(
      reinterpret_cast<CryptoPP::byte*>(&value[0]),
      value.size());
  }
  return SensitivePassword(std::move(protectedValue));
}

const uint8_t *SensitivePassword::data() const {
  return static_cast<const uint8_t*>(_data.data());
}

size_t SensitivePassword::size() const {
  return _data.size();
}

bool SensitivePassword::empty() const {
  return size() == 0;
}

bool SensitivePassword::equals(const SensitivePassword &rhs) const {
  return size() == rhs.size()
      && CryptoPP::VerifyBufsEqual(data(), rhs.data(), size());
}

}
