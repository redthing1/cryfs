#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ROOTPUBLICATIONSTORE_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ROOTPUBLICATIONSTORE_H_

#include "RootAuthentication.h"

#include <boost/filesystem/path.hpp>
#include <cpp-utils/crypto/symmetric/EncryptionKey.h>

#include <vector>

namespace cryfs {
namespace formatv2 {

class RootPublicationStore final {
public:
  RootPublicationStore(
    boost::filesystem::path directory,
    FilesystemId filesystemId,
    cpputils::EncryptionKey authenticationKey);

  void publish(const RootRecord &record) const;
  std::vector<AuthenticatedRoot> loadAuthenticatedRoots() const;

private:
  boost::filesystem::path _slotPathForEpoch(uint64_t epoch) const;

  boost::filesystem::path _directory;
  FilesystemId _filesystemId;
  cpputils::EncryptionKey _authenticationKey;
};

}
}

#endif
