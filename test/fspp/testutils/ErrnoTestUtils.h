#pragma once
#ifndef MESSMER_FSPP_TEST_TESTUTILS_ERRNOTESTUTILS_H_
#define MESSMER_FSPP_TEST_TESTUTILS_ERRNOTESTUTILS_H_

#include <cerrno>

namespace fspp_test {

inline bool isUnsupportedOperationErrno(int value) {
#if defined(EOPNOTSUPP)
  if (value == EOPNOTSUPP) {
    return true;
  }
#endif
#if defined(ENOTSUP)
  if (value == ENOTSUP) {
    return true;
  }
#endif
  return value == EINVAL;
}

}

#endif
