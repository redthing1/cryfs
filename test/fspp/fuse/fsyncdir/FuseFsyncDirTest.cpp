#include "../../testutils/FuseTest.h"

#include <cerrno>
#include <dirent.h>
#include <unistd.h>

using ::testing::Eq;
using ::testing::Return;
using ::testing::Throw;

class FuseFsyncDirTest: public FuseTest {
protected:
  int fsyncRootDirAndReturnErrno() {
    auto fs = TestFS();

    DIR *dir = ::opendir(fs->mountDir().string().c_str());
    EXPECT_NE(nullptr, dir) << "Opening mount root directory failed";
    if (dir == nullptr) {
      return errno;
    }

    errno = 0;
    const int result = ::fsync(::dirfd(dir));
    const int syncErrno = result == 0 ? 0 : errno;

    EXPECT_EQ(0, ::closedir(dir));
    return syncErrno;
  }
};

TEST_F(FuseFsyncDirTest, SyncsDirectoryByPath) {
  EXPECT_CALL(*fsimpl, syncDir(Eq("/"))).Times(1).WillOnce(Return());
  EXPECT_EQ(0, fsyncRootDirAndReturnErrno());
}

TEST_F(FuseFsyncDirTest, PropagatesErrors) {
  EXPECT_CALL(*fsimpl, syncDir(Eq("/"))).Times(1)
    .WillOnce(Throw(fspp::fuse::FuseErrnoException(EIO)));
  EXPECT_EQ(EIO, fsyncRootDirAndReturnErrno());
}
