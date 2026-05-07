#include <gtest/gtest.h>
#include "cpp-utils/system/AtomicFile.h"
#include "cpp-utils/tempfile/TempDir.h"
#include <boost/filesystem.hpp>
#include <fstream>
#include <string>
#include <system_error>

using cpputils::TempDir;

namespace {
std::string readFile(const boost::filesystem::path &filepath) {
  std::ifstream file(filepath.string().c_str(), std::ios::binary);
  return std::string(
    std::istreambuf_iterator<char>(file),
    std::istreambuf_iterator<char>());
}

#if !defined(_WIN32)
class ScopedPermissions final {
public:
  ScopedPermissions(const boost::filesystem::path &path, boost::filesystem::perms permissions)
    : _path(path),
      _originalPermissions(boost::filesystem::status(path).permissions()) {
    boost::filesystem::permissions(_path, permissions);
  }

  ~ScopedPermissions() {
    boost::system::error_code ignored;
    boost::filesystem::permissions(_path, _originalPermissions, ignored);
  }

private:
  boost::filesystem::path _path;
  boost::filesystem::perms _originalPermissions;
};

bool directorySyncFails(const boost::filesystem::path &directory) {
  try {
    cpputils::syncDirectory(directory);
    return false;
  } catch (const std::system_error &) {
    return true;
  }
}
#endif
}

TEST(AtomicFileTest, storeFileAtomicallyIfAbsentCreatesFile) {
  const TempDir tempDir;
  const auto filepath = tempDir.path() / "created";
  const std::string content = "content";

  EXPECT_TRUE(cpputils::storeFileAtomicallyIfAbsent(filepath, content.data(), content.size()));

  EXPECT_EQ(content, readFile(filepath));
}

TEST(AtomicFileTest, storeFileAtomicallyIfAbsentReturnsFalseAndKeepsExistingFile) {
  const TempDir tempDir;
  const auto filepath = tempDir.path() / "created";
  const std::string original = "original";
  const std::string replacement = "replacement";
  ASSERT_TRUE(cpputils::storeFileAtomicallyIfAbsent(filepath, original.data(), original.size()));

  EXPECT_FALSE(cpputils::storeFileAtomicallyIfAbsent(filepath, replacement.data(), replacement.size()));

  EXPECT_EQ(original, readFile(filepath));
  for (const auto &entry: boost::filesystem::directory_iterator(tempDir.path())) {
    EXPECT_FALSE(cpputils::isAtomicFileTemporaryFileFor(filepath, entry.path()));
  }
}

#if !defined(_WIN32)
TEST(AtomicFileTest, storeFileAtomicallyPropagatesDirectorySyncFailure) {
  const TempDir tempDir;
  const auto filepath = tempDir.path() / "stored";
  const std::string content = "content";
  const ScopedPermissions permissions(
    tempDir.path(),
    boost::filesystem::owner_write | boost::filesystem::owner_exe);
  if (!directorySyncFails(tempDir.path())) {
    GTEST_SKIP() << "Directory fsync succeeds despite removed read permission";
  }

  EXPECT_THROW(cpputils::storeFileAtomically(filepath, content.data(), content.size()), std::system_error);
}

TEST(AtomicFileTest, storeFileAtomicallyIfAbsentPropagatesDirectorySyncFailureAfterCreate) {
  const TempDir tempDir;
  const auto filepath = tempDir.path() / "created";
  const std::string content = "content";
  const ScopedPermissions permissions(
    tempDir.path(),
    boost::filesystem::owner_write | boost::filesystem::owner_exe);
  if (!directorySyncFails(tempDir.path())) {
    GTEST_SKIP() << "Directory fsync succeeds despite removed read permission";
  }

  EXPECT_THROW(cpputils::storeFileAtomicallyIfAbsent(filepath, content.data(), content.size()), std::system_error);
}

