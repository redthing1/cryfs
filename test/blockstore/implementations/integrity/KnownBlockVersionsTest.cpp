#include <gtest/gtest.h>
#include <blockstore/implementations/integrity/KnownBlockVersions.h>
#include <cpp-utils/data/Data.h>
#include <cpp-utils/data/Serializer.h>
#include <cpp-utils/tempfile/TempDir.h>
#include <cpp-utils/tempfile/TempFile.h>
#include <boost/filesystem.hpp>
#include <cstring>
#include <stdexcept>

using blockstore::integrity::KnownBlockVersions;
using blockstore::BlockId;
using cpputils::Data;
using cpputils::Serializer;
using cpputils::TempDir;
using cpputils::TempFile;
using std::unordered_set;

namespace {
constexpr const char *KNOWN_BLOCK_VERSIONS_HEADER = "cryfs.integritydata.knownblockversions;1";
constexpr const char *OLD_KNOWN_BLOCK_VERSIONS_HEADER = "cryfs.integritydata.knownblockversions;0";

void storeStringOnlyStateFile(const boost::filesystem::path &path, const std::string &value) {
    Serializer serializer(Serializer::StringSize(value));
    serializer.writeString(value);
    serializer.finished().StoreToFile(path);
}

void storeOldFormatEmptyStateFile(const boost::filesystem::path &path) {
    Serializer serializer(
        Serializer::StringSize(OLD_KNOWN_BLOCK_VERSIONS_HEADER) +
        sizeof(uint64_t) +
        sizeof(uint64_t));
    serializer.writeString(OLD_KNOWN_BLOCK_VERSIONS_HEADER);
    serializer.writeUint64(0);
    serializer.writeUint64(0);
    serializer.finished().StoreToFile(path);
}

void storePrefixOfStateFile(const boost::filesystem::path &path, size_t size) {
    const Data stored = Data::LoadFromFile(path).value();
    ASSERT_LT(size, stored.size());
    Data truncated(size);
    std::memcpy(truncated.data(), stored.data(), truncated.size());
    truncated.StoreToFile(path);
}

void appendByteToStateFile(const boost::filesystem::path &path, uint8_t byte) {
    const Data stored = Data::LoadFromFile(path).value();
    Data withTrailingByte(stored.size() + 1);
    std::memcpy(withTrailingByte.data(), stored.data(), stored.size());
    *static_cast<uint8_t*>(withTrailingByte.dataOffset(stored.size())) = byte;
    withTrailingByte.StoreToFile(path);
}
}

class KnownBlockVersionsTest : public ::testing::Test {
public:
    KnownBlockVersionsTest() :stateFile(false), testobj(stateFile.path(), myClientId) {}

    blockstore::BlockId blockId = blockstore::BlockId::FromString("1491BB4932A389EE14BC7090AC772972");
    blockstore::BlockId blockId2 = blockstore::BlockId::FromString("C772972491BB4932A1389EE14BC7090A");
    static constexpr uint32_t myClientId = 0x12345678;
    static constexpr uint32_t clientId = 0x23456789;
    static constexpr uint32_t clientId2 = 0x34567890;

    TempFile stateFile;
    KnownBlockVersions testobj;

    void setVersion(KnownBlockVersions *testobj, uint32_t clientId, const blockstore::BlockId &blockId, uint64_t version) {
        if (!testobj->checkAndUpdateVersion(clientId, blockId, version)) {
            throw std::runtime_error("Couldn't increase version");
        }
    }

    void EXPECT_VERSION_IS(uint64_t version, KnownBlockVersions *testobj, blockstore::BlockId &blockId, uint32_t clientId) {
        EXPECT_FALSE(testobj->checkAndUpdateVersion(clientId, blockId, version-1));
        EXPECT_TRUE(testobj->checkAndUpdateVersion(clientId, blockId, version+1));
    }
};

TEST_F(KnownBlockVersionsTest, setandget) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
}

TEST_F(KnownBlockVersionsTest, setandget_isPerClientId) {
    setVersion(&testobj, clientId, blockId, 5);
    setVersion(&testobj, clientId2, blockId, 3);
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
    EXPECT_EQ(3u, testobj.getBlockVersion(clientId2, blockId));
}

TEST_F(KnownBlockVersionsTest, setandget_isPerBlock) {
    setVersion(&testobj, clientId, blockId, 5);
    setVersion(&testobj, clientId, blockId2, 3);
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
    EXPECT_EQ(3u, testobj.getBlockVersion(clientId, blockId2));
}

