# How Eden4 works

Notes for contributors: what the PS4 port changes in Eden, and why.

## Repository layout

| Path | What |
|---|---|
| `eden/` | Eden's source with the PS4 port. Upstream: <https://git.eden-emu.dev/eden-emu/eden> |
| `eden/src/yuzu_ps4/` | The PS4 frontend: `main.cpp` (start-up, settings, game loop, HOME menu host), `platform.*` (controllers, VideoOut), `ui_*` (HOME menu) |
| `eden/ps4/` | Build and packaging scripts, the asset generator, the HOME menu preview, the diagnostics app |
| `runtime/` | The PS4 runtime from DolphinPS4: `port/` (start-up, heap, crash log, profiler, Mesa winsys), `toolchain/`, `scripts/`, `patches/mesa-ps4.patch`, `fonts/` |
| `tools/jit-harness/` | Dynarmic test harness for a PC |
| `tools/pacbrew/` | Pinned PacBrew SDK packages |
| `branding/` | Package icon and home-screen background |

## Memory

A PS4 game gets 4608 MiB of **direct memory** (GPU-visible, physically contiguous) plus a few
hundred MiB of **flexible memory**. The Switch's 4 GiB of RAM is committed in direct memory up front,
which leaves about 480 MiB for everything the GPU uses. Most of the port's memory work follows from that:

- **C heap** (`runtime/port/ps4_runtime.cpp`): dlmalloc on 512 MiB of system flexible memory, then
  32 MiB overflow steps from system flexible, regular flexible and finally direct memory, never taking
  the last 256 MiB of direct memory that the GPU needs. The heap uses pthread mutexes (a spin lock
  deadlocked under load) and per-thread re-entry guards.
- **GPU page table** (`common/multi_level_page_table.inc`): reserved address space, backed 64 KiB at a
  time when a mapping first touches it.
- **Caches** (`video_core/vulkan_common/vulkan_device.cpp`, `texture_cache.h`, `buffer_cache.h`): the
  GPU budget is the largest free direct memory block at start; garbage collection starts at 30-40 % of
  it.
- **Staging buffers and allocations** (`vk_staging_buffer_pool.cpp`, `vulkan_memory_allocator.cpp`):
  capped at 64 MiB; before an allocation that would leave less than 48 MiB free, or after a failed
  one, Eden waits for the GPU, frees idle staging buffers and retries.

## CPU (Dynarmic)

There is no fastmem on the PS4, so every guest load and store walks a software page table. The JIT
changes (all under `__OPENORBIS__`) make that walk cheap:

- `SparseLargeVector` stores page table entries in 8192-entry chunks with a chunk directory in a host
  register (R14); a "direct" mirror 64 KiB into each chunk holds `host - guest` offsets, so a lookup
  is two loads and an add.
- `PS4PageReusePass` (IR) and `A64EdenPageLookup` reuse one lookup for several accesses to the same
  page.
- The register allocator can use R13 and moves values into callee-saved registers instead of spilling.
- The code cache is made writable only in a window around the code being emitted.

The harness in `tools/jit-harness` runs these changes on a PC against AArch64 test programs; run it
after any JIT change. Its tick and block bisection scripts find the first wrong block.

## GPU

Eden's Vulkan renderer runs on Mesa RADV with DolphinPS4's GNM winsys (`runtime/port/mesa/ac_ps4_drm.c`):
buffers are direct memory, command buffers go to the GPU through `sceGnmSubmitCommandBuffers`. The
"swapchain" (`vk_swapchain_ps4.cpp`) is three linear images registered with VideoOut; each frame's
flip is queued behind its GPU work (`ac_ps4_submit_flip`).

The winsys describes the **base PS4 GPU** (2 shader engines, 18 compute units, 8 render backends,
base tiling). PS4 Pro mode gives the app a different GPU layout and hung the console, so the package
runs in base mode.

## HOME menu

`ui_launcher.cpp` depends only on `Ui::Canvas` and the `Ui::Host` interface, so it also builds on a PC
(`eden/ps4/ui-preview`). `ui_canvas.cpp` is a CPU renderer for a 1920x1080 canvas: anti-aliased rounded
rectangles and circles, tinted masks, 9-slice shadows and text from glyph atlases baked at build time
by `generate-ui-assets.py`. The canvas is copied to a VideoOut buffer once per frame.

`main.cpp`'s `PsHost` implements the host: games (sources, recent list), users (Eden's
`ProfileManager`), controllers, `ps4.ini` options and the icon cache. Before a game starts the host
stops its threads and frees its pictures; the loading screen is handed to the game's swapchain, which
shows it until the game's first frame.

Game icons and titles are written to `/data/EdenPS4/cache/ui` when a game starts (from the loader Eden
has then). Reading NSP/XCI files from the menu itself, with a second `Core::System`, ended the app in
a test build.

## Diagnostics

- `crash.log`: signal handler on a per-thread alternate stack (`ps4_crashlog_thread_init`); registers
  and the code addresses found on the stack. Symbolize with
  `llvm-symbolizer-21 --obj=<debug eden-ps4 ELF> <address>`.
- `hang.log`: a watchdog dumps every named thread's stack when the frontend loop stops for 15 s or no
  frame arrives for 20 s.
- `profile=on` in `ps4.ini` (System Settings → Advanced): a SIGPROF sampler reports where CPU time goes
  every 10 s.
- `xmb-trace.log`: heap and memory events from before logging starts.

## Settings files

- `/data/EdenPS4/ps4.ini`: options shown in System Settings, plus `user=<index>`.
- `/data/EdenPS4/ui-state.ini`: recently played games, user icons, custom sources, disabled sources.
