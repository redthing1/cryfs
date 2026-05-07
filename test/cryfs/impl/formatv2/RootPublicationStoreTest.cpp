#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/RootPublicationStore.h>

#include <boost/filesystem.hpp>
#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using cryfs::formatv2::AuthenticatedRoot;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::RootId;
using cryfs::formatv2::RootPublicationStore;
using cryfs::formatv2::RootRecord;
using cryfs::formatv2::authenticateRootRecord;
using cryfs::formatv2::deriveRootAuthenticationKey;

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

cpputils::EncryptionKey masterKey(unsigned int seed = 1) {
  return cpputils::EncryptionKey::FromString(
    cpputils::DataFixture::generate(FORMAT_V2_MASTER_KEY_SIZE, seed).ToString());
}

cpputils::EncryptionKey authenticationKey(unsigned int seed = 1) {
  return deriveRootAuthenticationKey(masterKey(seed), filesystemId());
}

RootRecord rootRecord(uint64_t epoch, unsigned int rootIdSeed) {
  return RootRecord{filesystemId(), epoch, rootId(rootIdSeed)};
}

AuthenticatedRoot authenticatedRoot(const RootRecord &record) {
  return AuthenticatedRoot{record.epoch, record.rootId};
}

bool containsRoot(const std::vector<AuthenticatedRoot> &roots, const RootRecord &record) {
  const AuthenticatedRoot expected = authenticatedRoot(record);
  for (const AuthenticatedRoot &root: roots) {
    if (root == expected) {
      return true;
    }
  }
  return false;
}

void flipByte(cpputils::Data *data, size_t offset) {
  auto *byte = static_cast<uint8_t*>(data->dataOffset(offset));
  *byte ^= 1;
}

}

TEST(RootPublicationStoreTest, ConstructorCreatesPublicationDirectory) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "roots";

  const RootPublicationStore store(directory, filesystemId(), authenticationKey());

  EXPECT_TRUE(boost::filesystem::is_directory(directory));
}

TEST(RootPublicationStoreTest, ConstructorCreatesMissingParentDirectories) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "state" / "format-v2" / "roots";

  const RootPublicationStore store(directory, filesystemId(), authenticationKey());

  EXPECT_TRUE(boost::filesystem::is_directory(directory));
}

TEST(RootPublicationStoreTest, ConstructorRejectsNullFilesystemId) {
  const cpputils::TempDir tempDir;

  EXPECT_THROW(
    RootPublicationStore(tempDir.path() / "roots", FilesystemId::Null(), authenticationKey()),
    std::runtime_error);
}

TEST(RootPublicationStoreTest, PublishWritesRootReadableByLoad) {
  const cpputils::TempDir tempDir;
  const RootPublicationStore store(tempDir.path() / "roots", filesystemId(), authenticationKey());
  const RootRecord record = rootRecord(1, 1);

  store.publish(record);

  const auto roots = store.loadAuthenticatedRoots();
  ASSERT_EQ(1u, roots.size());
  EXPECT_EQ(authenticatedRoot(record), roots[0]);
}

TEST(RootPublicationStoreTest, PublishRejectsWrongFilesystemId) {
  const cpputils::TempDir tempDir;
  const RootPublicationStore store(tempDir.path() / "roots", filesystemId(), authenticationKey());
  RootRecord record = rootRecord(1, 1);
  record.filesystemId = otherFilesystemId();

  EXPECT_THROW(store.publish(record), std::runtime_error);
}

TEST(RootPublicationStoreTest, PublishingThreeEpochsKeepsLatestEpochFromEachSlot) {
  const cpputils::TempDir tempDir;
  const RootPublicationStore store(tempDir.path() / "roots", filesystemId(), authenticationKey());
  const RootRecord epoch1 = rootRecord(1, 1);
  const RootRecord epoch2 = rootRecord(2, 2);
  const RootRecord epoch3 = rootRecord(3, 3);

  store.publish(epoch1);
  store.publish(epoch2);
  store.publish(epoch3);

  const auto roots = store.loadAuthenticatedRoots();
  ASSERT_EQ(2u, roots.size());
  EXPECT_FALSE(containsRoot(roots, epoch1));
  EXPECT_TRUE(containsRoot(roots, epoch2));
  EXPECT_TRUE(containsRoot(roots, epoch3));
}

TEST(RootPublicationStoreTest, LoadIgnoresTamperedSlot) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "roots";
  const RootPublicationStore store(directory, filesystemId(), authenticationKey());
  const RootRecord record = rootRecord(1, 1);
  store.publish(record);

  cpputils::Data slotData = cpputils::Data::LoadFromFile(directory / "root.1").value();
  flipByte(&slotData, slotData.size() - 1);
  slotData.StoreToFile(directory / "root.1");

  EXPECT_TRUE(store.loadAuthenticatedRoots().empty());
}

TEST(RootPublicationStoreTest, LoadKeepsAuthenticatedSlotWhenOtherSlotIsTampered) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "roots";
  const RootPublicationStore store(directory, filesystemId(), authenticationKey());
  const RootRecord epoch1 = rootRecord(1, 1);
  const RootRecord epoch2 = rootRecord(2, 2);
  store.publish(epoch1);
  store.publish(epoch2);

  cpputils::Data slotData = cpputils::Data::LoadFromFile(directory / "root.1").value();
  flipByte(&slotData, slotData.size() - 1);
  slotData.StoreToFile(directory / "root.1");

  const auto roots = store.loadAuthenticatedRoots();
  ASSERT_EQ(1u, roots.size());
  EXPECT_EQ(authenticatedRoot(epoch2), roots[0]);
}

TEST(RootPublicationStoreTest, LoadIgnoresSlotAuthenticatedWithWrongKey) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "roots";
  const RootRecord record = rootRecord(1, 1);
  RootPublicationStore(directory, filesystemId(), authenticationKey(1)).publish(record);

  const RootPublicationStore store(directory, filesystemId(), authenticationKey(2));

  EXPECT_TRUE(store.loadAuthenticatedRoots().empty());
}

TEST(RootPublicationStoreTest, LoadIgnoresNonSlotFiles) {
  const cpputils::TempDir tempDir;
  const auto directory = tempDir.path() / "roots";
  const RootPublicationStore store(directory, filesystemId(), authenticationKey());
  const RootRecord record = rootRecord(1, 1);

  authenticateRootRecord(record, authenticationKey()).StoreToFile(directory / "other");

  EXPECT_TRUE(store.loadAuthenticatedRoots().empty());
}
