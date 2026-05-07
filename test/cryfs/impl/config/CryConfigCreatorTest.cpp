#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cryfs/impl/config/CryConfigCreator.h>
#include <cryfs/impl/config/CryCipher.h>
#include <cpp-utils/crypto/symmetric/ciphers.h>
#include "../../impl/testutils/MockConsole.h"
#include "../../impl/testutils/TestWithFakeHomeDirectory.h"
#include <cpp-utils/io/NoninteractiveConsole.h>
#include <gitversion/gitversion.h>
#include <cryfs/impl/localstate/LocalStateDir.h>

using namespace cryfs;

using boost::none;
using cpputils::NoninteractiveConsole;
using std::string;
using std::shared_ptr;
using std::make_shared;
using ::testing::Return;
using ::testing::HasSubstr;
using ::testing::UnorderedElementsAreArray;
using ::testing::NiceMock;

#define EXPECT_ASK_TO_USE_DEFAULT_SETTINGS()                                                                           \
  EXPECT_CALL(*console, askYesNo("Use default settings?", true)).Times(1)
#define EXPECT_DOES_NOT_ASK_TO_USE_DEFAULT_SETTINGS()                                                                  \
  EXPECT_CALL(*console, askYesNo("Use default settings?", true)).Times(0)
#define EXPECT_ASK_FOR_CIPHER()                                                                                        \
  EXPECT_CALL(*console, ask(HasSubstr("block cipher"), UnorderedElementsAreArray(CryCiphers::supportedCipherNames()))).Times(1)
#define EXPECT_DOES_NOT_ASK_FOR_CIPHER()                                                                               \
  EXPECT_CALL(*console, ask(HasSubstr("block cipher"), testing::_)).Times(0)
#define EXPECT_ASK_FOR_BLOCKSIZE()                                                                                     \
  EXPECT_CALL(*console, ask(HasSubstr("block size"), testing::_)).Times(1)
#define EXPECT_DOES_NOT_ASK_FOR_BLOCKSIZE()                                                                            \
  EXPECT_CALL(*console, ask(HasSubstr("block size"), testing::_)).Times(0)

class CryConfigCreatorTest: public ::testing::Test, TestWithFakeHomeDirectory {
public:
    CryConfigCreatorTest()
            : console(make_shared<NiceMock<MockConsole>>()),
              tempLocalStateDir(), localStateDir(tempLocalStateDir.path()),
              creator(console, cpputils::Random::Csprng(), localStateDir),
              noninteractiveCreator(make_shared<NoninteractiveConsole>(console), cpputils::Random::Csprng(), localStateDir) {
        EXPECT_CALL(*console, ask(HasSubstr("block cipher"), testing::_)).WillRepeatedly(ChooseAnyCipher());
        EXPECT_CALL(*console, ask(HasSubstr("block size"), testing::_)).WillRepeatedly(Return(0));
    }
    shared_ptr<NiceMock<MockConsole>> console;
    cpputils::TempDir tempLocalStateDir;
    LocalStateDir localStateDir;
    CryConfigCreator creator;
    CryConfigCreator noninteractiveCreator;

    void AnswerNoToDefaultSettings() {
        EXPECT_ASK_TO_USE_DEFAULT_SETTINGS().WillOnce(Return(false));
    }

    void AnswerYesToDefaultSettings() {
        EXPECT_ASK_TO_USE_DEFAULT_SETTINGS().WillOnce(Return(true));
    }
};

TEST_F(CryConfigCreatorTest, DoesAskForCipherIfNotSpecified) {
    AnswerNoToDefaultSettings();
    EXPECT_ASK_FOR_CIPHER().WillOnce(ChooseAnyCipher());
    const CryConfig config = creator.create(none, none, false).config;
}

TEST_F(CryConfigCreatorTest, DoesNotAskForCipherIfSpecified) {
    AnswerNoToDefaultSettings();
    EXPECT_DOES_NOT_ASK_FOR_CIPHER();
    const CryConfig config = creator.create(string("aes-256-gcm"), none, false).config;
}

TEST_F(CryConfigCreatorTest, DoesNotAskForCipherIfUsingDefaultSettings) {
    AnswerYesToDefaultSettings();
    EXPECT_DOES_NOT_ASK_FOR_CIPHER();
    const CryConfig config = creator.create(none, none, false).config;
}

TEST_F(CryConfigCreatorTest, DoesNotAskForCipherIfNoninteractive) {
    EXPECT_DOES_NOT_ASK_TO_USE_DEFAULT_SETTINGS();
    EXPECT_DOES_NOT_ASK_FOR_CIPHER();
    const CryConfig config = noninteractiveCreator.create(none, none, false).config;
}

