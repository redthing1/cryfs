#include "ConfigPayload.h"

#include <cpp-utils/data/Deserializer.h>
#include <cpp-utils/data/Serializer.h>
#include <cpp-utils/logging/logging.h>

#include <exception>
#include <utility>

namespace cryfs {

namespace {
const std::string HEADER = "cryfs.config.payload;0";
}

cpputils::Data ConfigPayload::serialize() const {
  cpputils::Serializer serializer(
    cpputils::Serializer::StringSize(HEADER)
    + cpputils::Serializer::StringSize(cipherName)
    + config.size());
  serializer.writeString(HEADER);
  serializer.writeString(cipherName);
  serializer.writeTailData(config);
  return serializer.finished();
}

boost::optional<ConfigPayload> ConfigPayload::deserialize(
    const cpputils::Data &data) {
  try {
    cpputils::Deserializer deserializer(&data);
    if (deserializer.readString() != HEADER) {
      return boost::none;
    }
    auto cipherName = deserializer.readString();
    auto config = deserializer.readTailData();
    deserializer.finished();
    return ConfigPayload{std::move(cipherName), std::move(config)};
  } catch (const std::exception &e) {
    cpputils::logging::LOG(
      cpputils::logging::ERR,
      "Error deserializing configuration payload: {}", e.what());
    return boost::none;
  }
}

}
