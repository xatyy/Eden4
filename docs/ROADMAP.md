# Roadmap

Ideas for after 1.0, roughly in order of value. Contributions are welcome.

## Stability first

- Confirm the 1.0 HOME menu, GPU-queued flips, multiple controllers, sources and the loading screen on
  consoles with different firmware.
- Make Mesa's command buffer allocation fail cleanly (`radv_amdgpu_cs_grow`) and let Eden reclaim
  memory and retry.
- A game compatibility list with the emulation switches each game needs.

## Speed

- Profile real games with the sampling profiler and keep tuning the JIT for the Jaguar CPU: fewer
  memory callbacks, block linking, cheaper page table walks.
- A disk shader and pipeline cache, so effects compile once instead of every session.
- Asynchronous shader compilation on the spare CPU cores.
- **PS4 Pro mode**: read the Pro GPU's real layout (shader engines, render backends, tiling registers)
  instead of assuming the base PS4, for its faster GPU and 512 MiB more memory.
- Compile Eden with link-time optimisation once the toolchain allows it.

## Features

- An in-game menu (touchpad or PS button): back to HOME without ending the game, save states,
  per-game settings, an fps overlay.
- Rumble and motion controls from the DualShock 4; touchpad as the touchscreen.
- Per-game settings (console mode, resolution, controller type).
- Install updates and DLC (NSP) into the emulated NAND; manage save data and back it up to USB.
- Firmware and key import from USB inside the app.
- A font with Japanese, Chinese and Korean glyphs; a translatable menu.
- Network and local play.
- Rename the data folder to `/data/Eden4`, moving existing data automatically.

## Building

- A Dockerfile for the build environment and a CI build that publishes packages.
- An update check in About that downloads new releases.
