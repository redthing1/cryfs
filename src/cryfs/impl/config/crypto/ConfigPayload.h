#pragma once
#ifndef MESSMER_CRYFS_SRC_CONFIG_CRYPTO_CONFIGPAYLOAD_H
#define MESSMER_CRYFS_SRC_CONFIG_CRYPTO_CONFIGPAYLOAD_H

#include <cpp-utils/data/Data.h>

#include <boost/optional.hpp>

#include <string>

namespace cryfs {

struct ConfigPayload final {
  std::string cipherName;
  cpputils::Data config;

  cpputils::Data serialize() const;
  static boost::optional<ConfigPayload> deserialize(const cpputils::Data &data);
};

}

#endif