TEST(AtomicFileTest, storeFileAtomicallyIfAbsentPropagatesDirectorySyncFailureWhenFileExists) {
  const TempDir tempDir;
  const auto filepath = tempDir.path() / "created";
  const std::string original = "original";
  const std::string replacement = "replacement";
  ASSERT_TRUE(cpputils::storeFileAtomicallyIfAbsent(filepath, original.data(), original.size()));
  const ScopedPermissions permissions(
    tempDir.path(),
    boost::filesystem::owner_write | boost::filesystem::owner_exe);
  if (!directorySyncFails(tempDir.path())) {
    GTEST_SKIP() << "Directory fsync succeeds despite removed read permission";
  }

  EXPECT_THROW(cpputils::storeFileAtomicallyIfAbsent(filepath, replacement.data(), replacement.size()), std::system_error);
}
#endif

TEST(AtomicFileTest, isAtomicFileTemporaryFileForRecognizesHelperTemporaryName) {
  const TempDir tempDir;
  const auto filepath = tempDir.path() / "data";
  const auto temporaryPath = tempDir.path() / "data.tmp.123.0";

  EXPECT_TRUE(cpputils::isAtomicFileTemporaryFileFor(filepath, temporaryPath));
}

TEST(AtomicFileTest, isAtomicFileTemporaryFileForRejectsOtherNames) {
  const TempDir tempDir;
  const auto filepath = tempDir.path() / "data";

  EXPECT_FALSE(cpputils::isAtomicFileTemporaryFileFor(filepath, tempDir.path() / "other.tmp.123.0"));
  EXPECT_FALSE(cpputils::isAtomicFileTemporaryFileFor(filepath, tempDir.path() / "data.tmp.pid.0"));
  EXPECT_FALSE(cpputils::isAtomicFileTemporaryFileFor(filepath, tempDir.path() / "data.tmp.123"));
  EXPECT_FALSE(cpputils::isAtomicFileTemporaryFileFor(filepath, tempDir.path() / "data.tmp.123."));
  EXPECT_FALSE(cpputils::isAtomicFileTemporaryFileFor(filepath, tempDir.path() / "child" / "data.tmp.123.0"));
}

TEST(AtomicFileTest, createDirectoryDurablyCreatesDirectory) {
  const TempDir tempDir;
  const auto directory = tempDir.path() / "created";

  EXPECT_TRUE(cpputils::createDirectoryDurably(directory));

  EXPECT_TRUE(boost::filesystem::is_directory(directory));
}

TEST(AtomicFileTest, createDirectoryDurablyReturnsFalseForExistingDirectory) {
  const TempDir tempDir;
  const auto directory = tempDir.path() / "created";
  ASSERT_TRUE(cpputils::createDirectoryDurably(directory));

  EXPECT_FALSE(cpputils::createDirectoryDurably(directory));

  EXPECT_TRUE(boost::filesystem::is_directory(directory));
}

TEST(AtomicFileTest, createDirectoryDurablyThrowsForExistingFile) {
  const TempDir tempDir;
  const auto filepath = tempDir.path() / "not-a-directory";
  std::ofstream file(filepath.string().c_str(), std::ios::binary);
  file << "content";
  file.close();
  ASSERT_TRUE(boost::filesystem::is_regular_file(filepath));

  EXPECT_THROW(cpputils::createDirectoryDurably(filepath), std::system_error);
}

#if !defined(_WIN32)
TEST(AtomicFileTest, createDirectoryDurablyPropagatesDirectorySyncFailure) {
  const TempDir tempDir;
  const auto directory = tempDir.path() / "created";
  const ScopedPermissions permissions(
    tempDir.path(),
    boost::filesystem::owner_write | boost::filesystem::owner_exe);
  if (!directorySyncFails(tempDir.path())) {
    GTEST_SKIP() << "Directory fsync succeeds despite removed read permission";
  }

  EXPECT_THROW(cpputils::createDirectoryDurably(directory), std::system_error);
}
#endif

TEST(AtomicFileTest, createDirectoryTreeDurablyCreatesNestedDirectories) {
  const TempDir tempDir;
  const auto directory = tempDir.path() / "parent" / "child" / "grandchild";

  EXPECT_TRUE(cpputils::createDirectoryTreeDurably(directory));

  EXPECT_TRUE(boost::filesystem::is_directory(directory));
}

TEST(AtomicFileTest, createDirectoryTreeDurablyReturnsFalseForExistingDirectory) {
  const TempDir tempDir;
  const auto directory = tempDir.path() / "parent" / "child";
  ASSERT_TRUE(cpputils::createDirectoryTreeDurably(directory));

  EXPECT_FALSE(cpputils::createDirectoryTreeDurably(directory));

  EXPECT_TRUE(boost::filesystem::is_directory(directory));
}

