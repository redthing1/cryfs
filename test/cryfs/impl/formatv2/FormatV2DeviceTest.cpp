#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/AcceptedRootState.h>
#include <cryfs/impl/formatv2/FormatV2Device.h>
#include <cryfs/impl/formatv2/KeyDerivation.h>
#include <cryfs/impl/formatv2/RootContentStore.h>
#include <cryfs/impl/formatv2/VolumePathOperations.h>

#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/tempfile/TempDir.h>
#include <fspp/fs_interface/Dir.h>
#include <fspp/fs_interface/File.h>
#include <fspp/fs_interface/FuseErrnoException.h>
#include <fspp/fs_interface/Node.h>
#include <fspp/fs_interface/OpenFile.h>
#include <fspp/fs_interface/Symlink.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using cryfs::formatv2::AcceptedRoot;
using cryfs::formatv2::AcceptedRootStateStore;
using cryfs::formatv2::DirectoryMetadata;
using cryfs::formatv2::FilesystemId;
using cryfs::formatv2::FORMAT_V2_MASTER_KEY_SIZE;
using cryfs::formatv2::FormatV2Device;
using cryfs::formatv2::ObjectId;
using cryfs::formatv2::RootContent;
using cryfs::formatv2::RootContentStore;
using cryfs::formatv2::RootId;
using cryfs::formatv2::RootOpenStatus;
using cryfs::formatv2::Timestamp;
using cryfs::formatv2::VolumeLayout;
using cryfs::formatv2::createOrOpenInitialEmptyVolume;
using cryfs::formatv2::deriveObjectEncryptionKey;
using cryfs::formatv2::publishVolumeCreateDirectoryAtPath;
using cryfs::formatv2::publishVolumeCreateFileWithDataAtPath;
using cryfs::formatv2::publishVolumeCreateSymlinkAtPath;
using cryfs::formatv2::volumeLayout;

namespace {

FilesystemId filesystemId() {
  return FilesystemId::FromString("00112233445566778899AABBCCDDEEFF");
}

RootId rootId(unsigned int seed) {
  return cpputils::DataFixture::generateFixedSize<RootId::BINARY_LENGTH>(seed);
}

ObjectId objectId(unsigned int seed) {
  return cpputils::DataFixture::generateFixedSize<ObjectId::BINARY_LENGTH>(seed);
}

cpputils::EncryptionKey masterKey(unsigned int seed = 1) {
  return cpputils::EncryptionKey::FromString(
    cpputils::DataFixture::generate(FORMAT_V2_MASTER_KEY_SIZE, seed).ToString());
}

template<class Id>
cpputils::Data dataForId(const Id &id) {
  cpputils::Data data(Id::BINARY_LENGTH);
  id.ToBinary(data.data());
  return data;
}

Timestamp timestamp(int64_t seconds, uint32_t nanoseconds) {
  return Timestamp{seconds, nanoseconds};
}

DirectoryMetadata metadata(uint32_t permissions = 0755) {
  return DirectoryMetadata{
    permissions,
    1000,
    1001,
    timestamp(10, 11),
    timestamp(12, 13),
    timestamp(14, 15)
  };
}

cpputils::Data payload(const std::string &value) {
  cpputils::Data data(value.size());
  std::memcpy(data.data(), value.data(), value.size());
  return data;
}

class SequenceRandomGenerator final: public cpputils::RandomGenerator {
public:
  void add(cpputils::Data output) {
    std::lock_guard<std::mutex> lock(_mutex);
    _outputs.emplace_back(std::move(output));
  }

private:
  void _get(void *target, size_t bytes) override {
    std::lock_guard<std::mutex> lock(_mutex);
    ASSERT_LT(_next, _outputs.size());
    ASSERT_EQ(bytes, _outputs[_next].size());
    std::memcpy(target, _outputs[_next].data(), bytes);
    ++_next;
  }

  std::mutex _mutex;
  std::vector<cpputils::Data> _outputs;
  size_t _next = 0;
};

class FormatV2DeviceTest: public ::testing::Test {
protected:
  VolumeLayout layout() const {
    return volumeLayout(_baseDir.path(), _localStateDir.path());
  }

  FormatV2Device device() const {
    return FormatV2Device(layout(), filesystemId(), masterKey());
  }

