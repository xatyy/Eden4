#!/usr/bin/env bash
# Cross-builds Mesa's RADV (Vulkan for AMD GCN) for the PS4: the GNM path for Dolphin's Vulkan
# backend. Mesa source: $PS4_BUILD_ROOT/src/mesa (mesa-26.0.8), libdrm headers from
# $PS4_BUILD_ROOT/src/libdrm (headers only - there is no DRM on the PS4; the PS4 winsys submits
# through libSceGnmDriver). Host tools: meson/mako/pyyaml in $PS4_BUILD_ROOT/mesa-venv,
# glslangValidator in $PS4_BUILD_ROOT/host-tools (built from Dolphin's Externals/glslang).
#   build-mesa.sh [setup]   (setup: reconfigure from scratch)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/ps4-env.sh"
ROOT="$PS4_BUILD_ROOT"
SYSROOT="${PS4_SYSROOT:-$ROOT/sysroot}"
MESA="$ROOT/src/mesa"
build="$ROOT/build/mesa"
export PATH="$ROOT/mesa-venv/bin:$ROOT/host-tools/bin:$PATH"

# Header-only libdrm / libdrm_amdgpu packages for meson's dependency() checks.
pc="$ROOT/mesa-pkgconfig"
mkdir -p "$pc"
rm -f "$pc/libdrm.pc"
for name in libdrm_amdgpu; do
    cat > "$pc/$name.pc" <<EOF
Name: $name
Description: libdrm headers only (PS4 has no DRM)
Version: 2.4.125
Cflags: -I$ROOT/src/libdrm -I$ROOT/src/libdrm/include/drm -I$ROOT/src/libdrm/amdgpu
Libs:
EOF
done

flags="'-target', 'x86_64-pc-freebsd12-elf', '-D_GNU_SOURCE', '-D__PS4__', '-D__OPENORBIS__',
  '-D__ORBIS__', '-DPS4', '-D__BSD_VISIBLE', '-D_BSD_SOURCE', '-fPIC', '-funwind-tables',
  '-march=btver2', '-isysroot', '$OPENORBIS', '-isystem', '$SYSROOT/libc-overlay',
  '-isystem', '$HERE/../port/mesa-overlay', '-I$ROOT/src/libdrm', '-I$ROOT/src/libdrm/include/drm',
  '-isystem', '$OPENORBIS/include', '-I$OPENORBIS/usr/include'"
link="'-fuse-ld=$OPENORBIS/bin/ld.lld', '-nostdlib', '-Wl,-m,elf_x86_64', '-Wl,-pie',
  '-Wl,--eh-frame-hdr', '-Wl,--script,$OPENORBIS/link.x', '-L$OPENORBIS/lib',
  '$OPENORBIS/lib/crt1.o', '-lkernel', '-lc', '-lclang_rt.builtins-x86_64', '-lSceLibcInternal'"
cross="$ROOT/mesa-ps4-cross.ini"
cat > "$cross" <<EOF
[binaries]
c = 'clang-21'
cpp = 'clang++-21'
ar = 'llvm-ar-21'
strip = 'llvm-strip-21'
pkg-config = 'pkg-config'

[properties]
needs_exe_wrapper = true
pkg_config_libdir = '$pc'

[built-in options]
c_args = [$flags]
cpp_args = ['-nostdinc++', '-isystem', '$SYSROOT/include/c++/v1', $flags]
c_link_args = [$link]
cpp_link_args = [$link, '$SYSROOT/lib/libc++.a', '$SYSROOT/lib/libc++abi.a',
  '$OPENORBIS/lib/libunwind.a']

[host_machine]
system = 'freebsd'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF

# The PS4 implementation of the amdgpu kernel interface lives in this repo.
tr -d '\015' < "$HERE/../port/mesa/ac_ps4_drm.c" > "$MESA/src/amd/common/ac_ps4_drm.c"

if [ "${1:-}" = setup ] || [ ! -f "$build/build.ninja" ]; then
    rm -rf "$build"
    PKG_CONFIG_LIBDIR="$pc" meson setup "$build" "$MESA" --cross-file "$cross" \
        -Dbuildtype=release -Db_ndebug=true -Ddefault_library=static \
        -Dvulkan-drivers=amd -Dgallium-drivers= -Dplatforms= -Dllvm=disabled \
        -Damd-use-llvm=false -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dglx=disabled \
        -Degl=disabled -Dgbm=disabled -Dzstd=disabled -Dexpat=disabled -Dxmlconfig=disabled \
        -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false \
        -Dshader-cache=disabled -Dvulkan-layers= -Dtools= -Dvideo-codecs= -Dspirv-tools=disabled \
        -Dperfetto=false -Dselinux=false
fi
# The static libraries RADV needs (meson's default targets also include host tools such as
# ac_ib_parser, which don't link for the PS4 and aren't needed).
libs=(src/amd/vulkan/libvulkan_radeon.a src/amd/addrlib/libaddrlib.a
    src/amd/common/libamd_common.a src/amd/compiler/libaco.a src/c11/impl/libmesa_util_c11.a
    src/compiler/libcompiler.a src/compiler/nir/libnir.a src/compiler/spirv/libvtn.a
    src/util/blake3/libblake3.a src/util/libmesa_util.a src/util/libmesa_util_clflush.a
    src/util/libmesa_util_clflushopt.a src/util/libmesa_util_simd.a src/util/libparson.a
    src/util/libxmlconfig.a src/vulkan/runtime/libvulkan_instance.a
    src/vulkan/runtime/libvulkan_runtime.a src/vulkan/util/libvulkan_util.a
    src/vulkan/wsi/libvulkan_wsi.a)
# RADV derives its pipeline-cache UUID from the binary's build-id note via dladdr, which the PS4
# lacks ("cannot generate UUID"): give it the Mesa commit instead (stable, so editing
# port/mesa/ac_ps4_drm.c doesn't rebuild all of RADV; the disk shader cache is off).
build_id="$(git -C "$MESA" rev-parse HEAD)"
meson configure "$build" -Dradv-build-id="$build_id" >/dev/null
ninja -C "$build" "${@:2}" "${libs[@]}"
