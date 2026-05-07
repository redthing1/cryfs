#include "blockstore/implementations/integrity/IntegrityBlockStore2.h"
#include "blockstore/implementations/low2highlevel/LowToHighLevelBlockStore.h"
#include "blockstore/implementations/inmemory/InMemoryBlockStore2.h"
#include "../../testutils/BlockStoreTest.h"
#include "../../testutils/BlockStore2Test.h"
#include <gtest/gtest.h>
#include <cpp-utils/tempfile/TempFile.h>


using blockstore::BlockStore;
using blockstore::BlockStore2;
using blockstore::integrity::IntegrityBlockStore2;
using blockstore::lowtohighlevel::LowToHighLevelBlockStore;
using blockstore::inmemory::InMemoryBlockStore2;

using cpputils::make_unique_ref;
using cpputils::unique_ref;
using cpputils::TempFile;

class IntegrityBlockStoreTestFixture: public BlockStoreTestFixture {
public:
   IntegrityBlockStoreTestFixture() :stateFile(false) {}

  TempFile stateFile;
  unique_ref<BlockStore> createBlockStore() override {
    return make_unique_ref<LowToHighLevelBlockStore>(
        make_unique_ref<IntegrityBlockStore2>(make_unique_ref<InMemoryBlockStore2>(), stateFile.path(), 0x12345678, [] {})
    );
  }
};

INSTANTIATE_TYPED_TEST_SUITE_P(Integrity, BlockStoreTest, IntegrityBlockStoreTestFixture);

class IntegrityBlockStore2TestFixture: public BlockStore2TestFixture {
public:
  IntegrityBlockStore2TestFixture() :stateFile(false) {}

  TempFile stateFile;
  unique_ref<BlockStore2> createBlockStore() override {
    return make_unique_ref<IntegrityBlockStore2>(make_unique_ref<InMemoryBlockStore2>(), stateFile.path(), 0x12345678, [] {});
  }
};

INSTANTIATE_TYPED_TEST_SUITE_P(Integrity, BlockStore2Test, IntegrityBlockStore2TestFixture);