  FormatV2Device device(cpputils::RandomGenerator *randomGenerator) const {
    return FormatV2Device(layout(), filesystemId(), masterKey(), randomGenerator);
  }

  void createInitialVolume() const {
    SequenceRandomGenerator random;
    random.add(dataForId(rootId(1)));
    random.add(dataForId(objectId(2)));
    const auto created = createOrOpenInitialEmptyVolume(
      layout(),
      filesystemId(),
      masterKey(),
      &random,
      metadata(0700));
    ASSERT_EQ(RootOpenStatus::Selected, created.status);
  }

  void createRootDirectory(
    const std::string &path,
    unsigned int rootSeed,
    unsigned int directorySeed) const {
    SequenceRandomGenerator random;
    random.add(dataForId(rootId(rootSeed)));
    random.add(dataForId(objectId(directorySeed)));
    ASSERT_EQ(
      RootOpenStatus::Selected,
      publishVolumeCreateDirectoryAtPath(
        layout(),
        filesystemId(),
        masterKey(),
        &random,
        path,
        metadata(),
        metadata(0711)).status);
  }

  void createRootFileWithData(
    const std::string &path,
    unsigned int rootSeed,
    unsigned int fileSeed,
    unsigned int dataSeed,
    const std::string &contents) const {
    SequenceRandomGenerator random;
    random.add(dataForId(rootId(rootSeed)));
    random.add(dataForId(objectId(fileSeed)));
    random.add(dataForId(objectId(dataSeed)));
    ASSERT_EQ(
      RootOpenStatus::Selected,
      publishVolumeCreateFileWithDataAtPath(
        layout(),
        filesystemId(),
        masterKey(),
        &random,
        path,
        metadata(),
        metadata(0600),
        payload(contents)).status);
  }

  void createRootSymlink(
    const std::string &path,
    unsigned int rootSeed,
    unsigned int symlinkSeed,
    const std::string &target) const {
    SequenceRandomGenerator random;
    random.add(dataForId(rootId(rootSeed)));
    random.add(dataForId(objectId(symlinkSeed)));
    ASSERT_EQ(
      RootOpenStatus::Selected,
      publishVolumeCreateSymlinkAtPath(
        layout(),
        filesystemId(),
        masterKey(),
        &random,
        path,
        metadata(),
        metadata(0777),
        target).status);
  }

  void storeConflictingRootContent(uint64_t epoch, const RootId &nextRootId) const {
    RootContentStore rootContentStore(
      layout().rootContentDirectory,
      filesystemId(),
      deriveObjectEncryptionKey(masterKey(), filesystemId()));
    rootContentStore.store(RootContent{
      filesystemId(),
      epoch,
      nextRootId,
      objectId(99),
      1
    });
  }

private:
  cpputils::TempDir _baseDir;
  cpputils::TempDir _localStateDir;
};

std::vector<std::string> childNames(const std::vector<fspp::Dir::Entry> &entries) {
  std::vector<std::string> names;
  for (const fspp::Dir::Entry &entry: entries) {
    names.push_back(entry.name);
  }
  return names;
}

std::string payloadString(const cpputils::Data &data) {
  return std::string(static_cast<const char*>(data.data()), data.size());
}

template<class Operation>
void expectFuseErrno(Operation operation, int expectedErrno) {
  try {
    operation();
    FAIL() << "Expected errno " << expectedErrno;
  } catch (const fspp::fuse::FuseErrnoException &e) {
    EXPECT_EQ(expectedErrno, e.getErrno());
  }
}

int staleFileHandleErrno() {
#if defined(ESTALE)
  return ESTALE;
#else
  return EIO;
#endif
}

}

