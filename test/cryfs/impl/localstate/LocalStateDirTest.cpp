#include <gtest/gtest.h>

#include <cryfs/impl/localstate/LocalStateDir.h>
#include <cpp-utils/tempfile/TempDir.h>

#include <boost/filesystem.hpp>

namespace {

cryfs::CryConfig::FilesystemID filesystemId() {
  return cryfs::CryConfig::FilesystemID::FromString("1491BB4932A389EE14BC7090AC772972");
}

}

TEST(LocalStateDirTest, forFilesystemIdCreatesNestedStateDirectories) {
  const cpputils::TempDir tempDir;
  const boost::filesystem::path appDir = tempDir.path() / "nested" / "cryfs";
  const cryfs::LocalStateDir localStateDir(appDir);

  const boost::filesystem::path filesystemDir = localStateDir.forFilesystemId(filesystemId());

  EXPECT_EQ(appDir / "filesystems" / filesystemId().ToString(), filesystemDir);
  EXPECT_TRUE(boost::filesystem::is_directory(filesystemDir));
}

TEST(LocalStateDirTest, forBasedirMetadataCreatesNestedAppDirectory) {
  const cpputils::TempDir tempDir;
  const boost::filesystem::path appDir = tempDir.path() / "nested" / "cryfs";
  const cryfs::LocalStateDir localStateDir(appDir);

  const boost::filesystem::path basedirMetadata = localStateDir.forBasedirMetadata();

  EXPECT_EQ(appDir / "basedirs", basedirMetadata);
  EXPECT_TRUE(boost::filesystem::is_directory(appDir));
}
