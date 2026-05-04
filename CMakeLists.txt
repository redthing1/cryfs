cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# TODO Perf test:
#  - try if setting CRYPTOPP_NATIVE_ARCH=ON and adding -march=native to the compile commands for cryfs source files makes a difference
#    -> if yes, offer a cmake option to enable both of these

project(cryfs LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake-utils)
include(utils)

require_gcc_version(7.0)
require_clang_version(7.0)

option(BUILD_TESTING "Build test cases" OFF)
option(CRYFS_UPDATE_CHECKS "Enable online update and security-vulnerability checks" OFF)
option(DISABLE_OPENMP "Build without OpenMP libraries. This will cause performance degradations." OFF)

# The following options are helpful for development and maintenance.
option(USE_WERROR "build with -Werror flag" OFF)
option(USE_CLANG_TIDY "build with clang-tidy checks enabled" OFF)
option(USE_IWYU "build with iwyu checks enabled" OFF)
option(CLANG_TIDY_WARNINGS_AS_ERRORS "treat clang-tidy warnings as errors" OFF)

if (MSVC)
  option(DOKAN_PATH "Location of the Dokan library, e.g. C:\\Program Files\\Dokan\\DokanLibrary-2.2.0" "")
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# We don't use LTO because crypto++ has problems with it, see https://github.com/weidai11/cryptopp/issues/1031 and https://www.cryptopp.com/wiki/Link_Time_Optimization

if(MSVC)
  # Needed to avoid a msvc build error "fatal error C1128: number of sections exceeded object file format limit: compile with /bigobj"
  add_definitions(/bigobj)
endif()

include(cmake-utils/Dependencies.cmake)

add_subdirectory(vendor EXCLUDE_FROM_ALL)
add_subdirectory(src)
add_subdirectory(doc)
if(BUILD_TESTING)
  enable_testing()
endif()
add_subdirectory(test)
