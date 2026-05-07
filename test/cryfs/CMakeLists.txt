project (cryfs-test)

set(SOURCES
        impl/config/crypto/CryConfigEncryptorFactoryTest.cpp
        impl/config/crypto/outer/OuterConfigTest.cpp
        impl/config/crypto/outer/OuterEncryptorTest.cpp
        impl/config/crypto/inner/ConcreteInnerEncryptorTest.cpp
        impl/config/crypto/inner/InnerConfigTest.cpp
        impl/config/crypto/CryConfigEncryptorTest.cpp
        impl/config/CryConfigCreatorTest.cpp
        impl/config/CryConfigFileTest.cpp
        impl/config/CryConfigTest.cpp
        impl/config/CryCipherTest.cpp
        impl/config/CryConfigLoaderTest.cpp
        impl/config/CryConfigConsoleTest.cpp
        impl/config/CryPasswordBasedKeyProviderTest.cpp
        impl/config/CryPresetPasswordBasedKeyProviderTest.cpp
        impl/filesystem/testutils/CryTestBase.cpp
        impl/filesystem/CryFsTest.cpp
        impl/filesystem/fsblobstore/DirBlobPersistenceTest.cpp
        impl/filesystem/CryOpenFileSyncTest.cpp
        impl/filesystem/CryNodeTest_Rename.cpp
        impl/filesystem/CryNodeTest_RenameNested.cpp
        impl/filesystem/FileSystemTest.cpp
        impl/formatv2/AcceptedRootStateTest.cpp
        impl/formatv2/DirectoryRecordTest.cpp
        impl/formatv2/DirectoryRecordStoreTest.cpp
        impl/formatv2/FormatV2DeviceTest.cpp
        impl/formatv2/FileRecordTest.cpp
        impl/formatv2/FileRecordStoreTest.cpp
        impl/formatv2/KeyDerivationTest.cpp
        impl/formatv2/ObjectRecordTest.cpp
        impl/formatv2/ObjectTreeValidatorTest.cpp
        impl/formatv2/RootAuthenticationTest.cpp
        impl/formatv2/RootContentTest.cpp
        impl/formatv2/RootContentStoreTest.cpp
        impl/formatv2/RootOpenTest.cpp
        impl/formatv2/RootOpenWithContentTest.cpp
        impl/formatv2/RootOpenWithDirectoryTest.cpp
        impl/formatv2/RootPublisherTest.cpp
        impl/formatv2/RootPublicationStoreTest.cpp
        impl/formatv2/RootRecordTest.cpp
        impl/formatv2/RootSelectionTest.cpp
        impl/formatv2/RootTransactionTest.cpp
        impl/formatv2/SymlinkRecordTest.cpp
        impl/formatv2/SymlinkRecordStoreTest.cpp
        impl/formatv2/VolumeTest.cpp
        impl/formatv2/VolumePathOperationsTest.cpp
        impl/localstate/LocalStateDirTest.cpp
        impl/localstate/LocalStateMetadataTest.cpp
        impl/localstate/BasedirMetadataTest.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME} my-gtest-main cryfs)
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

target_enable_style_warnings(${PROJECT_NAME})
target_activate_cpp17(${PROJECT_NAME})
