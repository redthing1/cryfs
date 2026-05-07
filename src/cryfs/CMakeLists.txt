project (cryfs)

set(LIB_SOURCES
#    cryfs.cpp
        impl/CryfsException.cpp
        impl/config/crypto/outer/OuterConfig.cpp
        impl/config/crypto/outer/OuterEncryptor.cpp
        impl/config/crypto/CryConfigEncryptorFactory.cpp
        impl/config/crypto/inner/ConcreteInnerEncryptor.cpp
        impl/config/crypto/inner/InnerConfig.cpp
        impl/config/crypto/inner/InnerEncryptor.cpp
        impl/config/crypto/CryConfigEncryptor.cpp
        impl/config/CryConfigConsole.cpp
        impl/config/CryConfigLoader.cpp
        impl/config/CryConfig.cpp
        impl/config/CryConfigFile.cpp
        impl/config/CryCipher.cpp
        impl/config/CryConfigCreator.cpp
        impl/config/CryKeyProvider.cpp
        impl/config/CryPasswordBasedKeyProvider.cpp
        impl/config/CryPresetPasswordBasedKeyProvider.cpp
        impl/filesystem/CryOpenFile.cpp
        impl/filesystem/fsblobstore/utils/DirEntry.cpp
        impl/filesystem/fsblobstore/utils/DirEntryList.cpp
        impl/filesystem/fsblobstore/FsBlobStore.cpp
        impl/filesystem/fsblobstore/FsBlobView.cpp
        impl/filesystem/fsblobstore/FileBlob.cpp
        impl/filesystem/fsblobstore/FsBlob.cpp
        impl/filesystem/fsblobstore/SymlinkBlob.cpp
        impl/filesystem/fsblobstore/DirBlob.cpp
        impl/filesystem/CryNode.cpp
        impl/filesystem/parallelaccessfsblobstore/DirBlobRef.cpp
        impl/filesystem/parallelaccessfsblobstore/ParallelAccessFsBlobStore.cpp
        impl/filesystem/parallelaccessfsblobstore/ParallelAccessFsBlobStoreAdapter.cpp
        impl/filesystem/parallelaccessfsblobstore/FsBlobRef.cpp
        impl/filesystem/parallelaccessfsblobstore/FileBlobRef.cpp
        impl/filesystem/parallelaccessfsblobstore/SymlinkBlobRef.cpp
        impl/filesystem/entry_helper.cpp
        impl/filesystem/CrySymlink.cpp
        impl/filesystem/CryDir.cpp
        impl/filesystem/cachingfsblobstore/DirBlobRef.cpp
        impl/filesystem/cachingfsblobstore/CachingFsBlobStore.cpp
        impl/filesystem/cachingfsblobstore/FsBlobRef.cpp
        impl/filesystem/cachingfsblobstore/FileBlobRef.cpp
        impl/filesystem/cachingfsblobstore/SymlinkBlobRef.cpp
        impl/filesystem/CryFile.cpp
        impl/filesystem/CryDevice.cpp
        impl/formatv2/AcceptedRootState.cpp
        impl/formatv2/DirectoryRecord.cpp
        impl/formatv2/DirectoryRecordStore.cpp
        impl/formatv2/FormatV2Device.cpp
        impl/formatv2/FileRecord.cpp
        impl/formatv2/FileRecordStore.cpp
        impl/formatv2/KeyDerivation.cpp
        impl/formatv2/ObjectMetadata.cpp
        impl/formatv2/ObjectRecord.cpp
        impl/formatv2/ObjectTreeValidator.cpp
        impl/formatv2/RootAuthentication.cpp
        impl/formatv2/RootContent.cpp
        impl/formatv2/RootContentStore.cpp
        impl/formatv2/RootOpen.cpp
        impl/formatv2/RootOpenWithContent.cpp
        impl/formatv2/RootOpenWithDirectory.cpp
        impl/formatv2/RootPublisher.cpp
        impl/formatv2/RootPublicationStore.cpp
        impl/formatv2/RootRecord.cpp
        impl/formatv2/RootSelection.cpp
        impl/formatv2/RootTransaction.cpp
        impl/formatv2/SymlinkRecord.cpp
        impl/formatv2/SymlinkRecordStore.cpp
        impl/formatv2/Volume.cpp
        impl/formatv2/VolumePathOperations.cpp
        impl/localstate/LocalStateDir.cpp
        impl/localstate/LocalStateMetadata.cpp
        impl/localstate/BasedirMetadata.cpp
)

add_library(${PROJECT_NAME} STATIC ${LIB_SOURCES})
target_link_libraries(${PROJECT_NAME} PUBLIC cpp-utils fspp-interface blockstore blobstore gitversion)
target_add_boost(${PROJECT_NAME}) # TODO Check that dependent projects don't get boost added (use PRIVATE here)
target_enable_style_warnings(${PROJECT_NAME})
target_activate_cpp17(${PROJECT_NAME})

#install(TARGETS ${PROJECT_NAME}
#        DESTINATION lib
#        CONFIGURATIONS Release
#)