TEST_F(FormatV2DeviceTest, LoadDirReturnsRootDirectoryMetadataAndChildren) {
  createInitialVolume();
  createRootDirectory("/dir", 3, 4);
  createRootFileWithData("/file", 5, 6, 7, "hello");
  createRootSymlink("/link", 8, 9, "../target");

  FormatV2Device formatV2Device = device();
  const auto rootNode = formatV2Device.Load("/");
  const auto root = formatV2Device.LoadDir("/");

  ASSERT_TRUE(rootNode.is_initialized());
  ASSERT_TRUE(root.is_initialized());
  const fspp::stat_info stat = (*rootNode)->stat();
  EXPECT_TRUE(stat.mode.hasDirFlag());
  EXPECT_EQ(0755u, stat.mode.value() & 0777u);
  EXPECT_EQ(1000u, stat.uid.value());
  EXPECT_EQ(1001u, stat.gid.value());
  EXPECT_EQ(10, stat.atime.tv_sec);
  EXPECT_EQ(11, stat.atime.tv_nsec);

  const std::vector<fspp::Dir::Entry> children = (*root)->children();
  EXPECT_EQ(
    (std::vector<std::string>{".", "..", "dir", "file", "link"}),
    childNames(children));
  EXPECT_EQ(fspp::Dir::EntryType::DIR, children[2].type);
  EXPECT_EQ(fspp::Dir::EntryType::FILE, children[3].type);
  EXPECT_EQ(fspp::Dir::EntryType::SYMLINK, children[4].type);
}

TEST_F(FormatV2DeviceTest, StatfsReportsHostSpace) {
  createInitialVolume();

  FormatV2Device formatV2Device = device();
  const auto stat = formatV2Device.statfs();

  EXPECT_EQ(255u, stat.max_filename_length);
  EXPECT_EQ(4096u, stat.blocksize);
  EXPECT_GT(stat.num_total_blocks, 0u);
  EXPECT_GT(stat.num_free_blocks, 0u);
  EXPECT_GE(stat.num_total_blocks, stat.num_free_blocks);
  EXPECT_GE(stat.num_free_blocks, stat.num_available_blocks);
  EXPECT_EQ(stat.num_total_blocks, stat.num_total_inodes);
  EXPECT_EQ(stat.num_free_blocks, stat.num_free_inodes);
  EXPECT_EQ(stat.num_available_blocks, stat.num_available_inodes);
}

TEST_F(FormatV2DeviceTest, LoadFileOpensReadOnlyAndReadsContents) {
  createInitialVolume();
  createRootFileWithData("/file", 3, 4, 5, "hello");

  FormatV2Device formatV2Device = device();
  const auto file = formatV2Device.LoadFile("/file");
  ASSERT_TRUE(file.is_initialized());

  const auto openFile = (*file)->open(fspp::openflags_t::RDONLY());
  char buffer[8] = {};
  const fspp::num_bytes_t bytesRead =
    openFile->read(buffer, fspp::num_bytes_t(8), fspp::num_bytes_t(1));

  EXPECT_EQ(4, bytesRead.value());
  EXPECT_EQ("ello", std::string(buffer, static_cast<size_t>(bytesRead.value())));
  const fspp::stat_info stat = openFile->stat();
  EXPECT_TRUE(stat.mode.hasFileFlag());
  EXPECT_EQ(0600u, stat.mode.value() & 0777u);
  EXPECT_EQ(5, stat.size.value());
}

TEST_F(FormatV2DeviceTest, LoadSymlinkReturnsTargetAndMetadata) {
  createInitialVolume();
  createRootSymlink("/link", 3, 4, "../target");

  FormatV2Device formatV2Device = device();
  const auto symlink = formatV2Device.LoadSymlink("/link");

  ASSERT_TRUE(symlink.is_initialized());
  EXPECT_EQ("../target", (*symlink)->target().string());

  const auto node = formatV2Device.Load("/link");
  ASSERT_TRUE(node.is_initialized());
  const fspp::stat_info stat = (*node)->stat();
  EXPECT_TRUE(stat.mode.hasSymlinkFlag());
  EXPECT_EQ(0777u, stat.mode.value() & 0777u);
  EXPECT_EQ(9, stat.size.value());
}

TEST_F(FormatV2DeviceTest, LoadReturnsNoneForMissingPathAndRejectsWrongTypedLoads) {
  createInitialVolume();
  createRootFileWithData("/file", 3, 4, 5, "hello");

  FormatV2Device formatV2Device = device();

  EXPECT_FALSE(formatV2Device.Load("/missing").is_initialized());
  EXPECT_FALSE(formatV2Device.LoadFile("/missing").is_initialized());
  EXPECT_THROW(formatV2Device.LoadDir("/file"), fspp::fuse::FuseErrnoException);
  EXPECT_THROW(formatV2Device.LoadFile("/"), fspp::fuse::FuseErrnoException);
  EXPECT_THROW(formatV2Device.LoadSymlink("/file"), fspp::fuse::FuseErrnoException);
}

