#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/AcceptedRootState.h>

#include <boost/filesystem.hpp>
#include <cpp-utils/data/Data.h>
#include <cpp-utils/data/Serializer.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <cstring>
#include <stdexcept>
#include <string>

using cryfs::formatv2::AcceptedRoot;
using cryfs::formatv2::AcceptedRootState;
using cryfs::formatv2::AcceptedRootStateStore;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::RootId;
using cryfs::formatv2::deserializeAcceptedRootState;
using cryfs::formatv2::serializeAcceptedRootState;

namespace {

FilesystemId filesystemId(const std::string &value) {
  return FilesystemId::FromString(value);
}

RootId rootId(const std::string &value) {
  return RootId::FromString(value);
}

FilesystemId filesystemId() {
  return filesystemId("00112233445566778899AABBCCDDEEFF");
}

FilesystemId otherFilesystemId() {
  return filesystemId("FFEEDDCCBBAA99887766554433221100");
}

AcceptedRoot root(uint64_t epoch = 7) {
  return AcceptedRoot{
    epoch,
    rootId("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF")
  };
}

AcceptedRoot otherRoot(uint64_t epoch = 7) {
  return AcceptedRoot{
    epoch,
    rootId("FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210")
  };
}

cpputils::Data serializeStringOnly(const std::string &value) {
  cpputils::Serializer serializer(cpputils::Serializer::StringSize(value));
  serializer.writeString(value);
  return serializer.finished();
}

}

TEST(AcceptedRootStateTest, RoundTripsAcceptedRootState) {
  const AcceptedRootState original{filesystemId(), root()};

  const auto parsed = deserializeAcceptedRootState(serializeAcceptedRootState(original));

  ASSERT_TRUE(parsed.is_initialized());
  EXPECT_EQ(original, *parsed);
}

TEST(AcceptedRootStateTest, RejectsWrongHeader) {
  EXPECT_FALSE(deserializeAcceptedRootState(serializeStringOnly("cryfs.formatv2.root;0")).is_initialized());
}

TEST(AcceptedRootStateTest, RejectsTruncatedState) {
  const auto serialized = serializeAcceptedRootState(AcceptedRootState{filesystemId(), root()});
  cpputils::Data truncated(serialized.size() - 1);
  std::memcpy(truncated.data(), serialized.data(), truncated.size());

  EXPECT_FALSE(deserializeAcceptedRootState(truncated).is_initialized());
}

TEST(AcceptedRootStateTest, RejectsStateWithTrailingData) {
  const auto serialized = serializeAcceptedRootState(AcceptedRootState{filesystemId(), root()});
  cpputils::Data withTrailingByte(serialized.size() + 1);
  std::memcpy(withTrailingByte.data(), serialized.data(), serialized.size());
  static_cast<char*>(withTrailingByte.data())[serialized.size()] = 0;

  EXPECT_FALSE(deserializeAcceptedRootState(withTrailingByte).is_initialized());
}

TEST(AcceptedRootStateTest, RejectsNullFilesystemId) {
  EXPECT_THROW(
    serializeAcceptedRootState(AcceptedRootState{FilesystemId::Null(), root()}),
    std::runtime_error);
}

TEST(AcceptedRootStateTest, RejectsZeroEpoch) {
  EXPECT_THROW(
    serializeAcceptedRootState(AcceptedRootState{filesystemId(), root(0)}),
    std::runtime_error);
}

TEST(AcceptedRootStateTest, RejectsNullRootId) {
  AcceptedRoot accepted = root();
  accepted.rootId = RootId::Null();

  EXPECT_THROW(
    serializeAcceptedRootState(AcceptedRootState{filesystemId(), accepted}),
    std::runtime_error);
}

TEST(AcceptedRootStateTest, LoadReturnsNoneWhenStateFileIsMissing) {
  const cpputils::TempDir tempDir;
  const AcceptedRootStateStore store(tempDir.path() / "accepted-root", filesystemId());

  EXPECT_FALSE(store.load().is_initialized());
}

TEST(AcceptedRootStateTest, StorePersistsAcceptedRoot) {
  const cpputils::TempDir tempDir;
  const AcceptedRootStateStore store(tempDir.path() / "accepted-root", filesystemId());

  store.store(root());

  const auto loaded = store.load();
  ASSERT_TRUE(loaded.is_initialized());
  EXPECT_EQ(root(), *loaded);
}

TEST(AcceptedRootStateTest, StoreCreatesMissingParentDirectories) {
  const cpputils::TempDir tempDir;
  const auto filepath = tempDir.path() / "state" / "format-v2" / "accepted-root";
  const AcceptedRootStateStore store(filepath, filesystemId());

  store.store(root());

  EXPECT_TRUE(boost::filesystem::is_directory(filepath.parent_path()));
  ASSERT_TRUE(store.load().is_initialized());
  EXPECT_EQ(root(), *store.load());
}

TEST(AcceptedRootStateTest, LoadRejectsMalformedStateFile) {
  const cpputils::TempDir tempDir;
  const auto filepath = tempDir.path() / "accepted-root";
  serializeStringOnly("not accepted-root state").StoreToFile(filepath);
  const AcceptedRootStateStore store(filepath, filesystemId());

  EXPECT_THROW(store.load(), std::runtime_error);
}

TEST(AcceptedRootStateTest, LoadRejectsWrongFilesystemId) {
  const cpputils::TempDir tempDir;
  const auto filepath = tempDir.path() / "accepted-root";
  serializeAcceptedRootState(AcceptedRootState{otherFilesystemId(), root()}).StoreToFile(filepath);
  const AcceptedRootStateStore store(filepath, filesystemId());

  EXPECT_THROW(store.load(), std::runtime_error);
}

TEST(AcceptedRootStateTest, AdvanceStoresRootWhenStateFileIsMissing) {
  const cpputils::TempDir tempDir;
  const AcceptedRootStateStore store(tempDir.path() / "accepted-root", filesystemId());

  store.advanceTo(root());

  ASSERT_TRUE(store.load().is_initialized());
  EXPECT_EQ(root(), *store.load());
}

TEST(AcceptedRootStateTest, AdvanceAllowsSameAcceptedRoot) {
  const cpputils::TempDir tempDir;
  const AcceptedRootStateStore store(tempDir.path() / "accepted-root", filesystemId());
  store.store(root());

  store.advanceTo(root());

  EXPECT_EQ(root(), *store.load());
}

TEST(AcceptedRootStateTest, AdvanceReplacesOlderRoot) {
  const cpputils::TempDir tempDir;
  const AcceptedRootStateStore store(tempDir.path() / "accepted-root", filesystemId());
  store.store(root(7));

  store.advanceTo(otherRoot(8));

  EXPECT_EQ(otherRoot(8), *store.load());
}

TEST(AcceptedRootStateTest, AdvanceRejectsOlderRootAndKeepsCurrentState) {
  const cpputils::TempDir tempDir;
  const AcceptedRootStateStore store(tempDir.path() / "accepted-root", filesystemId());
  store.store(root(7));

  EXPECT_THROW(store.advanceTo(otherRoot(6)), std::runtime_error);

  EXPECT_EQ(root(7), *store.load());
}

TEST(AcceptedRootStateTest, AdvanceRejectsSameEpochDifferentRootAndKeepsCurrentState) {
  const cpputils::TempDir tempDir;
  const AcceptedRootStateStore store(tempDir.path() / "accepted-root", filesystemId());
  store.store(root(7));

  EXPECT_THROW(store.advanceTo(otherRoot(7)), std::runtime_error);

  EXPECT_EQ(root(7), *store.load());
}
