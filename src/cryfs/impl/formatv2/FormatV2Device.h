#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_FORMATV2DEVICE_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_FORMATV2DEVICE_H_

#include "VolumePathOperations.h"

#include <boost/filesystem/path.hpp>
#include <boost/optional.hpp>
#include <cpp-utils/crypto/symmetric/EncryptionKey.h>
#include <cpp-utils/macros.h>
#include <cpp-utils/pointer/unique_ref.h>
#include <cpp-utils/random/RandomGenerator.h>
#include <fspp/fs_interface/Device.h>

#include <mutex>

namespace cryfs {
namespace formatv2 {

class FormatV2Device final: public fspp::Device {
public:
  FormatV2Device(
    VolumeLayout layout,
    FilesystemId filesystemId,
    cpputils::EncryptionKey masterKey);
  FormatV2Device(
    VolumeLayout layout,
    FilesystemId filesystemId,
    cpputils::EncryptionKey masterKey,
    cpputils::RandomGenerator *randomGenerator);

  statvfs statfs() override;

  boost::optional<cpputils::unique_ref<fspp::Node>> Load(
    const boost::filesystem::path &path) override;
  boost::optional<cpputils::unique_ref<fspp::File>> LoadFile(
    const boost::filesystem::path &path) override;
  boost::optional<cpputils::unique_ref<fspp::Dir>> LoadDir(
    const boost::filesystem::path &path) override;
  boost::optional<cpputils::unique_ref<fspp::Symlink>> LoadSymlink(
    const boost::filesystem::path &path) override;

  void sync() const override;

  const VolumeLayout &layout() const;
  FilesystemId filesystemId() const;
  const cpputils::EncryptionKey &masterKey() const;
  cpputils::RandomGenerator *randomGenerator() const;
  std::unique_lock<std::mutex> lockOperation() const;

private:
  mutable std::mutex _operationMutex;
  VolumeLayout _layout;
  FilesystemId _filesystemId;
  cpputils::EncryptionKey _masterKey;
  cpputils::RandomGenerator *_randomGenerator;

  DISALLOW_COPY_AND_ASSIGN(FormatV2Device);
};

}
}

#endif