TEST(AtomicFileTest, createDirectoryTreeDurablyThrowsWhenParentIsFile) {
  const TempDir tempDir;
  const auto parent = tempDir.path() / "parent";
  std::ofstream file(parent.string().c_str(), std::ios::binary);
  file << "content";
  file.close();
  ASSERT_TRUE(boost::filesystem::is_regular_file(parent));

  EXPECT_THROW(cpputils::createDirectoryTreeDurably(parent / "child"), std::system_error);
}

TEST(AtomicFileTest, removeFileDurablyRemovesFile) {
  const TempDir tempDir;
  const auto filepath = tempDir.path() / "file";
  std::ofstream file(filepath.string().c_str(), std::ios::binary);
  file << "content";
  file.close();
  ASSERT_TRUE(boost::filesystem::is_regular_file(filepath));

  EXPECT_TRUE(cpputils::removeFileDurably(filepath));

  EXPECT_FALSE(boost::filesystem::exists(filepath));
}

TEST(AtomicFileTest, removeFileDurablyReturnsFalseForMissingFile) {
  const TempDir tempDir;
  const auto filepath = tempDir.path() / "missing";

  EXPECT_FALSE(cpputils::removeFileDurably(filepath));
}

#if !defined(_WIN32)
TEST(AtomicFileTest, removeFileDurablyPropagatesDirectorySyncFailure) {
  const TempDir tempDir;
  const auto filepath = tempDir.path() / "file";
  std::ofstream file(filepath.string().c_str(), std::ios::binary);
  file << "content";
  file.close();
  ASSERT_TRUE(boost::filesystem::is_regular_file(filepath));
  const ScopedPermissions permissions(
    tempDir.path(),
    boost::filesystem::owner_write | boost::filesystem::owner_exe);
  if (!directorySyncFails(tempDir.path())) {
    GTEST_SKIP() << "Directory fsync succeeds despite removed read permission";
  }

  EXPECT_THROW(cpputils::removeFileDurably(filepath), std::system_error);
}
#endif

TEST(AtomicFileTest, removeDirectoryIfEmptyDurablyRemovesEmptyDirectory) {
  const TempDir tempDir;
  const auto directory = tempDir.path() / "empty";
  ASSERT_TRUE(cpputils::createDirectoryDurably(directory));

  EXPECT_TRUE(cpputils::removeDirectoryIfEmptyDurably(directory));

  EXPECT_FALSE(boost::filesystem::exists(directory));
}

TEST(AtomicFileTest, removeDirectoryIfEmptyDurablyReturnsFalseForMissingDirectory) {
  const TempDir tempDir;
  const auto directory = tempDir.path() / "missing";

  EXPECT_FALSE(cpputils::removeDirectoryIfEmptyDurably(directory));
}

#if !defined(_WIN32)
TEST(AtomicFileTest, removeDirectoryIfEmptyDurablyPropagatesDirectorySyncFailure) {
  const TempDir tempDir;
  const auto directory = tempDir.path() / "empty";
  ASSERT_TRUE(cpputils::createDirectoryDurably(directory));
  const ScopedPermissions permissions(
    tempDir.path(),
    boost::filesystem::owner_write | boost::filesystem::owner_exe);
  if (!directorySyncFails(tempDir.path())) {
    GTEST_SKIP() << "Directory fsync succeeds despite removed read permission";
  }

  EXPECT_THROW(cpputils::removeDirectoryIfEmptyDurably(directory), std::system_error);
}
#endif

TEST(AtomicFileTest, removeDirectoryIfEmptyDurablyReturnsFalseForNonEmptyDirectory) {
  const TempDir tempDir;
  const auto directory = tempDir.path() / "not-empty";
  ASSERT_TRUE(cpputils::createDirectoryDurably(directory));
  std::ofstream file((directory / "file").string().c_str(), std::ios::binary);
  file << "content";
  file.close();

  EXPECT_FALSE(cpputils::removeDirectoryIfEmptyDurably(directory));

  EXPECT_TRUE(boost::filesystem::is_directory(directory));
  EXPECT_TRUE(boost::filesystem::exists(directory / "file"));
}
