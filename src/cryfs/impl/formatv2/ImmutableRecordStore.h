#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_IMMUTABLERECORDSTORE_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_IMMUTABLERECORDSTORE_H_

#include <boost/filesystem/path.hpp>
#include <boost/optional.hpp>
#include <cpp-utils/data/Data.h>
#include <cpp-utils/system/AtomicFile.h>

#include <stdexcept>
#include <string>

namespace cryfs {
namespace formatv2 {

template<class Record, class LoadExisting>
void storeImmutableRecord(
  const boost::filesystem::path &path,
  const cpputils::Data &serializedRecord,
  LoadExisting loadExisting,
  const Record &expectedRecord,
  const char *recordDescription) {
  if (cpputils::storeFileAtomicallyIfAbsent(path, serializedRecord.data(), serializedRecord.size())) {
    return;
  }

  const boost::optional<Record> existingRecord = loadExisting();
  if (existingRecord != boost::none && *existingRecord == expectedRecord) {
    return;
  }

  throw std::runtime_error(
    std::string("Refusing to overwrite immutable format-v2 ") + recordDescription + ": " + path.string());
}

}
}

#endif
