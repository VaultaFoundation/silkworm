#!/bin/bash
set -eo pipefail

# print and run a command
function ee()
{
    echo "$ $*"
    eval "$@"
}

# debug code
echo "CC='${CC}'"
echo "CXX='${CXX}'"
ee cmake --version

# build
ee mkdir build
ee pushd build
ee cmake ..
ee make all_unit_tests -j "$(nproc)"

# pack
ee popd
ee 'tar -czf build.tar.gz build/cmd/test/*_test'

echo "Done! - ${0##*/}"