TEST_F(FormatV2DeviceTest, DirectoryCreateOperationsPersistThroughAdapter) {
  createInitialVolume();

  SequenceRandomGenerator random;
  random.add(dataForId(rootId(3)));
  random.add(dataForId(objectId(4)));
  random.add(dataForId(rootId(5)));
  random.add(dataForId(objectId(6)));
  random.add(dataForId(rootId(7)));
  random.add(dataForId(objectId(8)));

  FormatV2Device formatV2Device = device(&random);
  auto root = formatV2Device.LoadDir("/");
  ASSERT_TRUE(root.is_initialized());

  (*root)->createDir("created-dir", fspp::mode_t(0750), fspp::uid_t(2000), fspp::gid_t(2001));
  auto createdFile = (*root)->createAndOpenFile(
    "created-file",
    fspp::mode_t(0640),
    fspp::uid_t(2002),
    fspp::gid_t(2003));
  (*root)->createSymlink("created-link", "../target", fspp::uid_t(2004), fspp::gid_t(2005));

  EXPECT_EQ(0, createdFile->stat().size.value());
  EXPECT_EQ(
    (std::vector<std::string>{".", "..", "created-dir", "created-file", "created-link"}),
    childNames((*root)->children()));

  auto directory = formatV2Device.Load("/created-dir");
  auto file = formatV2Device.Load("/created-file");
  auto symlinkNode = formatV2Device.Load("/created-link");
  auto symlink = formatV2Device.LoadSymlink("/created-link");
  ASSERT_TRUE(directory.is_initialized());
  ASSERT_TRUE(file.is_initialized());
  ASSERT_TRUE(symlinkNode.is_initialized());
  ASSERT_TRUE(symlink.is_initialized());

  EXPECT_TRUE((*directory)->stat().mode.hasDirFlag());
  EXPECT_EQ(0750u, (*directory)->stat().mode.value() & 0777u);
  EXPECT_EQ(2000u, (*directory)->stat().uid.value());
  EXPECT_TRUE((*file)->stat().mode.hasFileFlag());
  EXPECT_EQ(0640u, (*file)->stat().mode.value() & 0777u);
  EXPECT_EQ(2002u, (*file)->stat().uid.value());
  EXPECT_TRUE((*symlinkNode)->stat().mode.hasSymlinkFlag());
  EXPECT_EQ("../target", (*symlink)->target().string());
  EXPECT_EQ(2004u, (*symlinkNode)->stat().uid.value());
}

TEST_F(FormatV2DeviceTest, ConcurrentDirectoryCreatesAreSerializedThroughAdapter) {
  createInitialVolume();

  constexpr size_t THREAD_COUNT = 8;
  SequenceRandomGenerator random;
  for (size_t index = 0; index < THREAD_COUNT; ++index) {
    random.add(dataForId(rootId(3 + static_cast<unsigned int>(2 * index))));
    random.add(dataForId(objectId(4 + static_cast<unsigned int>(2 * index))));
  }

  FormatV2Device formatV2Device = device(&random);
  auto root = formatV2Device.LoadDir("/");
  ASSERT_TRUE(root.is_initialized());

  std::vector<std::thread> threads;
  std::vector<std::exception_ptr> failures(THREAD_COUNT);
  for (size_t index = 0; index < THREAD_COUNT; ++index) {
    threads.emplace_back([&, index] {
      try {
        (*root)->createDir(
          "dir-" + std::to_string(index),
          fspp::mode_t(0755),
          fspp::uid_t(1000 + static_cast<unsigned int>(index)),
          fspp::gid_t(2000 + static_cast<unsigned int>(index)));
      } catch (...) {
        failures[index] = std::current_exception();
      }
    });
  }

  for (std::thread &thread: threads) {
    thread.join();
  }

  for (const std::exception_ptr &failure: failures) {
    if (failure == nullptr) {
      continue;
    }
    try {
      std::rethrow_exception(failure);
    } catch (const std::exception &e) {
      FAIL() << "Concurrent create failed: " << e.what();
    } catch (...) {
      FAIL() << "Concurrent create failed with an unknown exception";
    }
  }

  std::vector<std::string> actualChildren = childNames((*root)->children());
  std::sort(actualChildren.begin(), actualChildren.end());

  std::vector<std::string> expectedChildren{".", ".."};
  for (size_t index = 0; index < THREAD_COUNT; ++index) {
    expectedChildren.push_back("dir-" + std::to_string(index));
  }
  std::sort(expectedChildren.begin(), expectedChildren.end());

  EXPECT_EQ(expectedChildren, actualChildren);
  for (size_t index = 0; index < THREAD_COUNT; ++index) {
    EXPECT_TRUE(formatV2Device.LoadDir("/dir-" + std::to_string(index)).is_initialized());
  }
}