TEST_F(KnownBlockVersionsTest, setandget_allowsIncreasing) {
    setVersion(&testobj, clientId, blockId, 5);
    setVersion(&testobj, clientId, blockId, 6);
    EXPECT_EQ(6u, testobj.getBlockVersion(clientId, blockId));
}

TEST_F(KnownBlockVersionsTest, setandget_doesntAllowDecreasing) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_ANY_THROW(
      setVersion(&testobj, clientId, blockId, 4);
    );
}

TEST_F(KnownBlockVersionsTest, myClientId_isConsistent) {
    EXPECT_EQ(testobj.myClientId(), testobj.myClientId());
}

TEST_F(KnownBlockVersionsTest, incrementVersion_newentry) {
    auto version = testobj.incrementVersion(blockId);
    EXPECT_EQ(1u, version);
    EXPECT_EQ(1u, testobj.getBlockVersion(testobj.myClientId(), blockId));
}

TEST_F(KnownBlockVersionsTest, incrementVersion_oldentry) {
    setVersion(&testobj, testobj.myClientId(), blockId, 5);
    auto version = testobj.incrementVersion(blockId);
    EXPECT_EQ(6u, version);
    EXPECT_EQ(6u, testobj.getBlockVersion(testobj.myClientId(), blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_newentry) {
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId, 5));
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_sameClientSameVersion) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId, 5));
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_sameClientLowerVersion) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_FALSE(testobj.checkAndUpdateVersion(clientId, blockId, 4));
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_sameClientNewerVersion) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId, 6));
    EXPECT_EQ(6u, testobj.getBlockVersion(clientId, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_differentClientSameVersion) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId2, blockId, 5));
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId2, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_differentClientLowerVersion) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId2, blockId, 3));
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
    EXPECT_EQ(3u, testobj.getBlockVersion(clientId2, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_differentClientHigherVersion) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId2, blockId, 7));
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
    EXPECT_EQ(7u, testobj.getBlockVersion(clientId2, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_oldClientLowerVersion) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId2, blockId, 7));
    EXPECT_FALSE(testobj.checkAndUpdateVersion(clientId, blockId, 3));
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
    EXPECT_EQ(7u, testobj.getBlockVersion(clientId2, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_oldClientSameVersion) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId2, blockId, 7));
    EXPECT_FALSE(testobj.checkAndUpdateVersion(clientId, blockId, 5)); // Don't allow rollback to old client's newest block, if it was superseded by another client
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
    EXPECT_EQ(7u, testobj.getBlockVersion(clientId2, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_oldClientHigherVersion) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId2, blockId, 7));
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId, 6));
    EXPECT_EQ(6u, testobj.getBlockVersion(clientId, blockId));
    EXPECT_EQ(7u, testobj.getBlockVersion(clientId2, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_oldClientLowerVersion_oldClientIsSelf) {
    setVersion(&testobj, testobj.myClientId(), blockId, 5);
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId2, blockId, 7));
    EXPECT_FALSE(testobj.checkAndUpdateVersion(testobj.myClientId(), blockId, 3));
    EXPECT_EQ(5u, testobj.getBlockVersion(testobj.myClientId(), blockId));
    EXPECT_EQ(7u, testobj.getBlockVersion(clientId2, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_oldClientSameVersion_oldClientIsSelf) {
    setVersion(&testobj, testobj.myClientId(), blockId, 5);
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId2, blockId, 7));
    EXPECT_FALSE(testobj.checkAndUpdateVersion(testobj.myClientId(), blockId, 5)); // Don't allow rollback to old client's newest block, if it was superseded by another client
    EXPECT_EQ(5u, testobj.getBlockVersion(testobj.myClientId(), blockId));
    EXPECT_EQ(7u, testobj.getBlockVersion(clientId2, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_oldClientHigherVersion_oldClientIsSelf) {
    setVersion(&testobj, testobj.myClientId(), blockId, 4);
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId2, blockId, 7));
    EXPECT_TRUE(testobj.checkAndUpdateVersion(testobj.myClientId(), blockId, 6));
    EXPECT_EQ(6u, testobj.getBlockVersion(testobj.myClientId(), blockId));
    EXPECT_EQ(7u, testobj.getBlockVersion(clientId2, blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_oldClientLowerVersion_newClientIsSelf) {
    setVersion(&testobj, clientId, blockId, 5);
    setVersion(&testobj, testobj.myClientId(), blockId, 7);
    EXPECT_FALSE(testobj.checkAndUpdateVersion(clientId, blockId, 3));
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
    EXPECT_EQ(7u, testobj.getBlockVersion(testobj.myClientId(), blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_oldClientSameVersion_newClientIsSelf) {
    setVersion(&testobj, clientId, blockId, 5);
    setVersion(&testobj, testobj.myClientId(), blockId, 7);
    EXPECT_FALSE(testobj.checkAndUpdateVersion(clientId, blockId, 5)); // Don't allow rollback to old client's newest block, if it was superseded by another client
    EXPECT_EQ(5u, testobj.getBlockVersion(clientId, blockId));
    EXPECT_EQ(7u, testobj.getBlockVersion(testobj.myClientId(), blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdateVersion_oldentry_oldClientHigherVersion_newClientIsSelf) {
    setVersion(&testobj, clientId, blockId, 5);
    setVersion(&testobj, testobj.myClientId(), blockId, 7);
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId, 6));
    EXPECT_EQ(6u, testobj.getBlockVersion(clientId, blockId));
    EXPECT_EQ(7u, testobj.getBlockVersion(testobj.myClientId(), blockId));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdate_twoEntriesDontInfluenceEachOther_differentKeys) {
    // Setup
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId, 100));
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId2, 100));
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId, 150));

    // Checks
    EXPECT_VERSION_IS(150, &testobj, blockId, clientId);
    EXPECT_VERSION_IS(100, &testobj, blockId2, clientId);
}

