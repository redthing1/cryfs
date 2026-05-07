#include <gtest/gtest.h>
#include "blockstore/implementations/ondisk/OnDiskBlockStore2.h"
#include <cpp-utils/tempfile/TempDir.h>
#include <boost/filesystem.hpp>

#include <cstddef>
#include <fstream>
#include <set>
#include <string_view>

using ::testing::Test;

using cpputils::TempDir;
using cpputils::Data;
using std::ifstream;
using std::ofstream;
using std::set;
using blockstore::BlockId;

using namespace blockstore::ondisk;

class OnDiskBlockStoreTest: public Test {
public:
    OnDiskBlockStoreTest():
    baseDir(),
    blockStore(baseDir.path()) {
  }
  TempDir baseDir;
  OnDiskBlockStore2 blockStore;

  blockstore::BlockId CreateBlockReturnKey(const Data &initData) {
    return blockStore.create(initData.copy());
  }

  boost::filesystem::path getPrefixDir(const boost::filesystem::path &rootDir, const BlockId &blockId) {
    return rootDir / blockId.ToString().substr(0, 3);
  }

  boost::filesystem::path getPrefixDir(const BlockId &blockId) {
    return getPrefixDir(baseDir.path(), blockId);
  }

  boost::filesystem::path getBlockFilepath(const boost::filesystem::path &rootDir, const BlockId &blockId) {
    return getPrefixDir(rootDir, blockId) / blockId.ToString().substr(3);
  }

  boost::filesystem::path getBlockFilepath(const BlockId &blockId) {
    return getBlockFilepath(baseDir.path(), blockId);
  }

  uint64_t getPhysicalBlockSize(const BlockId &blockId) {
    ifstream stream(getBlockFilepath(blockId).c_str());
    stream.seekg(0, stream.end);
    return stream.tellg();
  }

  void writeRawBlockFile(const BlockId &blockId, const void *data, size_t size) {
    std::string const idStr = blockId.ToString();
    auto dir = baseDir.path() / idStr.substr(0, 3);
    boost::filesystem::create_directories(dir);
    auto filepath = dir / idStr.substr(3);
    ofstream file(filepath.string().c_str(), std::ios::binary | std::ios::trunc);
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  }

  void writeFile(const boost::filesystem::path &filepath) {
    boost::filesystem::create_directories(filepath.parent_path());
    ofstream file(filepath.string().c_str(), std::ios::binary | std::ios::trunc);
    file << "content";
  }
};

TEST_F(OnDiskBlockStoreTest, PhysicalBlockSize_zerophysical) {
  EXPECT_EQ(0u, blockStore.blockSizeFromPhysicalBlockSize(0));
}

TEST_F(OnDiskBlockStoreTest, PhysicalBlockSize_zerovirtual) {
  auto blockId = CreateBlockReturnKey(Data(0));
  auto baseSize = getPhysicalBlockSize(blockId);
  EXPECT_EQ(0u, blockStore.blockSizeFromPhysicalBlockSize(baseSize));
}

TEST_F(OnDiskBlockStoreTest, PhysicalBlockSize_negativeboundaries) {
  // This tests that a potential if/else in blockSizeFromPhysicalBlockSize that catches negative values has the
  // correct boundary set. We test the highest value that is negative and the smallest value that is positive.
  auto physicalSizeForVirtualSizeZero = getPhysicalBlockSize(CreateBlockReturnKey(Data(0)));
  if (physicalSizeForVirtualSizeZero > 0) {
    EXPECT_EQ(0u, blockStore.blockSizeFromPhysicalBlockSize(physicalSizeForVirtualSizeZero - 1));
  }
  EXPECT_EQ(0u, blockStore.blockSizeFromPhysicalBlockSize(physicalSizeForVirtualSizeZero));
  EXPECT_EQ(1u, blockStore.blockSizeFromPhysicalBlockSize(physicalSizeForVirtualSizeZero + 1));
}

