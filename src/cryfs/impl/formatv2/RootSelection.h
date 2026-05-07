#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ROOTSELECTION_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ROOTSELECTION_H_

#include "RootTypes.h"

#include <boost/optional.hpp>

#include <cstdint>
#include <vector>

namespace cryfs {
namespace formatv2 {

enum class RootSelectionStatus : uint8_t {
  Selected,
  NoAuthenticatedRoots,
  RollbackDetected,
  AcceptedRootMissing,
  AmbiguousRootEpoch
};

struct RootSelectionResult final {
  RootSelectionStatus status;
  boost::optional<AuthenticatedRoot> selectedRoot;

  static RootSelectionResult selected(AuthenticatedRoot root);
  static RootSelectionResult failed(RootSelectionStatus status);
};

RootSelectionResult selectRoot(
  const std::vector<AuthenticatedRoot> &authenticatedRoots,
  const boost::optional<AcceptedRoot> &localAcceptedRoot);

}
}

#endif
