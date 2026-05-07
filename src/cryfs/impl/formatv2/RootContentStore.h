#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ROOTCONTENTSTORE_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ROOTCONTENTSTORE_H_

#include "RootContent.h"
#include "ObjectRecord.h"

#include <boost/filesystem/path.hpp>
#include <cpp-utils/crypto/symmetric/EncryptionKey.h>

namespace cryfs {
namespace formatv2 {

class RootContentStore final {
public:
  RootContentStore(
    boost::filesystem::path directory,
    FilesystemId filesystemId,
    cpputils::EncryptionKey objectEncryptionKey);

  void store(const RootContent &content) const;
  boost::optional<RootContent> load(const AuthenticatedRoot &root) const;

private:
  boost::filesystem::path _pathForRoot(const RootId &rootId) const;

  boost::filesystem::path _directory;
  FilesystemId _filesystemId;
  cpputils::EncryptionKey _objectEncryptionKey;
};

}
}

#endif
