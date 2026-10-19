#!/bin/bash
HARNESS="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HARNESS/../.." && pwd)"
# Builds libdynarmic_host.a for x86-64 Linux (x86-64 Linux; run eden/ps4/configure.sh once first so Eden's dependency cache exists).
set -e
W="$REPO"
H="$HARNESS"
OUT=$H/build
mkdir -p $OUT/obj
BOOST=$($H/gen_includes.sh)
FLAGS="-std=c++20 -O1 -g0 -fno-strict-aliasing -w -DXBYAK_STRICT_CHECK_MEM_REG_SIZE=0 -DXBYAK_OLD_DISP_CHECK=1 \
 -DDYNARMIC_ENABLE_CPU_FEATURE_DETECTION=1 -DMCL_IGNORE_ASSERTS=1 -DFMT_USE_USER_DEFINED_LITERALS=1 -DFMT_HEADER_ONLY \
 -DARCHITECTURE_x86_64 -DDYNARMIC_PAGE_REUSE_TEST=1 \
 -I$H/shim -I$W/eden/src/dynarmic/src -I$W/eden/src -I$W/eden/.cache/cpm/xbyak/v7.40.1 -I$W/eden/.cache/cpm/fmt/12.1.0/include $BOOST"
echo "$FLAGS" > $OUT/flags.txt
rm -f $OUT/build.mk
objs=""
while read f; do
  o=$OUT/obj/$(echo $f | tr '/' '_').o
  objs="$objs $o"
  printf '%s: %s\n\tg++ %s -MMD -MP -c $< -o $@\n' "$o" "$W/eden/src/dynarmic/src/dynarmic/$f" "$FLAGS" >> $OUT/build.mk
done < $H/dynarmic_sources.txt
printf 'all: %s\n\tar rcs %s/libdynarmic_host.a $^\n' "$objs" "$OUT" >> $OUT/build.mk
echo '-include $(wildcard '$OUT'/obj/*.d)' >> $OUT/build.mk
make -f $OUT/build.mk -j8 -k all 2>&1 | tail -40