TEST_F(OnDiskBlockStoreTest, PhysicalBlockSize_positive) {
  auto blockId = CreateBlockReturnKey(Data(10*1024));
  auto baseSize = getPhysicalBlockSize(blockId);
  EXPECT_EQ(10*1024u, blockStore.blockSizeFromPhysicalBlockSize(baseSize));
}

TEST_F(OnDiskBlockStoreTest, NumBlocksIsCorrectAfterAddingTwoBlocksWithSameKeyPrefix) {
  const BlockId key1 = BlockId::FromString("4CE72ECDD20877A12ADBF4E3927C0A13");
  const BlockId key2 = BlockId::FromString("4CE72ECDD20877A12ADBF4E3927C0A14");
  EXPECT_TRUE(blockStore.tryCreate(key1, cpputils::Data(0)));
  EXPECT_TRUE(blockStore.tryCreate(key2, cpputils::Data(0)));
  EXPECT_EQ(2u, blockStore.numBlocks());
}

TEST_F(OnDiskBlockStoreTest, NumBlocksIgnoresNonBlockEntries) {
  const BlockId blockId = BlockId::FromString("AB0123456789ABCDEF0123456789AB01");
  ASSERT_TRUE(blockStore.tryCreate(blockId, cpputils::Data(0)));
  writeFile(getBlockFilepath(blockId).string() + ".tmp.123.0");
  writeFile(baseDir.path() / "ABC" / "not-a-block");
  boost::filesystem::create_directories(baseDir.path() / "ABC" / "0123456789ABCDEF0123456789A");
  writeFile(baseDir.path() / "not-a-prefix" / "0123456789ABCDEF0123456789A");

  EXPECT_EQ(1u, blockStore.numBlocks());
}

TEST_F(OnDiskBlockStoreTest, ForEachBlockIgnoresNonBlockEntries) {
  const BlockId blockId = BlockId::FromString("AB0123456789ABCDEF0123456789AB01");
  ASSERT_TRUE(blockStore.tryCreate(blockId, cpputils::Data(0)));
  writeFile(getBlockFilepath(blockId).string() + ".tmp.123.0");
  writeFile(baseDir.path() / "ABC" / "not-a-block");
  boost::filesystem::create_directories(baseDir.path() / "ABC" / "0123456789ABCDEF0123456789A");
  writeFile(baseDir.path() / "not-a-prefix" / "0123456789ABCDEF0123456789A");
  set<BlockId> seenBlocks;

  blockStore.forEachBlock([&seenBlocks] (const BlockId &seenBlockId) {
    seenBlocks.insert(seenBlockId);
  });

  EXPECT_EQ(set<BlockId>{blockId}, seenBlocks);
}

TEST_F(OnDiskBlockStoreTest, TryCreateExistingBlockDoesNotOverwriteData) {
  const BlockId blockId = BlockId::FromString("AB0123456789ABCDEF0123456789AB01");
  const Data original(8);
  const Data replacement(11);
  ASSERT_TRUE(blockStore.tryCreate(blockId, original));

  EXPECT_FALSE(blockStore.tryCreate(blockId, replacement));

  EXPECT_EQ(original, blockStore.load(blockId).value());
}

TEST_F(OnDiskBlockStoreTest, RemoveDeletesBlockFileAndEmptyPrefixDirectory) {
  const BlockId blockId = BlockId::FromString("AB0123456789ABCDEF0123456789AB01");
  ASSERT_TRUE(blockStore.tryCreate(blockId, cpputils::Data(0)));
  ASSERT_TRUE(boost::filesystem::exists(getBlockFilepath(blockId)));
  ASSERT_TRUE(boost::filesystem::exists(getPrefixDir(blockId)));

  EXPECT_TRUE(blockStore.remove(blockId));

  EXPECT_FALSE(boost::filesystem::exists(getBlockFilepath(blockId)));
  EXPECT_FALSE(boost::filesystem::exists(getPrefixDir(blockId)));
}

