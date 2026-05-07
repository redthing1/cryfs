#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_FILERECORDSTORE_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_FILERECORDSTORE_H_

#include "FileRecord.h"
#include "ObjectRecord.h"

#include <boost/filesystem/path.hpp>
#include <cpp-utils/crypto/symmetric/EncryptionKey.h>

namespace cryfs {
namespace formatv2 {

class FileRecordStore final {
public:
  FileRecordStore(
    boost::filesystem::path directory,
    FilesystemId filesystemId,
    cpputils::EncryptionKey objectEncryptionKey);

  void store(const FileRecord &record) const;
  boost::optional<FileRecord> load(const ObjectId &fileId, uint64_t generation) const;

  void storeData(const FileDataRecord &record) const;
  boost::optional<FileDataRecord> loadData(const ObjectId &dataId, uint64_t generation) const;

private:
  boost::filesystem::path _pathForFile(const ObjectId &fileId, uint64_t generation) const;
  boost::filesystem::path _pathForFileData(const ObjectId &dataId, uint64_t generation) const;

  boost::filesystem::path _directory;
  FilesystemId _filesystemId;
  cpputils::EncryptionKey _objectEncryptionKey;
};

}
}

#endif
