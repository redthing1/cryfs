#include "RootSelection.h"

#include <algorithm>
#include <utility>

namespace cryfs {
namespace formatv2 {
namespace {

uint64_t highestEpoch(const std::vector<AuthenticatedRoot> &roots) {
  const auto highest = std::max_element(
    roots.begin(), roots.end(),
    [] (const AuthenticatedRoot &lhs, const AuthenticatedRoot &rhs) {
      return lhs.epoch < rhs.epoch;
    });
  return highest->epoch;
}

bool sameRootIdentity(const AuthenticatedRoot &lhs, const AuthenticatedRoot &rhs) {
  return lhs.epoch == rhs.epoch && lhs.rootId == rhs.rootId;
}

}

RootSelectionResult RootSelectionResult::selected(AuthenticatedRoot root) {
  return RootSelectionResult{RootSelectionStatus::Selected, std::move(root)};
}

RootSelectionResult RootSelectionResult::failed(RootSelectionStatus status) {
  return RootSelectionResult{status, boost::none};
}

bool operator==(const AuthenticatedRoot &lhs, const AuthenticatedRoot &rhs) {
  return sameRootIdentity(lhs, rhs);
}

bool operator!=(const AuthenticatedRoot &lhs, const AuthenticatedRoot &rhs) {
  return !(lhs == rhs);
}

RootSelectionResult selectRoot(
  const std::vector<AuthenticatedRoot> &authenticatedRoots,
  const boost::optional<AcceptedRoot> &localAcceptedRoot) {
  if (authenticatedRoots.empty()) {
    return RootSelectionResult::failed(RootSelectionStatus::NoAuthenticatedRoots);
  }

  const uint64_t epoch = highestEpoch(authenticatedRoots);
  boost::optional<AuthenticatedRoot> selectedRoot = boost::none;
  for (const AuthenticatedRoot &root: authenticatedRoots) {
    if (root.epoch != epoch) {
      continue;
    }
    if (selectedRoot == boost::none) {
      selectedRoot = root;
      continue;
    }
    if (!sameRootIdentity(*selectedRoot, root)) {
      return RootSelectionResult::failed(RootSelectionStatus::AmbiguousRootEpoch);
    }
  }

  if (localAcceptedRoot != boost::none) {
    if (selectedRoot->epoch < localAcceptedRoot->epoch) {
      return RootSelectionResult::failed(RootSelectionStatus::RollbackDetected);
    }
    if (selectedRoot->epoch == localAcceptedRoot->epoch && selectedRoot->rootId != localAcceptedRoot->rootId) {
      return RootSelectionResult::failed(RootSelectionStatus::AcceptedRootMissing);
    }
  }

  return RootSelectionResult::selected(std::move(*selectedRoot));
}

}
}
