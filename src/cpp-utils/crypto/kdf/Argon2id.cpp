#include "Argon2id.h"

#include <cpp-utils/random/Random.h>

#include <argon2.h>

#include <stdexcept>
#include <string>

namespace cpputils {

constexpr Argon2idSettings Argon2id::DefaultSettings;
constexpr Argon2idSettings Argon2id::TestSettings;

namespace {

EncryptionKey derive(size_t keySize, const SensitivePassword &password,
                     const Argon2idParameters &parameters) {
  if (parameters.version() != Argon2id::VERSION) {
    throw std::runtime_error(
      "Unsupported Argon2 version: " + std::to_string(parameters.version()));
  }

  auto result = EncryptionKey::Null(keySize);
  const int status = argon2id_hash_raw(
    parameters.iterations(), parameters.memoryKiB(), parameters.parallelism(),
    password.data(), password.size(),
    parameters.salt().data(), parameters.salt().size(),
    result.data(), result.binaryLength());
  if (status != ARGON2_OK) {
    throw std::runtime_error(
      std::string("Error running Argon2id key derivation: ")
      + argon2_error_message(status));
  }
  return result;
}

Argon2idParameters createParameters(const Argon2idSettings &settings) {
  return Argon2idParameters(
    Random::Csprng()->get(settings.saltLength), Argon2id::VERSION,
    settings.memoryKiB, settings.iterations, settings.parallelism);
}

}

Argon2id::Argon2id(const Argon2idSettings &settingsForNewKeys)
  : _settingsForNewKeys(settingsForNewKeys) {
}

EncryptionKey Argon2id::deriveExistingKey(
    size_t keySize, const SensitivePassword &password,
    const Data &kdfParameters) {
  return derive(
    keySize, password, Argon2idParameters::deserialize(kdfParameters));
}

Argon2id::KeyResult Argon2id::deriveNewKey(
    size_t keySize, const SensitivePassword &password) {
  const auto parameters = createParameters(_settingsForNewKeys);
  return KeyResult{derive(keySize, password, parameters), parameters.serialize()};
}

}
