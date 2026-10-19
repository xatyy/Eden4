# Credits

Eden4 is a port: nearly all of the code it runs was written by other projects. Thank you to
everyone below.

## The emulator

- **[Eden](https://eden-emu.dev)** ([source](https://git.eden-emu.dev/eden-emu/eden)) - the Nintendo
  Switch emulator Eden4 is built from, by the Eden developers and contributors. GPL-3.0-or-later.
- **[yuzu](https://github.com/yuzu-emu)** - the emulator Eden continues, by the yuzu team and its
  contributors (GPL-2.0-or-later / GPL-3.0-or-later parts).
- **[Dynarmic](https://github.com/merryhime/dynarmic)** by MerryMage (merryhime) and contributors - the
  ARM → x86-64 dynamic recompiler, as maintained in Eden's tree.
- **Citra** - the 3DS emulator whose code base yuzu grew from.

## The PS4 port

- **[DolphinPS4](https://github.com/iHaiDeeZ/DolphinPS4)** by **ShiroKlein** and contributors
  (GPL-2.0-or-later) - the PS4 runtime Eden4 uses ([`runtime/`](runtime)): start-up code, the
  memory allocator and heaps, string routines, crash log, sampling profiler, the toolchain files, the
  packaging script, and the **GNM winsys that runs Mesa's RADV on the PS4 GPU**
  ([`runtime/port/mesa`](runtime/port/mesa), [`runtime/patches/mesa-ps4.patch`](runtime/patches/mesa-ps4.patch)).
  Built on [Dolphin](https://dolphin-emu.org) by the Dolphin Emulator Project.
- **[Mesa](https://mesa3d.org)** - RADV, the Vulkan driver for AMD GPUs (Mesa 26.0.8). MIT and per-file.
- **[OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain)** - the C runtime,
  PS4 system headers and the PKG tools.
- **[PacBrew](https://github.com/PacBrew)** - the PS4 toolchain distribution used for builds.
- **[LLVM](https://llvm.org)** - clang, lld and libc++ (Apache-2.0 with LLVM exception).
- **[love-ps4](https://github.com/Mari0/love-ps4)** - reference for linking, the runtime and packaging
  on the PS4 (via DolphinPS4).
- **[dlmalloc](https://gee.cs.oswego.edu/dl/html/malloc.html)** by Doug Lea - the memory allocator (CC0).
- **[stb](https://github.com/nothings/stb)** by Sean Barrett - image decoding and resizing (public domain / MIT).

## Inspiration and references

- **[ProsperoEden](https://github.com/blackbearreloaded/ProsperoEden)** by **BlackBearReloaded** - the
  PlayStation 5 port of Eden, and the model for this project's goal: Eden as a native console app.
- **[SwitchLauncher / Switch UI](https://github.com/kasobos67/SwitchLauncher)** by **kasobos67**
  (GPL-3.0-or-later) - a Switch-style launcher for ProsperoEden. Eden4's HOME menu follows its
  layout, positions, colours and behaviour, rewritten natively in C++. None of its artwork, fonts or
  sounds are used (they are Nintendo's): Eden4's icons are drawn by
  [`eden/ps4/generate-ui-assets.py`](eden/ps4/generate-ui-assets.py) and its sounds are synthesised.
- **The Nintendo Switch HOME menu** - the look the HOME menu imitates. Nintendo Switch is a trademark of
  Nintendo; Eden4 is not affiliated with Nintendo.
- **[shadPS4](https://github.com/shadps4-emu/shadPS4)** - its PARAM.SFO attribute and PS4 Pro tiling code
  were the reference for the PS4 Pro mode experiments.
- **Mihawk's [PS5 Mesa](https://github.com/mihawk-99/PS5_Mesa)** and the PS5 homebrew projects credited
  by ProsperoEden - prior work on running RADV on PlayStation hardware.

## Fonts

- **[Nunito](https://github.com/googlefonts/nunito)** by the Nunito Project Authors - HOME menu text.
  SIL Open Font License 1.1 ([`runtime/fonts/Nunito-OFL.txt`](runtime/fonts/Nunito-OFL.txt)).

## Bundled in the package

Eden's own third-party libraries (Boost, fmt, OpenSSL, FFmpeg, Opus, zstd, lz4, zlib, nlohmann/json,
VulkanMemoryAllocator, xbyak, sirit, SPIRV-Headers and others) keep their licences; the package
includes their licence texts in `licenses/`, collected by
[`eden/ps4/collect-licenses.py`](eden/ps4/collect-licenses.py).

## The port itself

Eden4's PS4 frontend, memory management, JIT changes, HOME menu and tools were written for this
project and are released under GPL-3.0-or-later.
