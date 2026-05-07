#pragma once
#ifndef MESSMER_CRYFS_IMPL_FORMATV2_ROOTOPEN_H_
#define MESSMER_CRYFS_IMPL_FORMATV2_ROOTOPEN_H_

#include "AcceptedRootState.h"
#include "RootPublicationStore.h"
#include "RootSelection.h"

namespace cryfs {
namespace formatv2 {

// A Selected result means the returned root was also recorded in local
// accepted-root state. Local-state corruption and persistence failures throw.
RootSelectionResult openRoot(
  const RootPublicationStore &publicationStore,
  const AcceptedRootStateStore &acceptedRootStateStore);

}
}

#endif
