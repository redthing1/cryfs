#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ROOTAUTHENTICATION_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ROOTAUTHENTICATION_H_

#include "RootRecord.h"

#include <boost/optional.hpp>
#include <cpp-utils/crypto/symmetric/EncryptionKey.h>
#include <cpp-utils/data/Data.h>
#include <cpp-utils/data/FixedSizeData.h>

#include <cstddef>

namespace cryfs {
namespace formatv2 {

constexpr size_t ROOT_AUTHENTICATION_KEY_SIZE = 32;
using RootAuthenticationTag = cpputils::FixedSizeData<32>;

cpputils::Data authenticateRootRecord(
  const RootRecord &record,
  const cpputils::EncryptionKey &authenticationKey);

boost::optional<AuthenticatedRoot> verifyRootRecord(
  const cpputils::Data &authenticatedRecord,
  const cpputils::EncryptionKey &authenticationKey,
  const FilesystemId &expectedFilesystemId);

}
}

#endif