TEST_F(CryConfigCreatorTest, DoesAskForBlocksizeIfNotSpecified) {
    AnswerNoToDefaultSettings();
    EXPECT_ASK_FOR_BLOCKSIZE().WillOnce(Return(1));
    const CryConfig config = creator.create(none, none, false).config;
}

TEST_F(CryConfigCreatorTest, DoesNotAskForBlocksizeIfSpecified) {
    AnswerNoToDefaultSettings();
    EXPECT_DOES_NOT_ASK_FOR_BLOCKSIZE();
    const CryConfig config = creator.create(none, 10*1024u, false).config;
}

TEST_F(CryConfigCreatorTest, DoesNotAskForBlocksizeIfNoninteractive) {
    EXPECT_DOES_NOT_ASK_TO_USE_DEFAULT_SETTINGS();
    EXPECT_DOES_NOT_ASK_FOR_BLOCKSIZE();
    const CryConfig config = noninteractiveCreator.create(none, none, false).config;
}

TEST_F(CryConfigCreatorTest, DoesNotAskForBlocksizeIfUsingDefaultSettings) {
    AnswerYesToDefaultSettings();
    EXPECT_DOES_NOT_ASK_FOR_BLOCKSIZE();
    const CryConfig config = creator.create(none, none, false).config;
}

TEST_F(CryConfigCreatorTest, CreatesNewConfigWithoutExclusiveClientPolicy) {
    const CryConfig config = noninteractiveCreator.create(none, none, false).config;
    EXPECT_EQ(string::npos, config.save().ToString().find("exclusiveClientId"));
}

TEST_F(CryConfigCreatorTest, CreatesNewConfigWithoutLegacyMigrationMarkers) {
    const CryConfig config = noninteractiveCreator.create(none, none, false).config;
    EXPECT_EQ(string::npos, config.save().ToString().find("migrations"));
}

TEST_F(CryConfigCreatorTest, ChoosesEmptyRootBlobId) {
    AnswerNoToDefaultSettings();
    const CryConfig config = creator.create(none, none, false).config;
    EXPECT_EQ("", config.RootBlob()); // This tells CryFS to create a new root blob
}

TEST_F(CryConfigCreatorTest, ChoosesValidEncryptionKey_448) {
    AnswerNoToDefaultSettings();
    EXPECT_ASK_FOR_CIPHER().WillOnce(ChooseCipher("mars-448-gcm"));
    const CryConfig config = creator.create(none, none, false).config;
    // Verify key has the correct size for Mars-448-GCM
    EXPECT_EQ(cpputils::Mars448_GCM::KEYSIZE, config.EncryptionKey().binaryLength());
}

TEST_F(CryConfigCreatorTest, ChoosesValidEncryptionKey_256) {
    AnswerNoToDefaultSettings();
    EXPECT_ASK_FOR_CIPHER().WillOnce(ChooseCipher("aes-256-gcm"));
    const CryConfig config = creator.create(none, none, false).config;
    // Verify key has the correct size for AES-256-GCM
    EXPECT_EQ(cpputils::AES256_GCM::KEYSIZE, config.EncryptionKey().binaryLength());
}

TEST_F(CryConfigCreatorTest, ChoosesValidEncryptionKey_128) {
    AnswerNoToDefaultSettings();
    EXPECT_ASK_FOR_CIPHER().WillOnce(ChooseCipher("aes-128-gcm"));
    const CryConfig config = creator.create(none, none, false).config;
    // Verify key has the correct size for AES-128-GCM
    EXPECT_EQ(cpputils::AES128_GCM::KEYSIZE, config.EncryptionKey().binaryLength());
}

TEST_F(CryConfigCreatorTest, DoesNotAskForAnythingIfEverythingIsSpecified) {
    EXPECT_DOES_NOT_ASK_TO_USE_DEFAULT_SETTINGS();
    EXPECT_DOES_NOT_ASK_FOR_CIPHER();
    const CryConfig config = noninteractiveCreator.create(string("aes-256-gcm"), 10*1024u, false).config;
}

TEST_F(CryConfigCreatorTest, SetsCorrectCreatedWithVersion) {
    const CryConfig config = noninteractiveCreator.create(none, none, false).config;
    EXPECT_EQ(gitversion::VersionString(), config.CreatedWithVersion());
}

TEST_F(CryConfigCreatorTest, SetsCorrectLastOpenedWithVersion) {
    const CryConfig config = noninteractiveCreator.create(none, none, false).config;
    EXPECT_EQ(gitversion::VersionString(), config.CreatedWithVersion());
}

TEST_F(CryConfigCreatorTest, SetsCorrectVersion) {
    const CryConfig config = noninteractiveCreator.create(none, none, false).config;
    EXPECT_EQ(CryConfig::FilesystemFormatVersion, config.Version());
}

//TODO Add test cases ensuring that the values entered are correctly taken
