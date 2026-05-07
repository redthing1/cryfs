#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_SYMLINKRECORDSTORE_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_SYMLINKRECORDSTORE_H_

#include "ObjectRecord.h"
#include "SymlinkRecord.h"

#include <boost/filesystem/path.hpp>
#include <cpp-utils/crypto/symmetric/EncryptionKey.h>

namespace cryfs {
namespace formatv2 {

class SymlinkRecordStore final {
public:
  SymlinkRecordStore(
    boost::filesystem::path directory,
    FilesystemId filesystemId,
    cpputils::EncryptionKey objectEncryptionKey);

  void store(const SymlinkRecord &record) const;
  boost::optional<SymlinkRecord> load(const ObjectId &symlinkId, uint64_t generation) const;

private:
  boost::filesystem::path _pathForSymlink(const ObjectId &symlinkId, uint64_t generation) const;

  boost::filesystem::path _directory;
  FilesystemId _filesystemId;
  cpputils::EncryptionKey _objectEncryptionKey;
};

}
}

#endif
