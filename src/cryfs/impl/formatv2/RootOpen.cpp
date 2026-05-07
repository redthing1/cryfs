#include "RootOpen.h"

namespace cryfs {
namespace formatv2 {

RootSelectionResult openRoot(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore) {
  const std::vector<AuthenticatedRoot> authenticatedRoots = publicationStore.loadAuthenticatedRoots();
  const boost::optional<AcceptedRoot> acceptedRoot = acceptedRootStateStore.load();

  RootSelectionResult selection = selectRoot(authenticatedRoots, acceptedRoot);
  if (selection.status != RootSelectionStatus::Selected) {
    return selection;
  }

  acceptedRootStateStore.advanceTo(*selection.selectedRoot);
  return selection;
}

}
}
