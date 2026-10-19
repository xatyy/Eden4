# Dynarmic test harness

Runs the real Dynarmic (from `eden/src/dynarmic`, with the PS4 changes compiled in under
`DYNARMIC_PAGE_REUSE_TEST`) as an x86-64 Linux program and executes AArch64 test programs through it,
with the same page table layout as the PS4 build. It found the register allocation bug behind the
Clubhouse Games hang (test builds 00.09-00.11).

- `build_dynarmic.sh`, `build_harness.sh`: build the library and the harness (x86-64 Linux, clang).
- `guest/`: AArch64 test programs - a zstd decoder (`main.c`, `ztool.c`, plus forced-Huffman variants)
  and a torture test (`torture.c`); `build_guest.sh` / `build_torture.sh` build them with clang.
- `run_all.sh`, `run_matrix.sh`, `run_variants.sh`, `run_torture*.sh`: compare the JIT's results with
  the expected ones across options.
- `bisect*.sh`, `tickbisect.sh`, `blockscan.sh`: find the first block or tick where results diverge.

The zstd tests need input files: compress any data you like with `zstd` into `data/a.zst` and
`data/b.zst` (their xxh64 is printed for comparison between runs). No test data is included.

Run the harness after every JIT change.
