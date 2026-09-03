#!/usr/bin/env bash
# Build a portable Linux libdds.so (DDS_THREADING=STL, Release) for
# python:3.12-slim / the postmortem container.  Do not use
# DDS_AGGRESSIVE_OPT here: it adds -march=native, which is not portable.
#
# Usage (inside Debian/Ubuntu, e.g. wslc python:3.12-slim):
#   bash tools/build_linux_so.sh
#
# Artifacts:
#   build-linux/libdds.so
#   dist/linux-x86_64/libdds.so

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This script must run on Linux (use tools/build_linux_so.ps1 via wslc)." >&2
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1 || ! command -v g++ >/dev/null 2>&1; then
  echo "Installing build dependencies (cmake, g++, make)..."
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y --no-install-recommends cmake g++ make
fi

echo "g++: $(g++ --version | head -n1)"
echo "cmake: $(cmake --version | head -n1)"

cmake -S . -B build-linux \
  -DCMAKE_BUILD_TYPE=Release \
  -DDDS_THREADING=STL \
  -DDDS_AGGRESSIVE_OPT=OFF

cmake --build build-linux --target dds dtest -j"$(nproc)"

SO="build-linux/libdds.so"
if [[ ! -f "$SO" ]]; then
  echo "ERROR: expected $SO" >&2
  exit 1
fi

mkdir -p dist/linux-x86_64
cp -f "$SO" dist/linux-x86_64/libdds.so

echo
echo "Built: $SO"
echo "Copied: dist/linux-x86_64/libdds.so"
ls -l "$SO" dist/linux-x86_64/libdds.so
echo
echo "Runtime NEEDED libs:"
ldd "$SO" || true
echo
echo "Exported ABI symbols:"
nm -D --defined-only "$SO" | awk '/ CalcAllTablesPBNx$| SetMaxThreads$| ErrorMessage$| SetResources$/' || \
  nm -D "$SO" | grep -E 'CalcAllTablesPBNx|SetMaxThreads|ErrorMessage'
