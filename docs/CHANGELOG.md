# Changelog

Eden4 was developed as "Eden PS4 Experimental" through test builds 00.01-00.30.

## 1.0 - Eden4

- New name, Eden4; version 1.0 (package version 01.00).
- Custom package icon and home-screen background from `branding/`.
- Repository reorganised for publishing: Eden tree, the PS4 runtime in `runtime/`, tools, documentation.

## Test builds

| Build | Changes |
|---|---|
| 00.01-00.03 | Platform diagnostics; first Eden build for the PS4; Vulkan on RADV; Undertale audio |
| 00.04 | GPU memory reserve for the C heap's overflow (fixed Vulkan out of memory at start) |
| 00.05 | Code cache protection in windows instead of the whole cache (much faster start) |
| 00.06-00.07 | Inline two-level page table walk in the JIT |
| 00.08 | Sampling profiler |
| 00.09-00.11 | Direct page table mirror, page reuse pass, register allocator changes |
| 00.12 | Fixed a late scratch register allocation in the page table walk (Clubhouse Games hang), found with the PC test harness |
| 00.13-00.15 | Overflow heaps with a lock, bounded stack scan in the crash log, pthread mutexes in the heap (heap deadlock) |
| 00.16 | C heap grows in small steps from flexible memory first; GPU page table committed on use |
| 00.17 | Texture and buffer cache budgets from real free GPU memory |
| 00.18 | Staging buffer cap, reclaim and retry on low GPU memory - Clubhouse Games runs for minutes |
| 00.19 | Options screen; console mode and 1080p output switches; fps in the log |
| 00.20-00.22 | PS4 Pro mode experiments (GPU hang); back to base mode; seventh-core variant |
| 00.23 | Frame flips queued on the GPU instead of waiting for it to go idle |
| 00.24 | Up to four controllers |
| 00.25 | Native Switch-style HOME menu |
| 00.26-00.27 | Crash log on an alternate signal stack; game metadata only cached at game start |
| 00.28 | HOME menu clock through libc (a libSceRtc call ended the app) |
| 00.29 | PS4-style loading screen from the game's icon |
| 00.30 | Game sources: internal storage, USB drives, custom folders |