TEST_F(KnownBlockVersionsTest, checkAndUpdate_twoEntriesDontInfluenceEachOther_differentClientIds) {
    // Setup
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId, 100));
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId2, blockId, 100));
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId, 150));

    EXPECT_VERSION_IS(150, &testobj, blockId, clientId);
    EXPECT_VERSION_IS(100, &testobj, blockId, clientId2);
}

TEST_F(KnownBlockVersionsTest, checkAndUpdate_allowsRollbackToSameClientWithSameVersionNumber) {
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId, 100));
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId, 100));
}

TEST_F(KnownBlockVersionsTest, checkAndUpdate_doesntAllowRollbackToOldClientWithSameVersionNumber) {
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId, blockId, 100));
    EXPECT_TRUE(testobj.checkAndUpdateVersion(clientId2, blockId, 10));
    EXPECT_FALSE(testobj.checkAndUpdateVersion(clientId, blockId, 100));
}

TEST_F(KnownBlockVersionsTest, saveAndLoad_empty) {
    const TempFile stateFile(false);
    {
      const KnownBlockVersions _1(stateFile.path(), myClientId);
      _1.save();
    }

    EXPECT_TRUE(KnownBlockVersions(stateFile.path(), myClientId).checkAndUpdateVersion(clientId, blockId, 1));
}

TEST_F(KnownBlockVersionsTest, saveWithoutChangesDoesNotCreateStateFile) {
    const TempFile stateFile(false);
    KnownBlockVersions obj(stateFile.path(), myClientId);

    obj.save();

    EXPECT_FALSE(boost::filesystem::exists(stateFile.path()));
}

TEST_F(KnownBlockVersionsTest, loadExistingNonRegularStatePathThrows) {
    const TempDir tempDir;

    EXPECT_THROW(KnownBlockVersions(tempDir.path(), myClientId), std::runtime_error);
}

TEST_F(KnownBlockVersionsTest, saveAndLoad_oneentry) {
    const TempFile stateFile(false);
    {
        KnownBlockVersions obj(stateFile.path(), myClientId);
        EXPECT_TRUE(obj.checkAndUpdateVersion(clientId, blockId, 100));
        obj.save();
    }

    const KnownBlockVersions obj(stateFile.path(), myClientId);
    EXPECT_EQ(100u, obj.getBlockVersion(clientId, blockId));
}

TEST_F(KnownBlockVersionsTest, destructorDoesNotPersistState) {
    const TempFile stateFile(false);
    {
      KnownBlockVersions obj(stateFile.path(), myClientId);
      EXPECT_TRUE(obj.checkAndUpdateVersion(clientId, blockId, 100));
    }

    EXPECT_FALSE(boost::filesystem::exists(stateFile.path()));
}

