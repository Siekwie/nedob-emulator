#!/usr/bin/env sh
set -eu

PROJECT_ROOT="$(pwd)"
PRIMARY_BUILD_DIR="${PROJECT_ROOT}/build"
FALLBACK_BUILD_DIR="${PROJECT_ROOT}/build_local"
BUILD_DIR="${PRIMARY_BUILD_DIR}"

echo "Preparing build directory..."
if [ -d "${BUILD_DIR}" ]; then
   if ! rm -rf "${BUILD_DIR}" 2>/dev/null; then
      echo "Primary build directory is not removable/writable. Switching to ${FALLBACK_BUILD_DIR}"
      BUILD_DIR="${FALLBACK_BUILD_DIR}"
      rm -rf "${BUILD_DIR}" 2>/dev/null || true
   fi
fi
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure: only show output on failure
if ! CCACHE_DISABLE=1 cmake "${PROJECT_ROOT}" > cmake_configure.log 2>&1; then
   cat cmake_configure.log
   exit 1
fi

# Build: log to file; only show output on failure (portable across sh).
if ! CCACHE_DISABLE=1 cmake --build . > build_log.txt 2>&1; then
   cat build_log.txt
   exit 1
fi

echo "Build finished in ${BUILD_DIR}"
