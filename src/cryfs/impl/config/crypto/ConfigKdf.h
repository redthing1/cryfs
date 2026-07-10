#pragma once
#ifndef MESSMER_CRYFS_SRC_CONFIG_CRYPTO_CONFIGKDF_H
#define MESSMER_CRYFS_SRC_CONFIG_CRYPTO_CONFIGKDF_H

namespace cryfs {

enum class ConfigKdf {
  Scrypt,
  Argon2id,
};

}

#endif