TEST_F(FormatV2DeviceTest, OpenFileWriteAndTruncatePersistThroughAdapter) {
  createInitialVolume();

  SequenceRandomGenerator random;
  random.add(dataForId(rootId(3)));
  random.add(dataForId(objectId(4)));
  random.add(dataForId(objectId(5)));
  random.add(dataForId(rootId(6)));
  random.add(dataForId(objectId(7)));
  random.add(dataForId(rootId(8)));
  random.add(dataForId(rootId(9)));

  FormatV2Device formatV2Device = device(&random);
  auto root = formatV2Device.LoadDir("/");
  ASSERT_TRUE(root.is_initialized());
  auto openFile = (*root)->createAndOpenFile(
    "file",
    fspp::mode_t(0600),
    fspp::uid_t(1000),
    fspp::gid_t(1001));

  const std::string contents = "hello";
  openFile->write(
    contents.data(),
    fspp::num_bytes_t(static_cast<int64_t>(contents.size())),
    fspp::num_bytes_t(0));

  char buffer[8] = {};
  EXPECT_EQ(5, openFile->read(buffer, fspp::num_bytes_t(8), fspp::num_bytes_t(0)).value());
  EXPECT_EQ("hello", std::string(buffer, 5));
  EXPECT_EQ("hello", payloadString(*loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/file")));

  openFile->truncate(fspp::num_bytes_t(2));
  char truncated[8] = {};
  EXPECT_EQ(2, openFile->read(truncated, fspp::num_bytes_t(8), fspp::num_bytes_t(0)).value());
  EXPECT_EQ("he", std::string(truncated, 2));
  EXPECT_EQ(2, openFile->stat().size.value());

  auto file = formatV2Device.LoadFile("/file");
  ASSERT_TRUE(file.is_initialized());
  (*file)->truncate(fspp::num_bytes_t(5));
  cpputils::Data extended = *loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/file");
  ASSERT_EQ(5u, extended.size());
  EXPECT_EQ('h', static_cast<const char*>(extended.data())[0]);
  EXPECT_EQ('e', static_cast<const char*>(extended.data())[1]);
  EXPECT_EQ('\0', static_cast<const char*>(extended.data())[2]);
  EXPECT_EQ('\0', static_cast<const char*>(extended.data())[3]);
  EXPECT_EQ('\0', static_cast<const char*>(extended.data())[4]);
}

TEST_F(FormatV2DeviceTest, NestedDirectoryMutationsPersistThroughAdapter) {
  createInitialVolume();

  SequenceRandomGenerator random;
  random.add(dataForId(rootId(3)));
  random.add(dataForId(objectId(4)));
  random.add(dataForId(rootId(5)));
  random.add(dataForId(objectId(6)));
  random.add(dataForId(objectId(7)));
  random.add(dataForId(rootId(8)));

  FormatV2Device formatV2Device = device(&random);
  auto root = formatV2Device.LoadDir("/");
  ASSERT_TRUE(root.is_initialized());
  (*root)->createDir("parent", fspp::mode_t(0755), fspp::uid_t(1000), fspp::gid_t(1001));

  auto parent = formatV2Device.LoadDir("/parent");
  ASSERT_TRUE(parent.is_initialized());
  auto child = (*parent)->createAndOpenFile(
    "child",
    fspp::mode_t(0644),
    fspp::uid_t(1002),
    fspp::gid_t(1003));

  const std::string contents = "nested";
  child->write(
    contents.data(),
    fspp::num_bytes_t(static_cast<int64_t>(contents.size())),
    fspp::num_bytes_t(0));

  EXPECT_EQ(
    (std::vector<std::string>{".", "..", "child"}),
    childNames((*parent)->children()));
  EXPECT_EQ("nested", payloadString(*loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/parent/child")));
}

TEST_F(FormatV2DeviceTest, OpenFileHonorsReadOnlyMode) {
  createInitialVolume();
  createRootFileWithData("/file", 3, 4, 5, "hello");

  FormatV2Device formatV2Device = device();
  auto file = formatV2Device.LoadFile("/file");
  ASSERT_TRUE(file.is_initialized());
  auto openFile = (*file)->open(fspp::openflags_t::RDONLY());

  try {
    openFile->write("x", fspp::num_bytes_t(1), fspp::num_bytes_t(0));
    FAIL() << "Expected read-only open file write to fail";
  } catch (const fspp::fuse::FuseErrnoException &e) {
    EXPECT_EQ(EBADF, e.getErrno());
  }
}

TEST_F(FormatV2DeviceTest, SyncOperationsSucceedAfterDurablePublication) {
  createInitialVolume();

  SequenceRandomGenerator random;
  random.add(dataForId(rootId(3)));
  random.add(dataForId(objectId(4)));
  random.add(dataForId(objectId(5)));
  random.add(dataForId(rootId(6)));
  random.add(dataForId(objectId(7)));

  FormatV2Device formatV2Device = device(&random);
  auto root = formatV2Device.LoadDir("/");
  ASSERT_TRUE(root.is_initialized());

  auto openFile = (*root)->createAndOpenFile(
    "file",
    fspp::mode_t(0600),
    fspp::uid_t(1000),
    fspp::gid_t(1001));
  const std::string contents = "durable";
  openFile->write(
    contents.data(),
    fspp::num_bytes_t(static_cast<int64_t>(contents.size())),
    fspp::num_bytes_t(0));

  EXPECT_NO_THROW(openFile->flush());
  EXPECT_NO_THROW(openFile->fsync());
  EXPECT_NO_THROW(openFile->fdatasync());
  EXPECT_NO_THROW((*root)->fsync());
  EXPECT_NO_THROW(formatV2Device.sync());

  FormatV2Device reopenedDevice = device();
  auto reopenedFile = reopenedDevice.LoadFile("/file");
  ASSERT_TRUE(reopenedFile.is_initialized());
  auto reopenedOpenFile = (*reopenedFile)->open(fspp::openflags_t::RDONLY());

  char buffer[16] = {};
  EXPECT_EQ(
    static_cast<int64_t>(contents.size()),
    reopenedOpenFile->read(buffer, fspp::num_bytes_t(sizeof(buffer)), fspp::num_bytes_t(0)).value());
  EXPECT_EQ(contents, std::string(buffer, contents.size()));
}

TEST_F(FormatV2DeviceTest, SyncDoesNotMaskFailedDurablePublication) {
  createInitialVolume();
  storeConflictingRootContent(2, rootId(3));

  SequenceRandomGenerator random;
  random.add(dataForId(rootId(3)));
  random.add(dataForId(objectId(4)));
  random.add(dataForId(objectId(5)));

  FormatV2Device formatV2Device = device(&random);
  auto root = formatV2Device.LoadDir("/");
  ASSERT_TRUE(root.is_initialized());

  EXPECT_THROW(
    (*root)->createAndOpenFile(
      "file",
      fspp::mode_t(0600),
      fspp::uid_t(1000),
      fspp::gid_t(1001)),
    std::runtime_error);
  EXPECT_NO_THROW(formatV2Device.sync());
  EXPECT_FALSE(formatV2Device.LoadFile("/file").is_initialized());

  const AcceptedRootStateStore acceptedRootStateStore(layout().acceptedRootFile, filesystemId());
  const auto acceptedRoot = acceptedRootStateStore.load();
  ASSERT_TRUE(acceptedRoot.is_initialized());
  EXPECT_EQ((AcceptedRoot{1, rootId(1)}), *acceptedRoot);
}

TEST_F(FormatV2DeviceTest, OpenFileFailsClosedWhenPathIsReusedForAnotherFile) {
  createInitialVolume();
  createRootFileWithData("/file", 3, 4, 5, "hello");

  SequenceRandomGenerator random;
  random.add(dataForId(rootId(6)));
  random.add(dataForId(rootId(7)));
  random.add(dataForId(objectId(8)));

  FormatV2Device formatV2Device = device(&random);
  auto file = formatV2Device.LoadFile("/file");
  auto node = formatV2Device.Load("/file");
  auto root = formatV2Device.LoadDir("/");
  ASSERT_TRUE(file.is_initialized());
  ASSERT_TRUE(node.is_initialized());
  ASSERT_TRUE(root.is_initialized());

  auto openFile = (*file)->open(fspp::openflags_t::RDWR());
  (*node)->rename("/renamed");
  auto replacement = (*root)->createAndOpenFile(
    "file",
    fspp::mode_t(0600),
    fspp::uid_t(1000),
    fspp::gid_t(1001));

  char buffer[8] = {};
  const int staleErrno = staleFileHandleErrno();
  expectFuseErrno(
    [&] { openFile->read(buffer, fspp::num_bytes_t(1), fspp::num_bytes_t(0)); },
    staleErrno);
  expectFuseErrno(
    [&] { openFile->write("x", fspp::num_bytes_t(1), fspp::num_bytes_t(0)); },
    staleErrno);
  expectFuseErrno([&] { openFile->stat(); }, staleErrno);

  EXPECT_EQ("hello", payloadString(*loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/renamed")));
  EXPECT_EQ("", payloadString(*loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/file")));
  EXPECT_EQ(0, replacement->stat().size.value());
}

TEST_F(FormatV2DeviceTest, NodeMetadataRenameAndRemovePersistThroughAdapter) {
  createInitialVolume();
  createRootFileWithData("/file", 3, 4, 5, "hello");

  SequenceRandomGenerator random;
  random.add(dataForId(rootId(6)));
  random.add(dataForId(rootId(7)));
  random.add(dataForId(rootId(8)));
  random.add(dataForId(rootId(9)));
  random.add(dataForId(rootId(10)));

  FormatV2Device formatV2Device = device(&random);
  auto node = formatV2Device.Load("/file");
  ASSERT_TRUE(node.is_initialized());

  (*node)->chmod(fspp::mode_t(0640));
  (*node)->chown(fspp::uid_t(3000), fspp::gid_t(3001));
  (*node)->utimens(timespec{123, 456}, timespec{789, 987});

  const fspp::stat_info updated = (*node)->stat();
  EXPECT_TRUE(updated.mode.hasFileFlag());
  EXPECT_EQ(0640u, updated.mode.value() & 0777u);
  EXPECT_EQ(3000u, updated.uid.value());
  EXPECT_EQ(3001u, updated.gid.value());
  EXPECT_EQ(123, updated.atime.tv_sec);
  EXPECT_EQ(456, updated.atime.tv_nsec);
  EXPECT_EQ(789, updated.mtime.tv_sec);
  EXPECT_EQ(987, updated.mtime.tv_nsec);

  (*node)->rename("/renamed");
  EXPECT_FALSE(formatV2Device.Load("/file").is_initialized());
  EXPECT_TRUE(formatV2Device.Load("/renamed").is_initialized());

  (*node)->remove();
  EXPECT_FALSE(formatV2Device.Load("/renamed").is_initialized());
}

TEST_F(FormatV2DeviceTest, RenameReplacesExistingFileThroughAdapter) {
  createInitialVolume();
  createRootFileWithData("/source", 3, 4, 5, "source");
  createRootFileWithData("/target", 6, 7, 8, "target");

  SequenceRandomGenerator random;
  random.add(dataForId(rootId(9)));

  FormatV2Device formatV2Device = device(&random);
  auto source = formatV2Device.Load("/source");
  ASSERT_TRUE(source.is_initialized());

  (*source)->rename("/target");

  EXPECT_FALSE(formatV2Device.Load("/source").is_initialized());
  EXPECT_EQ("source", payloadString(*loadVolumeFileContentsAtPath(
    layout(),
    filesystemId(),
    masterKey(),
    "/target")));
}
