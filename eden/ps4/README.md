# eden/ps4

The PS4 build of Eden (Eden4). See the repository's [README](../../README.md),
[Building](../../docs/BUILDING.md) and [Architecture](../../docs/ARCHITECTURE.md).

| File | What |
|---|---|
| `configure.sh` | Configures Eden for the PS4 with the toolchain in `runtime/toolchain` |
| `build.sh` | Builds and packages `Eden4-<version>.pkg` (icon from `branding/`) |
| `generate-ui-assets.py` | Bakes the HOME menu's font (Nunito) and icons into `ps4_ui_assets.h` |
| `ui-preview/` | Builds the HOME menu on a PC and renders every screen to PNG |
| `collect-licenses.py` | Gathers the third-party licence texts for the package |
| `cpu-smoke.S`, `build-cpu-smoke.sh` | A tiny AArch64 program for the CPU self-test entries (only with `EDEN4_SELF_TEST=1`) |
| `diagnostics/`, `build-diagnostics.sh` | A standalone PS4 app that checks threads, the JIT, VideoOut and memory |
| `tests/` | Host-side tests of PS4-specific helpers |
