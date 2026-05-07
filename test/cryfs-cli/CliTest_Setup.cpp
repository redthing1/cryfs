#include "testutils/CliTest.h"

#include <cryfs-cli/Environment.h>
#include <cryfs/impl/config/CryConfigFile.h>
#include <cryfs/impl/config/CryKeyProvider.h>
#include <cryfs/impl/formatv2/VolumePathOperations.h>
#include <cryfs/impl/formatv2/Volume.h>
#include <cryfs/impl/localstate/LocalStateDir.h>
#include <cpp-utils/crypto/kdf/Scrypt.h>

#include <fstream>

using cpputils::TempFile;
using cryfs::CryConfigFile;
using cryfs::CryKeyProvider;
using cryfs::ErrorCode;
using cryfs::LocalStateDir;
using cpputils::Data;
using cpputils::EncryptionKey;
using cpputils::SCrypt;

namespace bf = boost::filesystem;
namespace formatv2 = cryfs::formatv2;

namespace {

class FakeCryKeyProvider final : public CryKeyProvider {
  EncryptionKey requestKeyForExistingFilesystem(size_t keySize, const Data &kdfParameters) override {
    return SCrypt(SCrypt::TestSettings).deriveExistingKey(keySize, "pass", kdfParameters);
  }

  KeyResult requestKeyForNewFilesystem(size_t keySize) override {
    auto derived = SCrypt(SCrypt::TestSettings).deriveNewKey(keySize, "pass");
    return {
        std::move(derived.key),
        std::move(derived.kdfParameters)
    };
  }
};

}

//Tests that cryfs is correctly setup according to the CLI parameters specified
using CliTest_Setup = CliTest;

TEST_F(CliTest_Setup, NoSpecialOptions) {
    //Specify --cipher parameter to make it non-interactive
    //TODO Remove "-f" parameter, once EXPECT_RUN_SUCCESS can handle that
    EXPECT_RUN_SUCCESS({basedir.string().c_str(), mountdir.string().c_str(), "--cipher", "aes-256-gcm", "-f"}, mountdir);
}

TEST_F(CliTest_Setup, NewFilesystemCreatesFormatV2InitialRoot) {
    EXPECT_RUN_SUCCESS({basedir.string().c_str(), mountdir.string().c_str(), "--cipher", "aes-256-gcm", "-f"}, mountdir);

    FakeCryKeyProvider keyProvider;
    auto configFile = CryConfigFile::load(basedir / "cryfs.config", &keyProvider, CryConfigFile::Access::ReadOnly).right();
    const LocalStateDir localStateDir(cryfs_cli::Environment::localStateDir());
    const auto opened = formatv2::openVolumeRoot(
        formatv2::volumeLayout(basedir, localStateDir.forFilesystemId(configFile->config()->FilesystemId())),
        configFile->config()->FilesystemId(),
        configFile->config()->EncryptionKey());

    ASSERT_EQ(formatv2::RootOpenStatus::Selected, opened.status);
    ASSERT_TRUE(opened.rootDirectory.is_initialized());
    EXPECT_TRUE(opened.rootDirectory->entries.empty());
}

TEST_F(CliTest_Setup, ExistingFilesystemOpensFormatV2RootOnMount) {
    const std::vector<std::string> args{basedir.string().c_str(), mountdir.string().c_str(), "--cipher", "aes-256-gcm", "-f"};

    EXPECT_RUN_SUCCESS(args, mountdir);
    EXPECT_RUN_SUCCESS(args, mountdir);
}

TEST_F(CliTest_Setup, MountedWritesPersistInFormatV2Volume) {
    const std::vector<std::string> args{basedir.string().c_str(), mountdir.string().c_str(), "--cipher", "aes-256-gcm", "-f"};

    EXPECT_RUN_SUCCESS(args, mountdir, [&] {
        std::ofstream file((mountdir / "format-v2-file").c_str(), std::ios::binary | std::ios::trunc);
        file << "format-v2 data";
        ASSERT_TRUE(file.good());
    });

    FakeCryKeyProvider keyProvider;
    auto configFile = CryConfigFile::load(basedir / "cryfs.config", &keyProvider, CryConfigFile::Access::ReadOnly).right();
    const LocalStateDir localStateDir(cryfs_cli::Environment::localStateDir());
    const auto layout = formatv2::volumeLayout(basedir, localStateDir.forFilesystemId(configFile->config()->FilesystemId()));
    auto contents = formatv2::loadVolumeFileContentsAtPath(
        layout,
        configFile->config()->FilesystemId(),
        configFile->config()->EncryptionKey(),
        "/format-v2-file");

    ASSERT_TRUE(contents.is_initialized());
    const std::string storedContents(
        static_cast<const char*>(contents->data()),
        contents->size());
    EXPECT_EQ("format-v2 data", storedContents);
}

TEST_F(CliTest_Setup, ExistingFilesystemWithoutFormatV2RootFails) {
    const std::vector<std::string> args{basedir.string().c_str(), mountdir.string().c_str(), "--cipher", "aes-256-gcm", "-f"};

    EXPECT_RUN_SUCCESS(args, mountdir);
    bf::remove_all(basedir / "format-v2" / "roots");

    EXPECT_RUN_ERROR(
        args,
        "Error 19: Failed to open format-v2 root: no authenticated roots",
        ErrorCode::InvalidFilesystem);
}

