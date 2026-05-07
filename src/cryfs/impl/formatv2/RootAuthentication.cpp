#include "RootAuthentication.h"

#include <cpp-utils/data/Deserializer.h>
#include <cpp-utils/data/Serializer.h>
#include <vendor_cryptopp/hmac.h>
#include <vendor_cryptopp/misc.h>
#include <vendor_cryptopp/sha.h>

#include <stdexcept>
#include <string>

namespace cryfs {
namespace formatv2 {
namespace {

const std::string ENVELOPE_HEADER = "cryfs.formatv2.authenticated-root;0";
const std::string MAC_CONTEXT = "cryfs.formatv2.root-record-authentication;0";

void validateAuthenticationKey(const cpputils::EncryptionKey &authenticationKey) {
  if (authenticationKey.binaryLength() != ROOT_AUTHENTICATION_KEY_SIZE) {
    throw std::runtime_error("Invalid format-v2 root authentication key size");
  }
}

RootAuthenticationTag computeTag(
  const cpputils::Data &serializedRootRecord,
  const cpputils::EncryptionKey &authenticationKey) {
  validateAuthenticationKey(authenticationKey);

  RootAuthenticationTag tag = RootAuthenticationTag::Null();
  CryptoPP::HMAC<CryptoPP::SHA256> hmac(
    static_cast<const CryptoPP::byte*>(authenticationKey.data()),
    authenticationKey.binaryLength());
  hmac.Update(
    reinterpret_cast<const CryptoPP::byte*>(MAC_CONTEXT.data()),
    MAC_CONTEXT.size());
  hmac.Update(
    static_cast<const CryptoPP::byte*>(serializedRootRecord.data()),
    serializedRootRecord.size());
  hmac.Final(tag.data());
  return tag;
}

cpputils::Data serializeEnvelope(
  const cpputils::Data &serializedRootRecord,
  const RootAuthenticationTag &tag) {
  cpputils::Serializer serializer(
    cpputils::Serializer::StringSize(ENVELOPE_HEADER)
    + cpputils::Serializer::DataSize(serializedRootRecord)
    + RootAuthenticationTag::BINARY_LENGTH);
  serializer.writeString(ENVELOPE_HEADER);
  serializer.writeData(serializedRootRecord);
  serializer.writeFixedSizeData<RootAuthenticationTag::BINARY_LENGTH>(tag);
  return serializer.finished();
}

boost::optional<AuthenticatedRoot> parseAndVerifyEnvelope(
  const cpputils::Data &authenticatedRecord,
  const cpputils::EncryptionKey &authenticationKey,
  const FilesystemId &expectedFilesystemId) {
  cpputils::Deserializer deserializer(&authenticatedRecord);
  const std::string header = deserializer.readString();
  if (header != ENVELOPE_HEADER) {
    return boost::none;
  }

  const cpputils::Data serializedRootRecord = deserializer.readData();
  const RootAuthenticationTag storedTag = deserializer.readFixedSizeData<RootAuthenticationTag::BINARY_LENGTH>();
  deserializer.finished();

  const RootAuthenticationTag computedTag = computeTag(serializedRootRecord, authenticationKey);
  if (!CryptoPP::VerifyBufsEqual(storedTag.data(), computedTag.data(), RootAuthenticationTag::BINARY_LENGTH)) {
    return boost::none;
  }

  const boost::optional<RootRecord> rootRecord = deserializeRootRecord(serializedRootRecord);
  if (rootRecord == boost::none) {
    return boost::none;
  }

  return trustedRootFromRecord(*rootRecord, expectedFilesystemId);
}

}

cpputils::Data authenticateRootRecord(
  const RootRecord &record,
  const cpputils::EncryptionKey &authenticationKey) {
  const cpputils::Data serializedRootRecord = serializeRootRecord(record);
  const RootAuthenticationTag tag = computeTag(serializedRootRecord, authenticationKey);
  return serializeEnvelope(serializedRootRecord, tag);
}

boost::optional<AuthenticatedRoot> verifyRootRecord(
  const cpputils::Data &authenticatedRecord,
  const cpputils::EncryptionKey &authenticationKey,
  const FilesystemId &expectedFilesystemId) {
  validateAuthenticationKey(authenticationKey);
  try {
    return parseAndVerifyEnvelope(authenticatedRecord, authenticationKey, expectedFilesystemId);
  } catch (const std::exception&) {
    return boost::none;
  }
}

}
}
