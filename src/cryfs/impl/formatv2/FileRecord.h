#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_FILERECORD_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_FILERECORD_H_

#include "ObjectMetadata.h"
#include "RootTypes.h"

#include <boost/optional.hpp>
#include <cpp-utils/data/Data.h>

#include <cstdint>
#include <vector>

namespace cryfs {
namespace formatv2 {

using FileMetadata = ObjectMetadata;

struct FileExtent final {
  uint64_t offset;
  uint64_t size;
  ObjectId dataId;
  uint64_t generation;
};

struct FileRecord final {
  FilesystemId filesystemId;
  ObjectId fileId;
  uint64_t generation;
  FileMetadata metadata;
  uint64_t size;
  std::vector<FileExtent> extents;
};

struct FileDataRecord final {
  FilesystemId filesystemId;
  ObjectId dataId;
  uint64_t generation;
  cpputils::Data payload;
};

bool operator==(const FileExtent &lhs, const FileExtent &rhs);
bool operator!=(const FileExtent &lhs, const FileExtent &rhs);
bool operator==(const FileRecord &lhs, const FileRecord &rhs);
bool operator!=(const FileRecord &lhs, const FileRecord &rhs);
bool operator==(const FileDataRecord &lhs, const FileDataRecord &rhs);
bool operator!=(const FileDataRecord &lhs, const FileDataRecord &rhs);

cpputils::Data serializeFileRecord(const FileRecord &record);
boost::optional<FileRecord> deserializeFileRecord(const cpputils::Data &data);
cpputils::Data serializeFileDataRecord(const FileDataRecord &record);
boost::optional<FileDataRecord> deserializeFileDataRecord(const cpputils::Data &data);

bool fileRecordMatchesObject(
  const FileRecord &record,
  const FilesystemId &expectedFilesystemId,
  const ObjectId &expectedFileId,
  uint64_t expectedGeneration);

bool fileDataRecordMatchesObject(
  const FileDataRecord &record,
  const FilesystemId &expectedFilesystemId,
  const ObjectId &expectedDataId,
  uint64_t expectedGeneration);

}
}

#endif
