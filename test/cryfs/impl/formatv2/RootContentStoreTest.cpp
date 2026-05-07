#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/RootContentStore.h>

#include <boost/filesystem.hpp>
#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstdint>
#include <stdexcept>
#include <string>

using cryfs::formatv2::AuthenticatedRoot;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::RootContent;
using cryfs::formatv2::RootContentStore;
using cryfs::formatv2::RootId;
using cryfs::formatv2::deriveObjectEncryptionKey;

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

RootId rootId(unsigned int seed) {
  return cpputils::DataFixture::generateFixedSize<RootId::BINARY_LENGTH>(seed);
}

ObjectId objectId(unsigned int seed) {
  return cpputils::DataFixture::generateFixedSize<ObjectId::BINARY_LENGTH>(seed);
}

cpputils::EncryptionKey masterKey(unsigned int seed = 1) {
  return cpputils::EncryptionKey::FromString(
    cpputils::DataFixture::generate(FORMAT_V2_MASTER_KEY_SIZE, seed).ToString());
}

cpputils::EncryptionKey objectEncryptionKey(unsigned int seed = 1) {
  return deriveObjectEncryptionKey(masterKey(seed), filesystemId());
}

RootContent rootContent(uint64_t epoch = 7, unsigned int rootIdSeed = 1) {
  return RootContent{filesystemId(), epoch, rootId(rootIdSeed), objectId(3), 11};
}

AuthenticatedRoot authenticatedRoot(const RootContent &content) {
  return AuthenticatedRoot{content.epoch, content.rootId};
}

boost::filesystem::path rootContentPath(
  const boost::filesystem::path &directory,
  const RootId &id) {
  return directory / ("root-content." + id.ToString());
}

void flipByte(cpputils::Data *data, size_t offset) {
  auto *byte = static_cast<uint8_t*>(data->dataOffset(offset));
  *byte ^= 1;
}

}

TEST(RootContentStoreTest, ConstructorCreatesDirectory) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "root-content";

  const RootContentStore store(directory, filesystemId(), objectEncryptionKey());

  EXPECT_TRUE(boost::filesystem::is_directory(directory));
}

TEST(RootContentStoreTest, ConstructorRejectsNullFilesystemId) {
  const cpputils::TempDir tempDir;

  EXPECT_THROW(
    RootContentStore(tempDir.path() / "root-content", FilesystemId::Null(), objectEncryptionKey()),
    std::runtime_error);
}

TEST(RootContentStoreTest, StoreWritesRootContentReadableBySelectedRoot) {
  const cpputils::TempDir tempDir;
  const RootContentStore store(tempDir.path() / "root-content", filesystemId(), objectEncryptionKey());
  const RootContent content = rootContent();

  store.store(content);

  const auto loaded = store.load(authenticatedRoot(content));
  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(content, *loaded);
}

TEST(RootContentStoreTest, StoreIsIdempotentForSameRootContent) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "root-content";
  const RootContentStore store(directory, filesystemId(), objectEncryptionKey());
  const RootContent content = rootContent();
  store.store(content);
  const cpputils::Data before = cpputils::Data::LoadFromFile(rootContentPath(directory, content.rootId)).value();

  store.store(content);

  const cpputils::Data after = cpputils::Data::LoadFromFile(rootContentPath(directory, content.rootId)).value();
  EXPECT_EQ(before, after);
  EXPECT_EQ(content, *store.load(authenticatedRoot(content)));
}

TEST(RootContentStoreTest, StoreRejectsConflictingRootContentWithSameRootId) {
  const cpputils::TempDir tempDir;
  const RootContentStore store(tempDir.path() / "root-content", filesystemId(), objectEncryptionKey());
  const RootContent content = rootContent();
  RootContent conflicting = content;
  conflicting.rootDirectoryGeneration += 1;
  store.store(content);

  EXPECT_THROW(store.store(conflicting), std::runtime_error);
  EXPECT_EQ(content, *store.load(authenticatedRoot(content)));
}

TEST(RootContentStoreTest, LoadReturnsNoneWhenRootContentIsMissing) {
  const cpputils::TempDir tempDir;
  const RootContentStore store(tempDir.path() / "root-content", filesystemId(), objectEncryptionKey());

  EXPECT_FALSE(store.load(authenticatedRoot(rootContent())).is_initialized());
}

TEST(RootContentStoreTest, StoreRejectsWrongFilesystemId) {
  const cpputils::TempDir tempDir;
  const RootContentStore store(tempDir.path() / "root-content", filesystemId(), objectEncryptionKey());
  RootContent content = rootContent();
  content.filesystemId = otherFilesystemId();

  EXPECT_THROW(store.store(content), std::runtime_error);
}

TEST(RootContentStoreTest, LoadRejectsTamperedRootContentRecord) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "root-content";
  const RootContentStore store(directory, filesystemId(), objectEncryptionKey());
  const RootContent content = rootContent();
  store.store(content);

  cpputils::Data data = cpputils::Data::LoadFromFile(rootContentPath(directory, content.rootId)).value();
  flipByte(&data, data.size() - 1);
  data.StoreToFile(rootContentPath(directory, content.rootId));

  EXPECT_FALSE(store.load(authenticatedRoot(content)).is_initialized());
}

TEST(RootContentStoreTest, LoadRejectsWrongObjectEncryptionKey) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "root-content";
  const RootContent content = rootContent();
  RootContentStore(directory, filesystemId(), objectEncryptionKey(1)).store(content);

  const RootContentStore store(directory, filesystemId(), objectEncryptionKey(2));

  EXPECT_FALSE(store.load(authenticatedRoot(content)).is_initialized());
}

TEST(RootContentStoreTest, LoadRejectsSelectedRootWithWrongEpoch) {
  const cpputils::TempDir tempDir;
  const RootContentStore store(tempDir.path() / "root-content", filesystemId(), objectEncryptionKey());
  const RootContent content = rootContent();
  store.store(content);

  EXPECT_FALSE(store.load(AuthenticatedRoot{content.epoch + 1, content.rootId}).is_initialized());
}

TEST(RootContentStoreTest, LoadRejectsSelectedRootWithWrongRootId) {
  const cpputils::TempDir tempDir;
  const RootContentStore store(tempDir.path() / "root-content", filesystemId(), objectEncryptionKey());
  const RootContent content = rootContent();
  store.store(content);

  EXPECT_FALSE(store.load(AuthenticatedRoot{content.epoch, rootId(2)}).is_initialized());
}
