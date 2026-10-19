#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
TEST_BUILD="${PS4_HOST_TEST_BUILD:-/tmp/eden-ps4-host-tests}"
mkdir -p "$TEST_BUILD"
args=()
read -r -a extra_flags <<< "${PS4_TEST_CXXFLAGS:-}"
for include in "$ROOT/.cache/cpm/boost/1.92.0/libs/"*/include; do args+=("-I$include"); done
"${HOST_CXX:-clang++-21}" "${extra_flags[@]}" -std=c++20 -D__OPENORBIS__ -DARCHITECTURE_x86_64=1 -DFMT_HEADER_ONLY=1 \
    -I"$HERE/include" -I"$ROOT/src" -I"$ROOT/.cache/cpm/fmt/12.1.0/include" "${args[@]}" \
    "$HERE/host-memory.cpp" "$ROOT/src/common/host_memory.cpp" -o "$TEST_BUILD/host-memory"
"$TEST_BUILD/host-memory"
