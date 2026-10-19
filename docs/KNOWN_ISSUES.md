# Known issues

Eden4 is experimental. These are the problems known at 1.0, and the ones that are likely but not
yet seen. Please report others with `eden.log`, `crash.log`, `hang.log` and `xmb-trace.log` from
`/data/EdenPS4` (HOME → About → Copy Logs to USB).

## Not yet confirmed on a console

The last builds before 1.0 were written and checked on a PC (sanitizers, the HOME menu preview) but
had not all been run on a PS4 when 1.0 was packaged:

- **HOME menu start-up.** Test builds 00.25-00.27 closed on the menu's first frame without a crash
  log. The cause found was the clock call into `libSceRtc`; the menu now reads the time through libc.
  If the menu still closes at start, `eden.log` lists each start-up step ("first update", "first
  render", "first present").
- GPU-queued frame flips (smoother frames), multiple controllers, the loading screen and game sources.
- USB drives are expected at `/mnt/usb0`-`/mnt/usb7`. If a drive's games don't appear, add its folder
  by hand under Game Files → Add Folder.

## Performance

- Most games run **below full speed**
  The Jaguar CPU is much slower than the PCs Eden targets; 60 fps in 3D games is not expected.
- **Start-up takes 20-35 s** (JIT compilation of the game's code at boot).
- **Shader stutter**: the disk shader cache is off, so new effects compile while playing, every time.
- Every guest memory access goes through a software page table (no fastmem on the PS4).

## Memory

- Only about **480 MiB of GPU memory** remains after the Switch's 4 GiB. Large games can run out:
  `eden.log` then shows "PS4 memory reclaim" (short stalls) or `VK_ERROR_OUT_OF_DEVICE_MEMORY`
  (a crash).
- 1920x1080 output uses about 13 MiB more GPU memory than 1280x720.
- When RADV itself cannot allocate a command buffer, Mesa's error path writes out of bounds
  (`radv_amdgpu_cs_grow`) instead of failing cleanly - a crash rather than an error message.

## Hardware and system

- **PS4 Pro mode is not supported.** With it, the GPU hung and the whole console crashed: the RADV
  port describes the base PS4 GPU. The package runs in base mode; System → Boost Mode on a PS4 Pro
  still raises the clocks.
- The clock reads libc's local time, which may show UTC if the system's time zone is not available.

## Games and emulation

- Only two games have been tried (see above); expect crashes and graphical errors in others.
- `eden.log` fills with assert lines such as `handle.first <= tic_limit` and
  `texture format=0`; they have not caused visible problems so far.
- No network play, no online services (LDN, sockets are stubbed).
- Updates and DLC are not installed into the emulated NAND from the HOME menu.

## Controllers

- No rumble, motion (gyro) or touchscreen input yet; the touchpad click is the Switch's − button.
- The Handheld controller type is not offered.
- No in-game HOME menu: L3 + R3 + Options is the only way back, and it ends the game.

## HOME menu

- A game's icon and title appear only after its first start (reading game files in the menu itself
  crashed the app in a test build, so the icon is saved when Eden loads the game).
- Text uses a Latin font: Japanese, Chinese and Korean titles show "?" characters.
- On-screen keyboard: Latin letters, digits and a few symbols only.
- The data folder is still called `/data/EdenPS4`.
