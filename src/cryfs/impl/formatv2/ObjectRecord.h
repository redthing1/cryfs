#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_OBJECTRECORD_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_OBJECTRECORD_H_

#include "RootTypes.h"

#include <boost/optional.hpp>
#include <cpp-utils/crypto/symmetric/EncryptionKey.h>
#include <cpp-utils/data/Data.h>

#include <cstdint>

namespace cryfs {
namespace formatv2 {

enum class ObjectRecordType : uint8_t {
  RootContent = 1,
  Directory = 2,
  File = 3,
  Symlink = 4,
  FileData = 5,
};

constexpr size_t OBJECT_ENCRYPTION_KEY_SIZE = 32;

cpputils::Data encryptObjectRecordPayload(
  const cpputils::Data &payload,
  const cpputils::EncryptionKey &objectEncryptionKey,
  const FilesystemId &filesystemId,
  ObjectRecordType type,
  const ObjectId &objectId,
  uint64_t generation);

boost::optional<cpputils::Data> decryptObjectRecordPayload(
  const cpputils::Data &encryptedRecord,
  const cpputils::EncryptionKey &objectEncryptionKey,
  const FilesystemId &expectedFilesystemId,
  ObjectRecordType expectedType,
  const ObjectId &expectedObjectId,
  uint64_t expectedGeneration);

}
}

#endif