TEST_F(CliTest_Setup, UnmountIdleIsRejectedForFormatV2MountPath) {
    EXPECT_RUN_ERROR(
        {basedir.string().c_str(), mountdir.string().c_str(), "--cipher", "aes-256-gcm", "-f", "--unmount-idle", "1"},
        "Error 10: --unmount-idle is not supported by the format-v2 mount path yet.",
        ErrorCode::InvalidArguments);
}

TEST_F(CliTest_Setup, NotexistingLogfileGiven) {
    const TempFile notexisting_logfile(false);
    //Specify --cipher parameter to make it non-interactive
    //TODO Remove "-f" parameter, once EXPECT_RUN_SUCCESS can handle that
    EXPECT_RUN_SUCCESS({basedir.string().c_str(), mountdir.string().c_str(), "-f", "--cipher", "aes-256-gcm", "--logfile", notexisting_logfile.path().string().c_str()}, mountdir);
    //TODO Expect logfile is used (check logfile content)
}

TEST_F(CliTest_Setup, ExistingLogfileGiven) {
    //Specify --cipher parameter to make it non-interactive
    //TODO Remove "-f" parameter, once EXPECT_RUN_SUCCESS can handle that
    EXPECT_RUN_SUCCESS({basedir.string().c_str(), mountdir.string().c_str(), "-f", "--cipher", "aes-256-gcm", "--logfile", logfile.path().string().c_str()}, mountdir);
    //TODO Expect logfile is used (check logfile content)
}

TEST_F(CliTest_Setup, ConfigfileGiven) {
    //Specify --cipher parameter to make it non-interactive
    //TODO Remove "-f" parameter, once EXPECT_RUN_SUCCESS can handle that
    EXPECT_RUN_SUCCESS({basedir.string().c_str(), mountdir.string().c_str(), "-f", "--cipher", "aes-256-gcm", "--config", configfile.path().string().c_str()}, mountdir);
}

TEST_F(CliTest_Setup, AutocreateBasedir) {
    const TempFile notexisting_basedir(false);
    //Specify --cipher parameter to make it non-interactive
    //TODO Remove "-f" parameter, once EXPECT_RUN_SUCCESS can handle that
    EXPECT_RUN_SUCCESS({notexisting_basedir.path().string().c_str(), mountdir.string().c_str(), "-f", "--cipher", "aes-256-gcm", "--create-missing-basedir"}, mountdir);
}

TEST_F(CliTest_Setup, AutocreateBasedirFail) {
    const TempFile notexisting_basedir(false);
    //Specify --cipher parameter to make it non-interactive
    //TODO Remove "-f" parameter, once EXPECT_RUN_SUCCESS can handle that
    EXPECT_RUN_ERROR(
            {notexisting_basedir.path().string().c_str(), mountdir.string().c_str(), "-f", "--cipher", "aes-256-gcm"},
            "Error 16: base directory not found.",
            ErrorCode::InaccessibleBaseDir
    );
}

TEST_F(CliTest_Setup, AutocreateMountpoint) {
    const TempFile notexisting_mountpoint(false);
    //Specify --cipher parameter to make it non-interactive
    //TODO Remove "-f" parameter, once EXPECT_RUN_SUCCESS can handle that
    EXPECT_RUN_SUCCESS({basedir.string().c_str(), notexisting_mountpoint.path().string().c_str(), "-f", "--cipher", "aes-256-gcm", "--create-missing-mountpoint"}, notexisting_mountpoint.path());
}

TEST_F(CliTest_Setup, AutocreateMountdirFail) {
    const TempFile notexisting_mountdir(false);
    //Specify --cipher parameter to make it non-interactive
    //TODO Remove "-f" parameter, once EXPECT_RUN_SUCCESS can handle that
    EXPECT_RUN_ERROR(
            {basedir.string().c_str(), notexisting_mountdir.path().string().c_str(), "-f", "--cipher", "aes-256-gcm"},
            "Error 17: mount directory not found.",
            ErrorCode::InaccessibleMountDir
    );
}

TEST_F(CliTest_Setup, FuseOptionGiven) {
    //Specify --cipher parameter to make it non-interactive
    //TODO Remove "-f" parameter, once EXPECT_RUN_SUCCESS can handle that
    EXPECT_RUN_SUCCESS({basedir.string().c_str(), mountdir.string().c_str(), "-f", "--cipher", "aes-256-gcm", "--", "-f"}, mountdir);
}

TEST_F(CliTest, WorksWithCommasInBasedir) {
    // This test makes sure we don't regress on https://github.com/cryfs/cryfs/issues/326
    //TODO Remove "-f" parameter, once EXPECT_RUN_SUCCESS can handle that
    auto basedir_ = basedir / "pathname,with,commas";
    bf::create_directory(basedir_);
    EXPECT_RUN_SUCCESS({basedir_.string().c_str(), mountdir.string().c_str(), "-f"}, mountdir);
}