TEST_F(KnownBlockVersionsTest, save_persistsStateBeforeDestructor) {
    const TempFile stateFile(false);
    KnownBlockVersions obj(stateFile.path(), myClientId);
    EXPECT_TRUE(obj.checkAndUpdateVersion(clientId, blockId, 100));

    obj.save();

    const KnownBlockVersions loaded(stateFile.path(), myClientId);
    EXPECT_EQ(100u, loaded.getBlockVersion(clientId, blockId));
}

TEST_F(KnownBlockVersionsTest, saveFailureLeavesChangesPendingForRetry) {
    const TempDir tempDir;
    const auto stateFilePath = tempDir.path() / "missing-parent" / "known-block-versions";
    KnownBlockVersions obj(stateFilePath, myClientId);
    EXPECT_TRUE(obj.checkAndUpdateVersion(clientId, blockId, 100));

    EXPECT_THROW(obj.save(), std::runtime_error);
    ASSERT_TRUE(boost::filesystem::create_directory(stateFilePath.parent_path()));
    obj.save();

    const KnownBlockVersions loaded(stateFilePath, myClientId);
    EXPECT_EQ(100u, loaded.getBlockVersion(clientId, blockId));
}

TEST_F(KnownBlockVersionsTest, loadInvalidHeaderThrows) {
    const TempFile stateFile(false);
    storeStringOnlyStateFile(stateFile.path(), "not-cryfs-integrity-state");

    EXPECT_THROW(KnownBlockVersions(stateFile.path(), myClientId), std::runtime_error);
}

TEST_F(KnownBlockVersionsTest, loadOldHeaderThrows) {
    const TempFile stateFile(false);
    storeOldFormatEmptyStateFile(stateFile.path());

    EXPECT_THROW(KnownBlockVersions(stateFile.path(), myClientId), std::runtime_error);
}

TEST_F(KnownBlockVersionsTest, loadTrailingGarbageThrows) {
    const TempFile stateFile(false);
    {
        KnownBlockVersions obj(stateFile.path(), myClientId);
        EXPECT_TRUE(obj.checkAndUpdateVersion(clientId, blockId, 100));
        obj.save();
    }
    appendByteToStateFile(stateFile.path(), 0x42);

    EXPECT_THROW(KnownBlockVersions(stateFile.path(), myClientId), std::runtime_error);
}

TEST_F(KnownBlockVersionsTest, loadTruncatedFixedSizeBlockIdThrows) {
    const TempFile stateFile(false);
    {
        KnownBlockVersions obj(stateFile.path(), myClientId);
        EXPECT_TRUE(obj.checkAndUpdateVersion(clientId, blockId, 100));
        obj.save();
    }
    const size_t truncatedInsideKnownBlockId =
        Serializer::StringSize(KNOWN_BLOCK_VERSIONS_HEADER) +
        Serializer::BoolSize() +
        sizeof(uint64_t) +
        sizeof(uint32_t) +
        BlockId::BINARY_LENGTH / 2;
    storePrefixOfStateFile(stateFile.path(), truncatedInsideKnownBlockId);

    EXPECT_THROW(KnownBlockVersions(stateFile.path(), myClientId), std::runtime_error);
}

TEST_F(KnownBlockVersionsTest, saveAndLoad_threeentries) {
    const TempFile stateFile(false);
    {
        KnownBlockVersions obj(stateFile.path(), myClientId);
        EXPECT_TRUE(obj.checkAndUpdateVersion(obj.myClientId(), blockId, 100));
        EXPECT_TRUE(obj.checkAndUpdateVersion(obj.myClientId(), blockId2, 50));
        EXPECT_TRUE(obj.checkAndUpdateVersion(clientId, blockId, 150));
        obj.save();
    }

    const KnownBlockVersions obj(stateFile.path(), myClientId);
    EXPECT_EQ(100u, obj.getBlockVersion(obj.myClientId(), blockId));
    EXPECT_EQ(50u, obj.getBlockVersion(obj.myClientId(), blockId2));
    EXPECT_EQ(150u, obj.getBlockVersion(clientId, blockId));
}

