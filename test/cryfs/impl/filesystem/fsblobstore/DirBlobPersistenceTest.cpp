#include <gtest/gtest.h>

#include <blobstore/implementations/onblocks/BlobStoreOnBlocks.h>
#include <blockstore/implementations/testfake/FakeBlockStore.h>
#include <cryfs/impl/filesystem/fsblobstore/DirBlob.h>
#include <cryfs/impl/filesystem/fsblobstore/FsBlobStore.h>

namespace {

fspp::mode_t publicMode() {
  return fspp::mode_t()
    .addUserReadFlag().addUserWriteFlag().addUserExecFlag()
    .addGroupReadFlag().addGroupWriteFlag().addGroupExecFlag()
    .addOtherReadFlag().addOtherWriteFlag().addOtherExecFlag();
}

timespec timestamp() {
  return timespec{0, 0};
}

cryfs::fsblobstore::FsBlobStore createFsBlobStore() {
  auto blockStore = cpputils::make_unique_ref<blockstore::testfake::FakeBlockStore>();
  auto blobStore = cpputils::make_unique_ref<blobstore::onblocks::BlobStoreOnBlocks>(std::move(blockStore), 4096);
  return cryfs::fsblobstore::FsBlobStore(std::move(blobStore));
}

TEST(DirBlobPersistenceTest, AddChildFileIsVisibleBeforeDirBlobDestruction) {
  auto fsBlobStore = createFsBlobStore();
  const auto parentId = blockstore::BlockId::FromString("1491BB4932A389EE14BC7090AC772972");
  const auto childId = blockstore::BlockId::FromString("AC772971491BB4932A389EE14BC7090A");
  auto dir = fsBlobStore.createDirBlob(parentId);
  const auto dirId = dir->blockId();

  dir->AddChildFile("child", childId, publicMode(), fspp::uid_t(0), fspp::gid_t(0), timestamp(), timestamp());

  auto loaded = fsBlobStore.load(dirId).value();
  auto loadedDir = dynamic_cast<cryfs::fsblobstore::DirBlob*>(loaded.get());
  ASSERT_NE(nullptr, loadedDir);
  ASSERT_TRUE(loadedDir->GetChild("child").is_initialized());
  EXPECT_EQ(childId, loadedDir->GetChild("child")->blockId());
}

}
