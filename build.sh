#!/usr/bin/env bash
set -e

if [ -d "./build" ]; then
   echo "Build exists, removing directory for clean build"
   rm -rf ./build
   mkdir build
else
   echo "No build folder present, creating one and continuing"
   mkdir build
fi

cd build

# Configure: only show output on failure
if ! cmake .. > cmake_configure.log 2>&1; then
   cat cmake_configure.log
   exit 1
fi

# Build: log to file (and show on screen)
cmake --build . 2>&1 | tee build_log.txt

echo "Build finished!"