TEST_F(KnownBlockVersionsTest, saveAndLoad_lastUpdateClientIdIsStored) {
    {
        KnownBlockVersions obj(stateFile.path(), myClientId);
        EXPECT_TRUE(obj.checkAndUpdateVersion(clientId, blockId, 100));
        EXPECT_TRUE(obj.checkAndUpdateVersion(clientId2, blockId, 10));
        obj.save();
    }

    KnownBlockVersions obj(stateFile.path(), myClientId);
    EXPECT_FALSE(obj.checkAndUpdateVersion(clientId, blockId, 100));
    EXPECT_TRUE(obj.checkAndUpdateVersion(clientId2, blockId, 10));
    EXPECT_TRUE(obj.checkAndUpdateVersion(clientId, blockId, 101));
}

TEST_F(KnownBlockVersionsTest, markAsDeleted_doesntAllowReIntroducing_sameClientId) {
    setVersion(&testobj, clientId, blockId, 5);
    testobj.markBlockAsDeleted(blockId);
    EXPECT_FALSE(testobj.checkAndUpdateVersion(clientId, blockId, 5));
}

TEST_F(KnownBlockVersionsTest, markAsDeleted_doesntAllowReIntroducing_oldClientId) {
    setVersion(&testobj, clientId, blockId, 5);
    setVersion(&testobj, clientId2, blockId, 5);
    testobj.markBlockAsDeleted(blockId);
    EXPECT_FALSE(testobj.checkAndUpdateVersion(clientId, blockId, 5));
}

TEST_F(KnownBlockVersionsTest, markAsDeleted_checkAndUpdateDoesntDestroyState) {
    setVersion(&testobj, clientId, blockId, 5);
    setVersion(&testobj, clientId2, blockId, 5);
    testobj.markBlockAsDeleted(blockId);
    EXPECT_FALSE(testobj.checkAndUpdateVersion(clientId, blockId, 5));

    // Check block is still deleted
    EXPECT_FALSE(testobj.blockShouldExist(blockId));
}

TEST_F(KnownBlockVersionsTest, blockShouldExist_unknownBlock) {
    EXPECT_FALSE(testobj.blockShouldExist(blockId));
}

TEST_F(KnownBlockVersionsTest, blockShouldExist_knownBlock) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_TRUE(testobj.blockShouldExist(blockId));
}

TEST_F(KnownBlockVersionsTest, blockShouldExist_deletedBlock) {
    setVersion(&testobj, clientId, blockId, 5);
    testobj.markBlockAsDeleted(blockId);
    EXPECT_FALSE(testobj.blockShouldExist(blockId));
}

TEST_F(KnownBlockVersionsTest, path) {
    const KnownBlockVersions obj(stateFile.path(), myClientId);
    EXPECT_EQ(stateFile.path(), obj.path());
}

TEST_F(KnownBlockVersionsTest, existingBlocks_empty) {
    EXPECT_EQ(unordered_set<BlockId>({}), testobj.existingBlocks());
}

TEST_F(KnownBlockVersionsTest, existingBlocks_oneentry) {
    setVersion(&testobj, clientId, blockId, 5);
    EXPECT_EQ(unordered_set<BlockId>({blockId}), testobj.existingBlocks());
}

TEST_F(KnownBlockVersionsTest, existingBlocks_twoentries) {
    setVersion(&testobj, clientId, blockId, 5);
    setVersion(&testobj, clientId2, blockId2, 5);
    EXPECT_EQ(unordered_set<BlockId>({blockId, blockId2}), testobj.existingBlocks());
}

TEST_F(KnownBlockVersionsTest, existingBlocks_twoentries_sameKey) {
    setVersion(&testobj, clientId, blockId, 5);
    setVersion(&testobj, clientId2, blockId, 5);
    EXPECT_EQ(unordered_set<BlockId>({blockId}), testobj.existingBlocks());
}

TEST_F(KnownBlockVersionsTest, existingBlocks_deletedEntry) {
    setVersion(&testobj, clientId, blockId, 5);
    setVersion(&testobj, clientId2, blockId2, 5);
    testobj.markBlockAsDeleted(blockId2);
    EXPECT_EQ(unordered_set<BlockId>({blockId}), testobj.existingBlocks());
}

TEST_F(KnownBlockVersionsTest, existingBlocks_deletedEntries) {
    setVersion(&testobj, clientId, blockId, 5);
    setVersion(&testobj, clientId2, blockId2, 5);
    testobj.markBlockAsDeleted(blockId);
    testobj.markBlockAsDeleted(blockId2);
    EXPECT_EQ(unordered_set<BlockId>({}), testobj.existingBlocks());
}
