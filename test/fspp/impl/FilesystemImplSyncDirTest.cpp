#include "fspp/impl/FilesystemImpl.h"

#include "fspp/fs_interface/Device.h"
#include "fspp/fs_interface/Dir.h"
#include "fspp/fs_interface/File.h"
#include "fspp/fs_interface/FuseErrnoException.h"
#include "fspp/fs_interface/Node.h"
#include "fspp/fs_interface/OpenFile.h"
#include "fspp/fs_interface/Symlink.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <stdexcept>
#include <vector>

using cpputils::make_unique_ref;

namespace {

struct SyncDirCounts final {
  int loadDir = 0;
  int dirFsync = 0;
  int deviceSync = 0;
};

class RecordingDir final: public fspp::Dir {
public:
  explicit RecordingDir(SyncDirCounts *counts): _counts(counts) {
  }

  cpputils::unique_ref<fspp::OpenFile> createAndOpenFile(const std::string &, fspp::mode_t, fspp::uid_t, fspp::gid_t) override {
    throw std::logic_error("createAndOpenFile is not used by this test");
  }

  void createDir(const std::string &, fspp::mode_t, fspp::uid_t, fspp::gid_t) override {
    throw std::logic_error("createDir is not used by this test");
  }

  void createSymlink(const std::string &, const boost::filesystem::path &, fspp::uid_t, fspp::gid_t) override {
    throw std::logic_error("createSymlink is not used by this test");
  }

  std::vector<fspp::Dir::Entry> children() override {
    throw std::logic_error("children is not used by this test");
  }

  void fsync() override {
    ++_counts->dirFsync;
  }

private:
  SyncDirCounts *_counts;
};

class SyncDirDevice final: public fspp::Device {
public:
  SyncDirDevice(SyncDirCounts *counts, bool hasDir): _counts(counts), _hasDir(hasDir) {
  }

  fspp::Device::statvfs statfs() override {
    return {};
  }

  boost::optional<cpputils::unique_ref<fspp::Node>> Load(const boost::filesystem::path &) override {
    return boost::none;
  }

  boost::optional<cpputils::unique_ref<fspp::File>> LoadFile(const boost::filesystem::path &) override {
    return boost::none;
  }

  boost::optional<cpputils::unique_ref<fspp::Dir>> LoadDir(const boost::filesystem::path &) override {
    ++_counts->loadDir;
    if (!_hasDir) {
      return boost::none;
    }
    cpputils::unique_ref<fspp::Dir> dir = make_unique_ref<RecordingDir>(_counts);
    return dir;
  }

  boost::optional<cpputils::unique_ref<fspp::Symlink>> LoadSymlink(const boost::filesystem::path &) override {
    return boost::none;
  }

  void sync() const override {
    ++_counts->deviceSync;
  }

private:
  SyncDirCounts *_counts;
  bool _hasDir;
};

}

TEST(FilesystemImplSyncDirTest, LoadsDirectoryAndFsyncsIt) {
  SyncDirCounts counts;
  fspp::FilesystemImpl fs(make_unique_ref<SyncDirDevice>(&counts, true));

  fs.syncDir("/dir");

  EXPECT_EQ(1, counts.loadDir);
  EXPECT_EQ(1, counts.dirFsync);
}

TEST(FilesystemImplSyncDirTest, MissingDirectoryReturnsEio) {
  SyncDirCounts counts;
  fspp::FilesystemImpl fs(make_unique_ref<SyncDirDevice>(&counts, false));

  try {
    fs.syncDir("/missing");
    FAIL() << "Expected EIO for missing directory";
  } catch (const fspp::fuse::FuseErrnoException &e) {
    EXPECT_EQ(EIO, e.getErrno());
  }

  EXPECT_EQ(1, counts.loadDir);
  EXPECT_EQ(0, counts.dirFsync);
}

TEST(FilesystemImplSyncDirTest, DestructorSyncsDevice) {
  SyncDirCounts counts;
  {
    fspp::FilesystemImpl fs(make_unique_ref<SyncDirDevice>(&counts, true));
  }

  EXPECT_EQ(1, counts.deviceSync);
}
