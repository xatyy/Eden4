# Building Eden4

Eden4 cross-compiles on Linux (or WSL 2, or a Linux container on macOS) into a PS4 fake PKG.
You don't need a console to build, only to run the result. A first build takes a few hours (libc++,
Mesa and Eden); later builds only recompile what changed.

## What you need

An x86-64 Ubuntu 26.04 system or container with:

```sh
apt install clang-21 lld-21 llvm-21 libclang-rt-21-dev cmake ninja-build make nasm autoconf automake \
    libtool pkg-config git curl patch xz-utils zstd python3 python3-pil python3-venv python3-mako \
    python3-yaml meson glslang-tools libdrm-dev libxml2-16
```

and the **OpenOrbis PS4 toolchain from PacBrew** in `/opt/pacbrew/ps4/openorbis` (set `OPENORBIS`
otherwise). The pinned packages and their SHA-256 hashes are in
[`tools/pacbrew`](../tools/pacbrew):

```sh
python3 tools/pacbrew/fetch-pacbrew.py     # downloads and verifies them into tools/pacbrew/downloads
sudo python3 tools/pacbrew/install-pacbrew.py
```

PacBrew's packaging tool needs the .NET runtime it ships with; `ps4-env.sh` sets up the rest.

## Where things go

Everything built goes under `PS4_BUILD_ROOT` (default `~/dolphinps4-build`, the name the runtime
scripts use). For example:

```sh
export PS4_BUILD_ROOT=$HOME/eden4-build
```

```
$PS4_BUILD_ROOT/
├── src/mesa/     Mesa 26.0.8 with runtime/patches/mesa-ps4.patch
├── src/libdrm/   libdrm headers (for Mesa's build only; the PS4 has no DRM)
├── sysroot/      libc++ 21 and the runtime for the PS4 target
├── build/mesa/   RADV built for the PS4
├── build/eden/   Eden built for the PS4
└── out/          Eden4-01.00.pkg
```

## Steps

All paths below are relative to the repository root.

1. **Sysroot**: libc++ 21 for the PS4 target, then the runtime pieces linked ahead of OpenOrbis'
   C library.

   ```sh
   runtime/toolchain/build-libcxx.sh
   runtime/toolchain/build-runtime.sh
   ```

2. **Mesa (RADV for the PS4 GPU)**: unpack Mesa 26.0.8, apply the patch, add the GNM winsys, and
   put libdrm's headers next to it; then build.

   ```sh
   mkdir -p $PS4_BUILD_ROOT/src && cd $PS4_BUILD_ROOT/src
   curl -LO https://archive.mesa3d.org/mesa-26.0.8.tar.xz
   tar xf mesa-26.0.8.tar.xz && mv mesa-26.0.8 mesa
   (cd mesa && patch -p1 < /path/to/Eden4/runtime/patches/mesa-ps4.patch \
       && cp /path/to/Eden4/runtime/port/mesa/ac_ps4_drm.c src/amd/common/)
   git clone --depth 1 --branch libdrm-2.4.125 https://gitlab.freedesktop.org/mesa/drm.git libdrm
   cd /path/to/Eden4 && runtime/scripts/build-mesa.sh setup
   ```

   The script's comments list the host tools it uses (meson, mako, pyyaml, glslangValidator).

3. **Eden4**: configures Eden for the PS4 (the first time downloads Eden's dependencies), builds
   it and packages it.

   ```sh
   eden/ps4/build.sh
   ```

   Later builds: `eden/ps4/build.sh --no-configure`. The package is
   `$PS4_BUILD_ROOT/out/Eden4-01.00.pkg`.

Options (environment variables):

| Variable | Default | Effect |
|---|---|---|
| `EDEN4_VERSION` | `01.00` | Package version (XX.YY) |
| `EDEN4_TITLE_ID` | `EDEN00002` | Title ID (4 letters + 5 digits). `EDEN00002` installs over the Eden PS4 test builds |
| `EDEN_SFO_ATTRIBUTE` | `0` | PARAM.SFO attributes; `0x00010000` adds the seventh CPU core |
| `EDEN_PKG_SUFFIX` | (none) | Added to the package file name |
| `EDEN4_SELF_TEST` | `0` | `1` adds the CPU self-test program (two "CPU self-test" entries on HOME) for development builds |
| `EDEN_PS4_BUILD` | `$PS4_BUILD_ROOT/build/eden` | Eden build directory |

Never run two builds in the same build directory at once.

PacBrew's packaging tools (`create-fself`, `PkgTool.Core`) are x86-64 Linux programs. On an ARM host
(e.g. Apple silicon), compile in a native container and run `eden/ps4/build.sh --no-configure` in an
x86-64 (emulated) container that shares the same build directory.

## Custom icon

Place your images in [`branding/`](../branding) before building:

- `branding/icon0.png` (or `.jpg`): any square picture; it is centre-cropped and scaled to 512x512.
- `branding/pic1.png` (or `.jpg`), optional: the background shown behind the app on the PS4 home
  screen, scaled to 1920x1080.

Without them the package uses Eden's logo. `eden/ps4/build.sh --no-configure` repackages quickly.

## Tools for development

- **HOME menu preview** (any PC with a C++20 compiler, Python and Pillow): renders every HOME menu
  screen with made-up games to PNG files, and drives the menu with random input.

  ```sh
  eden/ps4/ui-preview/build.sh /tmp/ui-preview
  ```

- **JIT test harness** ([`tools/jit-harness`](../tools/jit-harness)): builds the real Dynarmic for
  x86-64 Linux and runs AArch64 test programs (a zstd decoder and a torture test) through it,
  comparing results; it found the register allocation bug behind the 00.09-00.11 hangs.
- **Diagnostics package** (`eden/ps4/build-diagnostics.sh`): a tiny app that checks threads, the JIT,
  VideoOut and memory on a console without emulating anything.

Before releasing, compare the system functions the program imports with the last working build
(`llvm-nm-21 -u`); one missing import (`sceRtcGetCurrentClockLocalTime`) closed the app without a
crash log in test builds 00.25-00.27.
