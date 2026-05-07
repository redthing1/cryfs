#include "fspp/impl/FilesystemImpl.h"

#include "fspp/fs_interface/Device.h"
#include "fspp/fs_interface/Dir.h"
#include "fspp/fs_interface/File.h"
#include "fspp/fs_interface/FuseErrnoException.h"
#include "fspp/fs_interface/Node.h"
#include "fspp/fs_interface/Symlink.h"
#include "../testutils/ErrnoTestUtils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fcntl.h>
#include <functional>
#include <vector>

using cpputils::make_unique_ref;

namespace {

struct LoadCounts final {
  int loadFile = 0;
  int loadDir = 0;
};

class RejectingDevice final: public fspp::Device {
public:
  explicit RejectingDevice(LoadCounts *counts): _counts(counts) {
  }

  fspp::Device::statvfs statfs() override {
    return {};
  }

  boost::optional<cpputils::unique_ref<fspp::Node>> Load(const boost::filesystem::path &) override {
    return boost::none;
  }

  boost::optional<cpputils::unique_ref<fspp::File>> LoadFile(const boost::filesystem::path &) override {
    ++_counts->loadFile;
    return boost::none;
  }

  boost::optional<cpputils::unique_ref<fspp::Dir>> LoadDir(const boost::filesystem::path &) override {
    ++_counts->loadDir;
    return boost::none;
  }

  boost::optional<cpputils::unique_ref<fspp::Symlink>> LoadSymlink(const boost::filesystem::path &) override {
    return boost::none;
  }

private:
  LoadCounts *_counts;
};

void addUniqueFlag(std::vector<int> *flags, int flag) {
  if (flag != 0 && std::find(flags->begin(), flags->end(), flag) == flags->end()) {
    flags->push_back(flag);
  }
}

std::vector<int> synchronousOpenFlags() {
  std::vector<int> flags;
#if defined(O_SYNC)
  addUniqueFlag(&flags, O_SYNC);
#endif
#if defined(O_DSYNC)
  addUniqueFlag(&flags, O_DSYNC);
#endif
#if defined(O_RSYNC)
  addUniqueFlag(&flags, O_RSYNC);
#endif
  return flags;
}

void expectUnsupportedOperation(const std::function<void()> &operation) {
  try {
    operation();
    FAIL() << "Expected unsupported operation";
  } catch (const fspp::fuse::FuseErrnoException &e) {
    EXPECT_TRUE(fspp_test::isUnsupportedOperationErrno(e.getErrno()));
  }
}

}

TEST(FilesystemImplUnsupportedOpenFlagsTest, OpenRejectsSynchronousFlagsBeforeLoadingFile) {
  const auto flags = synchronousOpenFlags();
  if (flags.empty()) {
    GTEST_SKIP() << "No synchronous open flags are defined on this platform";
  }

  for (const int flag : flags) {
    LoadCounts counts;
    fspp::FilesystemImpl fs(make_unique_ref<RejectingDevice>(&counts));

    expectUnsupportedOperation([&fs, flag] {
      fs.openFile("/file", O_WRONLY | flag);
    });

    EXPECT_EQ(0, counts.loadFile);
  }
}

TEST(FilesystemImplUnsupportedOpenFlagsTest, CreateRejectsSynchronousFlagsBeforeLoadingParentDir) {
  const auto flags = synchronousOpenFlags();
  if (flags.empty()) {
    GTEST_SKIP() << "No synchronous open flags are defined on this platform";
  }

  for (const int flag : flags) {
    LoadCounts counts;
    fspp::FilesystemImpl fs(make_unique_ref<RejectingDevice>(&counts));

    expectUnsupportedOperation([&fs, flag] {
      fs.createAndOpenFile("/file", 0644, 1000, 1000, O_WRONLY | O_CREAT | flag);
    });

    EXPECT_EQ(0, counts.loadDir);
  }
}
