#!/bin/bash

set -e

ROOTPATH=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${ROOTPATH}/build/iwyu"}

export NUMCORES=`nproc` && if [ ! -n "$NUMCORES" ]; then export NUMCORES=`sysctl -n hw.ncpu`; fi
echo Using ${NUMCORES} cores

cmake -S "${ROOTPATH}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCRYFS_UPDATE_CHECKS=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Filter all third party code from the compilation database
cd "${BUILD_DIR}"
cat compile_commands.json | jq "map(select(.file | test(\"^${ROOTPATH}/(src|test)/.*$\")))" > compile_commands2.json
rm compile_commands.json
mv compile_commands2.json compile_commands.json

if [ "$1" = "-fix" ]; then
  TMPFILE=$(mktemp -t iwyu.XXXXXXXX.out)

  function cleanup {
    rm -f "${TMPFILE}"
  }
  trap cleanup EXIT

  iwyu_tool -j${NUMCORES} -p. "${@:2}" | tee "${TMPFILE}"
  fix_include < "${TMPFILE}"
else
  iwyu_tool -j${NUMCORES} -p. "$@"
fi
