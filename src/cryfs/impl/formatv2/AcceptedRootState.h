#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ACCEPTEDROOTSTATE_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ACCEPTEDROOTSTATE_H_

#include "RootTypes.h"

#include <boost/filesystem/path.hpp>
#include <boost/optional.hpp>
#include <cpp-utils/data/Data.h>

namespace cryfs {
namespace formatv2 {

struct AcceptedRootState final {
  FilesystemId filesystemId;
  AcceptedRoot acceptedRoot;
};

bool operator==(const AcceptedRootState &lhs, const AcceptedRootState &rhs);
bool operator!=(const AcceptedRootState &lhs, const AcceptedRootState &rhs);

cpputils::Data serializeAcceptedRootState(const AcceptedRootState &state);
boost::optional<AcceptedRootState> deserializeAcceptedRootState(const cpputils::Data &data);

class AcceptedRootStateStore final {
public:
  AcceptedRootStateStore(boost::filesystem::path filepath, FilesystemId filesystemId);

  boost::optional<AcceptedRoot> load() const;
  void store(const AcceptedRoot &root) const;
  void advanceTo(const AcceptedRoot &root) const;

private:
  boost::filesystem::path _filepath;
  FilesystemId _filesystemId;
};

}
}

#endif
