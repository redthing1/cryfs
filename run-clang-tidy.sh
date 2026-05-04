#!/bin/bash

set -e

SCRIPT=run-clang-tidy-17.py
ROOTPATH=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR=${BUILD_DIR:-"${ROOTPATH}/build/clang-tidy"}

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

${SCRIPT} -j${NUMCORES} -quiet -config-file "${ROOTPATH}/.clang-tidy" -header-filter "${ROOTPATH}/(src|test)/.*" $@
