#include "testutils/FuseCreateAndOpenTest.h"

using ::testing::_;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Return;
using ::testing::WithParamInterface;
using ::testing::Values;

MATCHER_P(HasOpenFlags, expectedFlags, "") {
  return (arg & expectedFlags) == expectedFlags;
}

class FuseCreateAndOpenFlagsTest: public FuseCreateAndOpenTest, public WithParamInterface<int> {
};
INSTANTIATE_TEST_SUITE_P(FuseCreateAndOpenFlagsTest, FuseCreateAndOpenFlagsTest, Values(O_RDWR, O_RDONLY, O_WRONLY));

TEST_P(FuseCreateAndOpenFlagsTest, testFlags) {
  ReturnDoesntExistOnLstat(FILENAME);
  EXPECT_CALL(*fsimpl, createAndOpenFile(Eq(FILENAME), _, _, _, OpenFlagsEq(GetParam())))
    .Times(1).WillOnce(Return(0));
  //For the syscall to succeed, we also need to give an fstat implementation.
  ReturnIsFileOnFstat(0);

  CreateAndOpenFile(FILENAME, GetParam());
}

TEST_F(FuseCreateAndOpenTest, PassesSynchronousOpenFlags) {
#if defined(O_SYNC)
  ReturnDoesntExistOnLstat(FILENAME);
  EXPECT_CALL(*fsimpl, createAndOpenFile(Eq(FILENAME), _, _, _, AllOf(OpenFlagsEq(O_WRONLY), HasOpenFlags(O_SYNC))))
    .Times(1).WillOnce(Return(0));
  ReturnIsFileOnFstat(0);

  CreateAndOpenFile(FILENAME, O_WRONLY | O_SYNC);
#else
  GTEST_SKIP() << "O_SYNC is not defined on this platform";
#endif
}
