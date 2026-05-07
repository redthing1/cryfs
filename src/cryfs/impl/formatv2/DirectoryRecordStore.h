#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_DIRECTORYRECORDSTORE_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_DIRECTORYRECORDSTORE_H_

#include "DirectoryRecord.h"
#include "ObjectRecord.h"

#include <boost/filesystem/path.hpp>
#include <cpp-utils/crypto/symmetric/EncryptionKey.h>

namespace cryfs {
namespace formatv2 {

class DirectoryRecordStore final {
public:
  DirectoryRecordStore(
    boost::filesystem::path directory,
    FilesystemId filesystemId,
    cpputils::EncryptionKey objectEncryptionKey);

  void store(const DirectoryRecord &record) const;
  boost::optional<DirectoryRecord> load(const ObjectId &directoryId, uint64_t generation) const;

private:
  boost::filesystem::path _pathForDirectory(const ObjectId &directoryId, uint64_t generation) const;

  boost::filesystem::path _directory;
  FilesystemId _filesystemId;
  cpputils::EncryptionKey _objectEncryptionKey;
};

}
}

#endif
