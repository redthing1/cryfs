#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ROOTOPENWITHCONTENT_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ROOTOPENWITHCONTENT_H_

#include "AcceptedRootState.h"
#include "RootContentStore.h"
#include "RootPublicationStore.h"
#include "RootSelection.h"

#include <boost/optional.hpp>

namespace cryfs {
namespace formatv2 {

enum class RootOpenStatus {
  Selected,
  NoAuthenticatedRoots,
  RollbackDetected,
  AcceptedRootMissing,
  AmbiguousRootEpoch,
  RootContentUnavailable,
  RootDirectoryUnavailable,
  RootTreeInvalid,
};

struct RootOpenResult final {
  RootOpenStatus status;
  boost::optional<AuthenticatedRoot> selectedRoot;
  boost::optional<RootContent> rootContent;
};

// A Selected result means root content was loaded and authenticated, but local
// accepted-root state was not advanced.
RootOpenResult selectRootWithContent(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore);

// A Selected result means root content was loaded and local accepted-root state
// was advanced. Local-state corruption and persistence failures throw.
RootOpenResult openRootWithContent(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore,
  const RootContentStore &rootContentStore);

}
}

#endif
