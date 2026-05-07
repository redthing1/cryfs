#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_KEYDERIVATION_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_KEYDERIVATION_H_

#include "RootTypes.h"

#include <cpp-utils/crypto/symmetric/EncryptionKey.h>

#include <cstddef>

namespace cryfs {
namespace formatv2 {

constexpr size_t FORMAT_V2_MASTER_KEY_SIZE = 32;

cpputils::EncryptionKey deriveRootAuthenticationKey(
  const cpputils::EncryptionKey &masterKey,
  const FilesystemId &filesystemId);

cpputils::EncryptionKey deriveObjectEncryptionKey(
  const cpputils::EncryptionKey &masterKey,
  const FilesystemId &filesystemId);

}
}

#endif
