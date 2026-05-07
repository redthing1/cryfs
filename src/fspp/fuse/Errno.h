#pragma once
#ifndef MESSMER_FSPP_FUSE_ERRNO_H_
#define MESSMER_FSPP_FUSE_ERRNO_H_

#include <cerrno>

namespace fspp {
namespace fuse {

inline int unsupportedOperationErrno() {
#if defined(EOPNOTSUPP)
  return EOPNOTSUPP;
#elif defined(ENOTSUP)
  return ENOTSUP;
#else
  return EINVAL;
#endif
}

}
}

#endif
