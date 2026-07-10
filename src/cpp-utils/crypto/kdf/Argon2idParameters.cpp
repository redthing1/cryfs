#include "Argon2idParameters.h"

#include <cpp-utils/data/Deserializer.h>
#include <cpp-utils/data/Serializer.h>

#include <utility>

namespace cpputils {

Argon2idParameters::Argon2idParameters(Data salt, uint32_t version,
                                       uint32_t memoryKiB, uint32_t iterations,
                                       uint32_t parallelism)
  : _salt(std::move(salt)),
    _version(version),
    _memoryKiB(memoryKiB),
    _iterations(iterations),
    _parallelism(parallelism) {
}

const Data &Argon2idParameters::salt() const {
  return _salt;
}

uint32_t Argon2idParameters::version() const {
  return _version;
}

uint32_t Argon2idParameters::memoryKiB() const {
  return _memoryKiB;
}

uint32_t Argon2idParameters::iterations() const {
  return _iterations;
}

uint32_t Argon2idParameters::parallelism() const {
  return _parallelism;
}

Data Argon2idParameters::serialize() const {
  Serializer serializer(4 * sizeof(uint32_t) + _salt.size());
  serializer.writeUint32(_version);
  serializer.writeUint32(_memoryKiB);
  serializer.writeUint32(_iterations);
  serializer.writeUint32(_parallelism);
  serializer.writeTailData(_salt);
  return serializer.finished();
}

Argon2idParameters Argon2idParameters::deserialize(const Data &data) {
  Deserializer deserializer(&data);
  const auto version = deserializer.readUint32();
  const auto memoryKiB = deserializer.readUint32();
  const auto iterations = deserializer.readUint32();
  const auto parallelism = deserializer.readUint32();
  auto salt = deserializer.readTailData();
  deserializer.finished();
  return Argon2idParameters(
    std::move(salt), version, memoryKiB, iterations, parallelism);
}

bool operator==(const Argon2idParameters &lhs, const Argon2idParameters &rhs) {
  return lhs.salt() == rhs.salt()
      && lhs.version() == rhs.version()
      && lhs.memoryKiB() == rhs.memoryKiB()
      && lhs.iterations() == rhs.iterations()
      && lhs.parallelism() == rhs.parallelism();
}

bool operator!=(const Argon2idParameters &lhs, const Argon2idParameters &rhs) {
  return !(lhs == rhs);
}

}
