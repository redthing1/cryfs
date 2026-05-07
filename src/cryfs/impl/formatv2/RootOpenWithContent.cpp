#include "RootOpenWithContent.h"

#include <stdexcept>
#include <vector>

namespace cryfs {
namespace formatv2 {
namespace {

RootOpenStatus rootOpenStatusFromSelectionStatus(RootSelectionStatus status) {
  switch (status) {
    case RootSelectionStatus::Selected:
      return RootOpenStatus::Selected;
    case RootSelectionStatus::NoAuthenticatedRoots:
      return RootOpenStatus::NoAuthenticatedRoots;
    case RootSelectionStatus::RollbackDetected:
      return RootOpenStatus::RollbackDetected;
    case RootSelectionStatus::AcceptedRootMissing:
      return RootOpenStatus::AcceptedRootMissing;
    case RootSelectionStatus::AmbiguousRootEpoch:
      return RootOpenStatus::AmbiguousRootEpoch;
  }
  throw std::runtime_error("Unknown format-v2 root selection status");
}

}

RootOpenResult selectRootWithContent(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore) {
  const std::vector<AuthenticatedRoot> authenticatedRoots = publicationStore.loadAuthenticatedRoots();
  const boost::optional<AcceptedRoot> acceptedRoot = acceptedRootStateStore.load();

  const RootSelectionResult selection = selectRoot(authenticatedRoots, acceptedRoot);
  if (selection.status != RootSelectionStatus::Selected) {
    return RootOpenResult{
      rootOpenStatusFromSelectionStatus(selection.status),
      boost::none,
      boost::none
    };
  }

  const boost::optional<RootContent> rootContent = rootContentStore.load(*selection.selectedRoot);
  if (rootContent == boost::none) {
    return RootOpenResult{
      RootOpenStatus::RootContentUnavailable,
      selection.selectedRoot,
      boost::none
    };
  }

  return RootOpenResult{
    RootOpenStatus::Selected,
    selection.selectedRoot,
    rootContent
  };
}

RootOpenResult openRootWithContent(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore) {
  RootOpenResult result = selectRootWithContent(
    publicationStore,
    acceptedRootStateStore,
    rootContentStore);
  if (result.status == RootOpenStatus::Selected) {
    acceptedRootStateStore.advanceTo(*result.selectedRoot);
  }
  return result;
}

}
}