TEST_F(OnDiskBlockStoreTest, RemoveKeepsNonEmptyPrefixDirectory) {
  const BlockId key1 = BlockId::FromString("4CE72ECDD20877A12ADBF4E3927C0A13");
  const BlockId key2 = BlockId::FromString("4CE72ECDD20877A12ADBF4E3927C0A14");
  ASSERT_TRUE(blockStore.tryCreate(key1, cpputils::Data(0)));
  ASSERT_TRUE(blockStore.tryCreate(key2, cpputils::Data(1)));

  EXPECT_TRUE(blockStore.remove(key1));

  EXPECT_FALSE(boost::filesystem::exists(getBlockFilepath(key1)));
  EXPECT_TRUE(boost::filesystem::exists(getPrefixDir(key1)));
  EXPECT_TRUE(boost::filesystem::exists(getBlockFilepath(key2)));
  EXPECT_TRUE(blockStore.load(key2).is_initialized());
}

TEST_F(OnDiskBlockStoreTest, RemoveMissingBlockReturnsFalse) {
  const BlockId blockId = BlockId::FromString("AB0123456789ABCDEF0123456789AB01");

  EXPECT_FALSE(blockStore.remove(blockId));
}

TEST_F(OnDiskBlockStoreTest, ConstructorRemovesStaleTemporaryBlockFiles) {
  const TempDir startupDir;
  const BlockId blockId = BlockId::FromString("AB0123456789ABCDEF0123456789AB01");
  const auto temporaryPath = boost::filesystem::path(getBlockFilepath(startupDir.path(), blockId).string() + ".tmp.123.0");
  writeFile(temporaryPath);
  ASSERT_TRUE(boost::filesystem::is_regular_file(temporaryPath));

  OnDiskBlockStore2 reopenedBlockStore(startupDir.path());

  EXPECT_FALSE(boost::filesystem::exists(temporaryPath));
  EXPECT_FALSE(boost::filesystem::exists(getPrefixDir(startupDir.path(), blockId)));
}

TEST_F(OnDiskBlockStoreTest, ConstructorKeepsBlockFilesAndUnrecognizedFiles) {
  const TempDir startupDir;
  const BlockId blockId = BlockId::FromString("AB0123456789ABCDEF0123456789AB01");
  const auto blockPath = getBlockFilepath(startupDir.path(), blockId);
  const auto unrecognizedPath = boost::filesystem::path(blockPath.string() + ".tmp.pid.0");
  writeFile(blockPath);
  writeFile(unrecognizedPath);

  OnDiskBlockStore2 reopenedBlockStore(startupDir.path());

  EXPECT_TRUE(boost::filesystem::exists(blockPath));
  EXPECT_TRUE(boost::filesystem::exists(unrecognizedPath));
}

TEST_F(OnDiskBlockStoreTest, LoadingBlockWithEmptyFile_ThrowsError) {
  const BlockId blockId = BlockId::FromString("AB0123456789ABCDEF0123456789AB01");
  writeRawBlockFile(blockId, "", 0);
  EXPECT_THROW(blockStore.load(blockId), std::runtime_error);
}

TEST_F(OnDiskBlockStoreTest, LoadingBlockWithUndersizedFile_ThrowsError) {
  const BlockId blockId = BlockId::FromString("AB0123456789ABCDEF0123456789AB01");
  constexpr std::string_view shortData = "cryfs";
  writeRawBlockFile(blockId, shortData.data(), shortData.size());
  EXPECT_THROW(blockStore.load(blockId), std::runtime_error);
}

TEST_F(OnDiskBlockStoreTest, LoadingBlockWithSizeBetweenPrefixAndFullHeader_ThrowsError) {
  // Data larger than FORMAT_VERSION_HEADER_PREFIX but smaller than full header
  const BlockId blockId = BlockId::FromString("AB0123456789ABCDEF0123456789AB01");
  constexpr std::string_view partialHeader = "cryfs;block;";
  writeRawBlockFile(blockId, partialHeader.data(), partialHeader.size());
  EXPECT_THROW(blockStore.load(blockId), std::runtime_error);
}
