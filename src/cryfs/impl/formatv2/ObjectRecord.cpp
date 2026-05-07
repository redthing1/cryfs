#include "ObjectRecord.h"

#include <cpp-utils/crypto/symmetric/ciphers.h>
#include <cpp-utils/data/Deserializer.h>
#include <cpp-utils/data/Serializer.h>

#include <stdexcept>
#include <string>

namespace cryfs {
namespace formatv2 {
namespace {

static_assert(OBJECT_ENCRYPTION_KEY_SIZE == cpputils::AES256_GCM::KEYSIZE, "Object records use AES-256-GCM keys");

const std::string ENVELOPE_HEADER = "cryfs.formatv2.encrypted-object-record;0";
const std::string PLAINTEXT_HEADER = "cryfs.formatv2.object-record;0";

void validateObjectEncryptionKey(const cpputils::EncryptionKey &objectEncryptionKey) {
  if (objectEncryptionKey.binaryLength() != OBJECT_ENCRYPTION_KEY_SIZE) {
    throw std::runtime_error("Invalid format-v2 object encryption key size");
  }
}

void validateRecordIdentity(
  const FilesystemId &filesystemId,
  const ObjectId &objectId,
  uint64_t generation) {
  if (filesystemId == FilesystemId::Null()) {
    throw std::runtime_error("Invalid format-v2 filesystem id");
  }
  if (objectId == ObjectId::Null()) {
    throw std::runtime_error("Invalid format-v2 object id");
  }
  if (generation == 0) {
    throw std::runtime_error("Invalid format-v2 object generation");
  }
}

bool isKnownRecordType(uint8_t type) {
  return type == static_cast<uint8_t>(ObjectRecordType::RootContent)
      || type == static_cast<uint8_t>(ObjectRecordType::Directory)
      || type == static_cast<uint8_t>(ObjectRecordType::File)
      || type == static_cast<uint8_t>(ObjectRecordType::Symlink)
      || type == static_cast<uint8_t>(ObjectRecordType::FileData);
}

void validateRecordType(ObjectRecordType type) {
  if (!isKnownRecordType(static_cast<uint8_t>(type))) {
    throw std::runtime_error("Invalid format-v2 object record type");
  }
}

cpputils::Data serializePlaintext(
  const cpputils::Data &payload,
  const FilesystemId &filesystemId,
  ObjectRecordType type,
  const ObjectId &objectId,
  uint64_t generation) {
  cpputils::Serializer serializer(
    cpputils::Serializer::StringSize(PLAINTEXT_HEADER)
    + FilesystemId::BINARY_LENGTH
    + sizeof(uint8_t)
    + ObjectId::BINARY_LENGTH
    + sizeof(uint64_t)
    + cpputils::Serializer::DataSize(payload));
  serializer.writeString(PLAINTEXT_HEADER);
  serializer.writeFixedSizeData<FilesystemId::BINARY_LENGTH>(filesystemId);
  serializer.writeUint8(static_cast<uint8_t>(type));
  serializer.writeFixedSizeData<ObjectId::BINARY_LENGTH>(objectId);
  serializer.writeUint64(generation);
  serializer.writeData(payload);
  return serializer.finished();
}

cpputils::Data serializeEnvelope(const cpputils::Data &ciphertext) {
  cpputils::Serializer serializer(
    cpputils::Serializer::StringSize(ENVELOPE_HEADER)
    + cpputils::Serializer::DataSize(ciphertext));
  serializer.writeString(ENVELOPE_HEADER);
  serializer.writeData(ciphertext);
  return serializer.finished();
}

boost::optional<cpputils::Data> parsePlaintextPayload(
  const cpputils::Data &plaintext,
  const FilesystemId &expectedFilesystemId,
  ObjectRecordType expectedType,
  const ObjectId &expectedObjectId,
  uint64_t expectedGeneration) {
  cpputils::Deserializer deserializer(&plaintext);
  const std::string header = deserializer.readString();
  if (header != PLAINTEXT_HEADER) {
    return boost::none;
  }

  const FilesystemId filesystemId = deserializer.readFixedSizeData<FilesystemId::BINARY_LENGTH>();
  const uint8_t type = deserializer.readUint8();
  const ObjectId objectId = deserializer.readFixedSizeData<ObjectId::BINARY_LENGTH>();
  const uint64_t generation = deserializer.readUint64();
  cpputils::Data payload = deserializer.readData();
  deserializer.finished();

  if (!isKnownRecordType(type)
      || filesystemId != expectedFilesystemId
      || type != static_cast<uint8_t>(expectedType)
      || objectId != expectedObjectId
      || generation != expectedGeneration) {
    return boost::none;
  }
  return payload;
}

boost::optional<cpputils::Data> decryptEnvelope(
  const cpputils::Data &encryptedRecord,
  const cpputils::EncryptionKey &objectEncryptionKey) {
  cpputils::Deserializer deserializer(&encryptedRecord);
  const std::string header = deserializer.readString();
  if (header != ENVELOPE_HEADER) {
    return boost::none;
  }

  cpputils::Data ciphertext = deserializer.readData();
  deserializer.finished();

  return cpputils::AES256_GCM::decrypt(
    static_cast<const CryptoPP::byte*>(ciphertext.data()),
    ciphertext.size(),
    objectEncryptionKey);
}

}

cpputils::Data encryptObjectRecordPayload(
  const cpputils::Data &payload,
  const cpputils::EncryptionKey &objectEncryptionKey,
  const FilesystemId &filesystemId,
  ObjectRecordType type,
  const ObjectId &objectId,
  uint64_t generation) {
  validateObjectEncryptionKey(objectEncryptionKey);
  validateRecordIdentity(filesystemId, objectId, generation);
  validateRecordType(type);

  const cpputils::Data plaintext = serializePlaintext(payload, filesystemId, type, objectId, generation);
  const cpputils::Data ciphertext = cpputils::AES256_GCM::encrypt(
    static_cast<const CryptoPP::byte*>(plaintext.data()),
    plaintext.size(),
    objectEncryptionKey);
  return serializeEnvelope(ciphertext);
}

boost::optional<cpputils::Data> decryptObjectRecordPayload(
  const cpputils::Data &encryptedRecord,
  const cpputils::EncryptionKey &objectEncryptionKey,
  const FilesystemId &expectedFilesystemId,
  ObjectRecordType expectedType,
  const ObjectId &expectedObjectId,
  uint64_t expectedGeneration) {
  validateObjectEncryptionKey(objectEncryptionKey);
  validateRecordIdentity(expectedFilesystemId, expectedObjectId, expectedGeneration);
  validateRecordType(expectedType);

  try {
    const boost::optional<cpputils::Data> plaintext = decryptEnvelope(encryptedRecord, objectEncryptionKey);
    if (plaintext == boost::none) {
      return boost::none;
    }
    return parsePlaintextPayload(
      *plaintext,
      expectedFilesystemId,
      expectedType,
      expectedObjectId,
      expectedGeneration);
  } catch (const std::exception&) {
    return boost::none;
  }
}

}
}
