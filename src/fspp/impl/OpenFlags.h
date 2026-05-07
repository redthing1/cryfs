#pragma once
#ifndef MESSMER_FSPP_IMPL_OPENFLAGS_H_
#define MESSMER_FSPP_IMPL_OPENFLAGS_H_

#include <fcntl.h>

namespace fspp {
namespace impl {

inline bool containsOpenFlag(int flags, int flag) {
  return flag != 0 && (flags & flag) == flag;
}

inline bool hasSynchronousOpenFlag(int flags) {
#if defined(O_SYNC)
  if (containsOpenFlag(flags, O_SYNC)) {
    return true;
  }
#endif
#if defined(O_DSYNC)
  if (containsOpenFlag(flags, O_DSYNC)) {
    return true;
  }
#endif
#if defined(O_RSYNC)
  if (containsOpenFlag(flags, O_RSYNC)) {
    return true;
  }
#endif
  return false;
}

}
}

#endif
