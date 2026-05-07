#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/RootContent.h>

#include <cpp-utils/data/Serializer.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

using cryfs::formatv2::AuthenticatedRoot;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::RootContent;
using cryfs::formatv2::RootDirectoryRef;
using cryfs::formatv2::RootId;
using cryfs::formatv2::deserializeRootContent;
using cryfs::formatv2::serializeRootContent;
using cryfs::formatv2::trustedRootDirectoryFromContent;

namespace {

FilesystemId filesystemId(const std::string &value) {
  return FilesystemId::FromString(value);
}

FilesystemId filesystemId() {
  return filesystemId("00112233445566778899AABBCCDDEEFF");
}

FilesystemId otherFilesystemId() {
  return filesystemId("FFEEDDCCBBAA99887766554433221100");
}

RootId rootId(const std::string &value) {
  return RootId::FromString(value);
}

RootId rootId() {
  return rootId("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
}

RootId otherRootId() {
  return rootId("FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210");
}

ObjectId objectId(const std::string &value) {
  return ObjectId::FromString(value);
}

ObjectId rootDirectoryId() {
  return objectId("1111111111111111111111111111111111111111111111111111111111111111");
}

RootContent rootContent() {
  return RootContent{filesystemId(), 7, rootId(), rootDirectoryId(), 9};
}

cpputils::Data serializeStringOnly(const std::string &value) {
  cpputils::Serializer serializer(cpputils::Serializer::StringSize(value));
  serializer.writeString(value);
  return serializer.finished();
}

cpputils::Data withTrailingByte(const cpputils::Data &data) {
  cpputils::Data result(data.size() + 1);
  std::memcpy(result.data(), data.data(), data.size());
  static_cast<char*>(result.data())[data.size()] = 0;
  return result;
}

}

TEST(RootContentTest, RoundTripsRootContent) {
  const RootContent content = rootContent();

  const auto loaded = deserializeRootContent(serializeRootContent(content));

  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(content, *loaded);
}

TEST(RootContentTest, RejectsWrongHeader) {
  EXPECT_FALSE(deserializeRootContent(serializeStringOnly("not root content")).is_initialized());
}

TEST(RootContentTest, RejectsTruncatedContent) {
  const cpputils::Data serialized = serializeRootContent(rootContent());
  cpputils::Data truncated(serialized.size() - 1);
  std::memcpy(truncated.data(), serialized.data(), truncated.size());

  EXPECT_FALSE(deserializeRootContent(truncated).is_initialized());
}

TEST(RootContentTest, RejectsContentWithTrailingData) {
  EXPECT_FALSE(deserializeRootContent(withTrailingByte(serializeRootContent(rootContent()))).is_initialized());
}

TEST(RootContentTest, RejectsNullFilesystemId) {
  RootContent content = rootContent();
  content.filesystemId = FilesystemId::Null();

  EXPECT_THROW(serializeRootContent(content), std::runtime_error);
}

TEST(RootContentTest, RejectsZeroEpoch) {
  RootContent content = rootContent();
  content.epoch = 0;

  EXPECT_THROW(serializeRootContent(content), std::runtime_error);
}

TEST(RootContentTest, RejectsNullRootId) {
  RootContent content = rootContent();
  content.rootId = RootId::Null();

  EXPECT_THROW(serializeRootContent(content), std::runtime_error);
}

TEST(RootContentTest, RejectsNullRootDirectoryId) {
  RootContent content = rootContent();
  content.rootDirectoryId = ObjectId::Null();

  EXPECT_THROW(serializeRootContent(content), std::runtime_error);
}

TEST(RootContentTest, RejectsZeroRootDirectoryGeneration) {
  RootContent content = rootContent();
  content.rootDirectoryGeneration = 0;

  EXPECT_THROW(serializeRootContent(content), std::runtime_error);
}

TEST(RootContentTest, TrustedRootDirectoryUsesRootDirectoryIdWhenBindingsMatch) {
  const RootContent content = rootContent();
  const AuthenticatedRoot expectedRoot{content.epoch, content.rootId};

  const auto rootDirectory = trustedRootDirectoryFromContent(content, content.filesystemId, expectedRoot);

  ASSERT_TRUE(rootDirectory.is_initialized());
  EXPECT_EQ((RootDirectoryRef{content.rootDirectoryId, content.rootDirectoryGeneration}), *rootDirectory);
}

TEST(RootContentTest, TrustedRootDirectoryRejectsWrongFilesystemId) {
  const RootContent content = rootContent();
  const AuthenticatedRoot expectedRoot{content.epoch, content.rootId};

  EXPECT_FALSE(trustedRootDirectoryFromContent(content, otherFilesystemId(), expectedRoot).is_initialized());
}

TEST(RootContentTest, TrustedRootDirectoryRejectsWrongEpoch) {
  const RootContent content = rootContent();
  const AuthenticatedRoot expectedRoot{content.epoch + 1, content.rootId};

  EXPECT_FALSE(trustedRootDirectoryFromContent(content, content.filesystemId, expectedRoot).is_initialized());
}

TEST(RootContentTest, TrustedRootDirectoryRejectsWrongRootId) {
  const RootContent content = rootContent();
  const AuthenticatedRoot expectedRoot{content.epoch, otherRootId()};

  EXPECT_FALSE(trustedRootDirectoryFromContent(content, content.filesystemId, expectedRoot).is_initialized());
}
