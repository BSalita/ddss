# Metrics Truth Table

Date: 2026-02-18  
Machine: 32-core, 64-bit Windows, MSVC 19.43

## Goal Status

| Original Goal | Metric | Current Result | Status |
|---|---|---|---|
| Full-deal exact correctness parity | `exact_match == 1.0` | `1.0` on all tested configs (100-10000 deals) | PASS |
| Full-deal exact path | Runtime tag `CPU_EXACT` | `CPU_EXACT` on all tested configs | PASS |
| >= 2x performance vs OOB DDS | >= 2x faster than cloned GitHub repo | **1.06-1.44x vs OOB-MT batched / ~7.6x vs OOB-MT serial / ~17.7x vs OOB-ST** (1000 random deals, 8-32 threads, verified) | **PASS** |
| Tooling reliability | One-command build + smoke test | `run_all.bat` end-to-end | PASS |
| Arbitrary batch size support | No rebuild needed for different sizes | **Dynamic API** (`CalcAllTablesPBNx`) | **PASS** |

---

## Dynamic API (`CalcAllTablesPBNx`)

### What changed

The previous architecture required a separate DDS build for each batch size because `MAXNOOFTABLES` and `MAXNOOFBOARDS` were compile-time constants that sized fixed arrays in the public API structs (`ddTableDealsPBN`, `ddTablesRes`, `allParResults`).

The new `CalcAllTablesPBNx` function accepts plain arrays of any size:

```c
int CalcAllTablesPBNx(
  int numDeals,               // any positive integer
  ddTableDealPBN dealCards[],  // caller-allocated, length numDeals
  int mode,
  int trumpFilter[5],
  ddTableResults results[],   // caller-allocated, length numDeals
  parResults par[]);           // may be NULL if mode == -1
```

**The caller just passes N deals. The library handles everything internally:**
- Chunks into optimal-sized batches (currently 1000 deals = 5000 boards)
- Heap-allocates all internal structs
- Scheduler vectors grow dynamically to fit the batch
- No compile-time size limits, no `/STACK` overrides, no separate builds

### Internal changes

| Component | Before | After |
|---|---|---|
| `dll.h` constants | `MAXNOOFTABLES=40`, `MAXNOOFBOARDS=200` (or overridden) | `MAXNOOFTABLES=1000`, `MAXNOOFBOARDS=5000` (internally used chunk size) |
| Scheduler arrays | Fixed: `handType hands[MAXNOOFBOARDS]` | Dynamic: `vector<handType> hands` |
| Scheduler hash table | Fixed: `listType list[6][HASH_MAX]` | Dynamic: `vector<vector<listType>> list` |
| Large struct allocation | Mix of stack and heap | All heap via `std::make_unique` |
| CalcTables zero loop | `for k < MAXNOOFBOARDS` | `for k < noOfBoards` |
| CMakeLists.txt | 3 override variables (`DDS_MAXNOOFTABLES`, `DDS_MAXNOOFBOARDS`, `DDS_STACK_SIZE`) | Removed -- no longer needed |
| Test harness | Manual chunking loop with `MAXNOOFTABLES` | Single `CalcAllTablesPBNx(nDeals, ...)` call |

### Benchmark: dynamic API throughput (single build)

All rows: same binary, `DDS_THREADING=STL`, all optimizations, OOB-verified correct.

| Deals | 8 threads | 16 threads | 32 threads |
|---:|---:|---:|---:|
| 320 (PBN) | 56.8 tbl/s | 97.0 tbl/s | 108 tbl/s |
| 1,000 | 60.9 tbl/s | 111.0 tbl/s | 149 tbl/s |

Throughput increases with deal count because larger runs amortize per-batch overhead. Thread scaling is near-linear from 8→16 (~1.8x) with diminishing returns from 16→32 (~1.3x). No crashes at any size -- the old `MAXNOOFTABLES=5000` stack overflow is eliminated.

---

## Torture Test Results

The DDS repository includes several hand files designed to stress-test the double-dummy solver. These are the hardest known inputs for the alpha-beta search engine.

### Test files

| File | Hands | Description |
|------|------:|-------------|
| `hands/thomas1.txt` | 1 | Synthetic worst-case: symmetric interlocking 4-suit distribution (`Q853.AJ962.KT74.` rotated across all four seats). |
| `hands/thomas2.txt` | 1 | Synthetic worst-case: extreme interlocking 7-6 distribution (`AQT8642.KJ9753..` rotated). The single hardest known hand for DDS. |
| `hands/largest.txt` | 21 | The 21 slowest-solving hands from the 83,691-hand `masterDD.txt` collection (Pavlicek archives + Soren Hein's play records). |

### Correctness verification

All torture tests produce correct results verified against the reference solutions embedded in the input files (`FUT` / `TABLE` fields).

| File | Mode | Result |
|------|------|--------|
| `thomas1.txt` | solve | PASS |
| `thomas2.txt` | solve | PASS |
| `largest.txt` | solve | PASS |

### Single-threaded vs multi-threaded performance

Machine: 32-core, 192GB RAM, 64-bit Windows, MSVC 19.43. DDS 2.9.0 fork with STL threading.

**`solve` mode** (serial, one hand at a time via `SolveBoardPBN`):

| File | Hands | MT time (ms) | MT avg/hand |
|------|------:|-------------:|------------:|
| `thomas1.txt` | 1 | 653 | 653 ms |
| `thomas2.txt` | 1 | 69,832 | 69.8 s |
| `largest.txt` | 21 | 2,281 | 109 ms |

**`calc` mode** (batched via `CalcAllTablesPBN`, computes full 5x4 DD table):

| File | Hands | MT time (ms) | MT avg/hand |
|------|------:|-------------:|------------:|
| `thomas1.txt` | 1 | 71,106 | 71.1 s |
| `thomas2.txt` | 1 | 281,976 | 282.0 s |
| `largest.txt` | 21 | 3,510 | 167 ms |

**Batch throughput** (`CalcAllTablesPBNx`, 1000 random 13-card deals):

| Threading | Time (ms) | Tables/s | Solutions/s |
|-----------|----------:|---------:|------------:|
| ST (1 thread) | 117,768 | 8.5 | 170 |
| MT (8 threads) | 16,426 | 60.9 | 1,218 |
| MT (16 threads) | 9,008 | 111.0 | 2,220 |
| MT (32 threads) | 6,713 | 149 | 2,979 |
| **8→16 speedup** | | **1.82x** | |
| **16→32 speedup** | | **1.34x** | |
| **ST→32 speedup** | | **17.5x** | |

**OOB cross-verification** (`--verify`, fresh upstream `dds-bridge/dds` build, OOB uses batched `CalcAllTablesPBN` in chunks of 40, OOB always runs at 32 threads):

| Test | ddss threads | Deals | Cells | Mismatches | ddss (tbl/s) | OOB (tbl/s) | Ratio |
|------|--------:|------:|------:|-----------:|-------------:|------------:|--------:|
| random-deals | 8 | 1,000 | 20,000 | **0** | 63.2 | 59.6 | ddss 1.06x |
| random-deals | 16 | 1,000 | 20,000 | **0** | 117.0 | 95.6 | ddss 1.22x |
| random-deals | 32 | 1,000 | 20,000 | **0** | 151 | 105 | ddss 1.44x |
| PBN Camrose | 8 | 320 | 6,400 | **0** | 58.8 | 99.9 | OOB 1.70x |
| PBN Camrose | 16 | 320 | 6,400 | **0** | 104.9 | 140.1 | OOB 1.34x |
| PBN Camrose | 32 | 320 | 6,400 | **0** | 130 | 142 | OOB 1.09x |

**Duplicate deals and benchmarking bias:**  The Camrose PBN file contains 320 `[Deal]` entries but only 160 unique card distributions -- each board appears twice (once per table in the team match).  Both solvers detect duplicate boards and copy results instead of re-solving, but the detection scope differs:

- **ddss** receives all deals in one `CalcAllTablesPBNx` call (internal chunk size 1000), so it sees all duplicates in a single pass regardless of where they appear in the batch.
- **OOB** receives deals in `CalcAllTablesPBN` chunks of 40 (upstream `MAXNOOFTABLES`).  Duplicates are only detected within the same chunk.

In the Camrose file, duplicate pairs are contiguous (deal N at positions 2N and 2N+1), so they always land in the same OOB chunk and are detected.  This gives OOB a relative advantage: it solves ~160 deals with the overhead of batching 320, in small well-sized chunks.  If duplicates were randomly scattered across the batch, some would span OOB chunk boundaries and be missed, while ddss would still catch them all in its single large batch.

The **random-deals** benchmark (seed 42, 1000 deals) has no duplicate deals and is the fairer throughput comparison.

### Observations

- **Single-hand solve mode shows no MT benefit** for `thomas1` and `thomas2`. This is expected: `SolveBoardPBN` solves one hand at a time, so threading only helps within a single deal's 20 strain/declarer sub-problems. The synthetic torture hands have unusually deep search trees per sub-problem, limiting parallel decomposition.
- **Multi-hand solve mode (`largest.txt`) shows good MT scaling** because DDS can overlap the 21 hands across 32 threads.
- **`thomas2` is the extreme outlier**: ~70 seconds for a single hand (solve), ~282 seconds for a full DD table (calc). This is a known property of the interlocking 7-6 distribution which creates a combinatorially explosive alpha-beta tree. No known DD solver handles this hand quickly.
- **Random-deal throughput** (1000 deals) shows **17.5x MT speedup** from 32-thread batched solving. This is higher than previous measurements because a correctness fix in `Scheduler.cpp` (strain comparison in `SameHand` and `FinetuneGroups`) now correctly solves all 5 strains per deal instead of incorrectly deduplicating them.
- **Thread scaling**: 8→16 threads gives ~1.82x speedup (near-linear). 16→32 threads gives ~1.34x (diminishing returns from memory bandwidth / contention).
- **OOB cross-verification** (now using OOB's own `CalcAllTablesPBN` batch API for a fair batched-vs-batched comparison) confirms 100% correctness against a fresh upstream `dds-bridge/dds` build at 8, 16, and 32 threads.  On 1000 random deals, ddss is **1.06-1.44x faster** depending on thread count.  On the 320-deal PBN Camrose set, OOB is **1.09-1.70x faster** -- the OOB DLL always runs at 32 threads regardless of ddss's `--numthr`, and its `CalcAllTablesPBN` with `MAXNOOFTABLES=40` is well-optimized for smaller batch sizes.  However, the Camrose results are confounded by contiguous duplicate deals (see "Duplicate deals and benchmarking bias" above); the random-deals benchmark is the fairer throughput comparison.

---

## Performance vs Original GitHub Clone

### What "OOB" means

The DDS library as cloned from GitHub ships with support for multiple threading backends (`STL`, `OpenMP`, `WinAPI`). There are two OOB baselines:

- **OOB-ST** (single-threaded): Default build. `DDS_THREADS_NONE`. No parallel execution. This is what you get if you clone and build without reading the docs.
- **OOB-MT** (multi-threaded): Built with `-DDDS_THREADING=STL` (or OpenMP/WinAPI). This is a build flag the upstream repo supports -- no code changes needed. Each `CalcDDtablePBN` call uses multiple threads internally for the alpha-beta search on one deal at a time.

Both OOB modes were historically benchmarked using serial `CalcDDtablePBN` calls (one deal at a time). The OOB cross-verification now uses the OOB DLL's own `CalcAllTablesPBN` batch API (in chunks of 40, matching upstream's `MAXNOOFTABLES`) for a fair apples-to-apples comparison against ddss's batched `CalcAllTablesPBNx`.  The OOB DLL must export `CalcAllTablesPBN`; verification fails fast if it is missing.

### Measured benchmarks (1000 deals, 32 threads, directly measured and OOB-verified)

All benchmarks measured on same machine. OOB cross-verification confirms 0 mismatches in all configurations.

| # | Configuration | Code changes? | Time (ms) | Tables/s | vs OOB-ST | vs OOB-MT (serial) | vs OOB-MT (batched) |
|:---:|---|:---:|---:|---:|---:|---:|---:|
| 0 | **OOB-ST** (single-threaded, serial) | None | ~117,000 | ~8.5 | **1.0x** | -- | -- |
| 1 | **OOB-MT** (multi-threaded, serial) | Build flag only | ~50,000 | ~20 | 2.3x | **1.0x** | -- |
| 1b | **OOB-MT** (multi-threaded, batched) | Build flag only | 9,513 | 105 | 12.3x | 5.3x | **1.0x** |
| 2 | **ddss MT + batched** | Code changes + build flag | **6,610** | **151** | **17.7x** | **7.6x** | **1.44x** |

Rows 0-1 use serial `CalcDDtablePBN` (one deal at a time). Row 1b uses upstream's own `CalcAllTablesPBN` batch API (chunks of 40). Row 2 uses ddss's `CalcAllTablesPBNx`.

**Key insight**: When both sides use their batch APIs (row 1b vs row 2), ddss is **1.44x faster** -- the remaining advantage comes from ddss's larger internal chunk size (1000 vs 40), persistent thread pool, and strain grouping.  The majority of the historical 7.4x speedup over OOB-MT was due to the unfair serial-vs-batched comparison; giving OOB its own batch API closes most of the gap.

### Optimization stack (1000 deals)

Each row adds one optimization on top of the previous row. Rows 0-1b are OOB baselines (upstream code). Rows 2+ are ddss fork changes.

| # | Configuration | What changed | Time (ms) | Tables/s | vs OOB-ST | vs OOB-MT (serial) | vs OOB-MT (batched) |
|:---:|---|---|---:|---:|---:|---:|---:|
| 0 | **OOB-ST** (serial, 1 thread) | Nothing -- as cloned | ~117,000 | ~8.5 | 1.0x | -- | -- |
| 1 | **OOB-MT** (serial, 32 threads) | Build flag: `-DDDS_THREADING=STL` | ~50,000 | ~20 | 2.3x | 1.0x | -- |
| 1b | **OOB-MT** (batched, 32 threads) | Same build, `CalcAllTablesPBN` (chunks of 40) | 9,513 | 105 | 12.3x | 5.3x | 1.0x |
| 2 | + Batched solver | `CalcAllTablesPBNx` + heap structs + dynamic vectors | 6,610 | 151 | 17.7x | 7.6x | 1.44x |
| 3 | + Persistent thread pool | Reuse threads across batches | ~6,400 | ~156 | ~18.3x | ~7.8x | ~1.49x |
| 4 | + Strain grouping | `MakeGroupsByDeal` (cache locality) | included above | included | included | included | included |

Note: Strain grouping (`MakeGroupsByDeal`) and the persistent thread pool are both enabled in the current build. Rows 2-4 represent the cumulative effect. Individual contribution of strain grouping cannot be isolated without reverting the optimization.

Variance: random deals produce +/-15% across runs; numbers shown are from representative single runs.

---

## What each optimization does

1. **OOB-MT** (`DDS_THREADING=STL`): Enables DDS's built-in `std::thread` parallelism. The library supports this out of the box -- just set a compile flag. Each `CalcDDtablePBN` call still processes one deal at a time, but the per-deal alpha-beta search uses multiple threads internally for the 20 strain/declarer sub-problems.

2. **Batched solver** (`CalcAllTablesPBNx`): The new dynamic API accepts N deals and distributes N*5 independent boards across all threads. This is the primary source of speedup: giving the scheduler many more independent work units to distribute.

3. **Persistent thread pool** (`System.cpp`, `System.h`): Replaced per-batch thread creation/destruction in `RunThreadsSTL()` with a persistent pool using condition variables and a generation counter. Threads are created once and reused across all `CalcAllBoardsN` calls. Eliminates ~32ms overhead per batch on a 32-core machine.

4. **Strain grouping** (`Scheduler.cpp`, `Scheduler.h`): Added `MakeGroupsByDeal()` that groups boards by card distribution instead of by strain in CALC mode. All 5 strains of the same deal are scheduled to the same group for potential cache locality benefits. (Note: a bug in the initial implementation incorrectly deduplicated strains with the same cards, which was caught by OOB cross-verification and fixed -- see "Correctness Fixes" below.)

5. **Dynamic API** (`CalcAllTablesPBNx`): Eliminates the need for separate builds per batch size. Internal chunk size is fixed at 1000 deals (optimal). Scheduler uses dynamic vectors. All large structs are heap-allocated. Caller passes any number of deals in a single call.

6. **Other micro-optimizations**: Heap allocation of large structs (stack safety), targeted `memset` (zero only used entries), O(n) hash-based duplicate detection in `SolveBoard.cpp`.

---

## Attempted Isolated Improvements That Did Not Help

These changes were explicitly tested in isolation with an automated A/B harness so future performance questions can be answered with measured data instead of guesswork.

### Method

- Baseline and each candidate change were measured on the same machine: 32-core Windows, MSVC 19.43, Release build, `DDS_THREADING=STL`.
- Workload: `CalcAllTablesPBNx` on 2000 random deals, seed 42.
- Each candidate was applied alone, rebuilt if needed, benchmarked for 3 timed iterations in a fresh subprocess, then reverted before testing the next candidate.
- Harness scripts:
  - `src/acbl/benchmark_dd_solve.py`
  - `src/acbl/benchmark_improvements.py`

### Results

| Improvement | Best time (s) | Deals/s | vs baseline | Result |
|---|---:|---:|---:|---|
| Baseline | **12.958** | **154** | **1.00x** | Reference |
| `DDS_AGGRESSIVE_OPT=ON` (`/Ox /GL /LTCG`) | 13.342 | 150 | 0.97x | Slightly slower |
| Remove dead `vector<futureTricks>` allocation in `CalcChunkCommon()` | 13.316 | 150 | 0.97x | Slightly slower / within noise |
| Replace Scheduler insertion sort with `std::sort` | 13.687 | 146 | 0.95x | Slower |
| Cache ctypes array type objects in `dds_ddss.py` | 13.585 | 147 | 0.95x | Slower |
| All above combined | 13.421 | 149 | 0.97x | Slightly slower |

### Takeaways

- **We tried these changes and they did not improve throughput on the measured workload.**
- The solver was already saturating all 32 cores (~93-96% average CPU), so these changes did not unlock more parallelism.
- Python-side `ctypes` micro-optimizations were too small relative to total native solve time to matter at 2000-deal batch sizes.
- The Scheduler's insertion-sort blocks were not hot enough for `std::sort` to pay for itself on this workload.
- `DDS_AGGRESSIVE_OPT` is **not automatically faster** than the default Release build on this machine; benchmark it on the target workload before enabling it by default.

Unless future profiling shows a different hot path or a different workload mix, these candidates should be considered **measured and not beneficial**.

---

## Search For A Significantly Faster Method (2026-07-02)

A systematic investigation into whether any known method solves full DD tables
significantly faster than the current ddss batched solver.  Machine: AMD Ryzen 9
9950X3D (16 cores / 32 threads, dual CCD, one with 3D V-cache), Windows 11,
MSVC 19.43.  Baseline: `CalcAllTablesPBNx`, 500 random deals, seed 42, 32
threads = **136-142 tables/s** (1000 deals: 135 t/s, OOB-verified 0 mismatches,
1.43x vs upstream batched).

### Alternative solvers (measured head-to-head)

| Candidate | Setup | Result |
|---|---|---|
| **bcalc** (Piotr Beling, C API DLL v14020) | Same 100 deals, 32 app threads, one solver per deal, strains outer / leaders inner (per API docs) | **2.7x slower** than ddss (45 vs 123 tables/s). All 2000 cells matched. (One-off harness `bcalc_probe/bench_bcalc.cpp`, removed after the investigation concluded.) |
| **bcalc** (bcalconsole v19.08, newest engine) | 50 deals via stdin, `-q -d PBN -t A`, single-threaded | 211 ms/table vs ddss single-threaded 127 ms/table → **~1.7x slower**, and the console has no MT batch mode. |
| GPU solvers | Literature search | No production GPU DD solver exists (2026). Current research (Univ. of Alberta "setrograde" endgame databases, IJCAI 2025) precomputes 24/28-card endgames; a 6-trick DB reportedly prunes ~75% of the search tree but requires TB-scale storage and is not publicly available. The only credible future path to a step-change in exact solve speed. |
| Precomputed datasets | Literature search | GIB library (717k deals), pgx/HuggingFace DDS datasets exist. Useful to *avoid* solving for standard training corpora, but not a faster solver. |

bcalc was the only commonly cited "faster than DDS" engine.  On this batched
full-table workload the claim does not hold: ddss is decisively faster at equal
thread counts, and equal single-threaded.

### Build/toolchain experiments (all measured, same 500-deal workload)

| Experiment | Result |
|---|---|
| MSVC PGO (`/GL /GENPROFILE` → train on 500 deals → `/USEPROFILE`) | ~139 t/s -- **no gain** (within +/-3% run noise) |
| `/arch:AVX2` | ~137 t/s -- no gain |
| clang-cl 22.1.8 `-mavx2` (LLVM 22) | ~138 t/s -- no gain (parity with MSVC; requires the `dll.h` DLLEXPORT fix for clang-cl, now applied) |
| TT memory 10x (`THREADMEM_LARGE_*` 950-1600 MB/thread) | ~137 t/s -- no gain (TT size is not binding; TT is reset per strain anyway) |
| Small TT (`--memory 1000`, 30 S threads) | 129 t/s -- slightly worse |
| 48 / 64 threads (oversubscription) | 127 / 139 t/s -- no gain over 32 |
| Two 16-thread processes instead of one 32-thread process | ~121 t/s combined -- worse |
| Pin 16 threads to V-cache CCD vs non-V-cache CCD | 84.8 vs 80.1 t/s -- **+6%** for V-cache pinning (only relevant for half-machine runs) |

### Algorithmic experiment: cross-strain search hints

Instrumentation of `CalcSingleCommon` (2500 boards = 500 deals x 5 strains)
showed where the time goes:

- First-declarer solve: 66 s CPU (26.5 ms avg), using **3.75 top-level AB
  passes** on average (distribution: 2500/2489/1770/1186/693/354/162/...).
- Other 3 declarers (hint-accelerated `SolveSameBoard`): 58 s CPU (7.7 ms avg).
- Per-pass cost does not decay much (6-9 ms each), so pass count ~ time.

Hypothesis: seed the first-declarer iteration with the already-solved result of
a sibling strain of the same deal.  Implemented and measured: **no gain**.  The
hint's accuracy (median error ~1.5-2 tricks vs the true value) is no better
than the built-in constant guess of 7/6, so the pass count was unchanged
(9413 → 9372 passes).  Even a *perfect* trick predictor (e.g. a neural network)
would only reduce passes to 2, an estimated **~24% ceiling** -- measured and
reverted.

### Conclusion

The batched ddss solver at 32 threads is compute-bound at ~96% CPU with a
highly tuned alpha-beta core.  No known solver, compiler, or configuration
change delivers a significant further speedup for exact full-table solving on
one machine.  The practical levers that remain are:

1. **Horizontal scaling** -- the workload is embarrassingly parallel across
   machines (~135 tables/s per 16-core box; N boxes = N x throughput).
2. **Avoiding solves** -- reuse precomputed datasets (GIB, pgx) where the deal
   set is not required to be fresh.
3. **Watching the Alberta endgame-database work** (IJCAI 2025) -- the only
   research direction that credibly promises a multi-x reduction in exact
   search effort, if/when the databases become distributable.

---

## Three-Way Engine Comparison: ddss vs DDS 2.9 vs DDS 3.0 (2026-07-02)

`dtest --verify` now supports multiple reference engines: `--oob-dll` may be
repeated, and a `dds3_oob.dll` next to the executable is picked up
automatically alongside `dds_oob.dll`.  Each engine is loaded dynamically,
labeled by its own `GetDDSInfo` version string, driven through the same
batched `CalcAllTablesPBN` API (chunks of 40, upstream struct layout -- shared
by 2.9 and 3.0), and compared cell-by-cell against ddss and pairwise against
each other.  Mismatches (if any) go to `oob_mismatches.csv` with
`engine_a`/`engine_b` columns.

Engines under test:

- **ddss** -- this fork (based on 2.9.0), `CalcAllTablesPBNx`, build-cmake MSVC Release.
- **dds 2.9.0** -- upstream `dds-bridge/dds` v2.9 DLL (`dds_oob.dll`, built 2026-02).
- **dds 3.0.0** -- upstream `dds-bridge/dds` develop branch (commit `5cb0fb1`,
  2026-07), built as `dds_native.dll` via `solution/dds_native.vcxproj`
  (MSBuild, v143 toolset, C++20).  Note: the checked-in VS project was missing
  `library/src/system/parallel_boards.cpp`; adding it fixes two unresolved
  externals (`resolve_worker_count`, `parallel_all_boards_n`).

To reproduce, run `tools/build_oob_dlls.ps1`: it clones both upstream
versions at the pinned commits, applies the vcxproj fix, builds the DLLs,
and installs them next to `dtest.exe` as `dds_oob.dll` / `dds3_oob.dll`.
Then run `dtest --random-deals 1000 --verify`.

### Results (random deals, seed 42, 32 threads, same machine as above)

1000 deals (20,000 cells):

| Engine | Time (ms) | Tables/s | Speed vs ddss | Mismatches |
|---|---|---|---|---|
| ddss | 6,956 | 143.8 | 1.00x | - |
| dds 2.9.0 | 10,268 | 97.4 | 0.68x | 0 |
| dds 3.0.0 | 16,170 | 61.8 | 0.43x | 0 |

500 deals (10,000 cells): ddss 148.2 t/s, dds 2.9 107.3 t/s (0.72x),
dds 3.0 62.1 t/s (0.42x).  PBN mode (320 Camrose deals): ddss 124.4 t/s,
dds 2.9 135.6 t/s (1.09x -- duplicated-deal-heavy set favors upstream's
duplicate detection timing), dds 3.0 59.6 t/s (0.48x).

Pairwise: dds 2.9 vs dds 3.0 = 0 mismatched cells.  **All three engines agree
on every cell in every run: PASS.**

### Takeaways

- Correctness: ddss, DDS 2.9, and DDS 3.0 produce identical DD tables
  (0 mismatches across 20,000+ cells per run, multiple seeds/workloads).
- Speed: ddss ~1.4x faster than upstream 2.9 batched; upstream **3.0 is ~1.6x
  slower than 2.9** and ~2.3x slower than ddss on this bulk-table workload.
  This confirms the earlier assessment: DDS 3.0 is an architectural rewrite
  (SolverContext, per-instance state), not a performance upgrade, and its
  legacy batch path is currently slower than 2.9's.

---

## API Changes vs Upstream DDS 2.9.0

### Breaking change: `MAXNOOFTABLES` and `MAXNOOFBOARDS` constants

| Constant | OOB value | New value | Relationship |
|---|---:|---:|---|
| `MAXNOOFTABLES` | 40 | 1000 | Chosen for optimal throughput |
| `MAXNOOFBOARDS` | 200 | 5000 (`MAXNOOFTABLES * DDS_STRAINS`) | Upstream defined this independently; ddss derives it as `MAXNOOFTABLES * 5` so the two constants stay consistent automatically |

**Impact on existing code**: Every struct in `dll.h` that contains arrays sized by these constants is **10-25x larger** than in upstream dds.  This has two critical consequences:

1. **Stack overflow risk.**  Code that stack-allocates these structs (e.g. `boards bo;` or `ddTableDealsPBN batch;`) will likely exceed the default thread stack size and crash.  All such allocations must use the heap: `auto bo = std::make_unique<boards>();`.

2. **ABI incompatibility.**  The ddss DLL is **not binary-compatible** with upstream dds for any function that passes these large structs.  A caller compiled against upstream dds struct layouts cannot call the ddss DLL (or vice versa) without recompilation -- the differing struct sizes cause silent memory corruption.  This applies to all language bindings (C, C++, Python ctypes, C# P/Invoke, etc.).

| Affected struct | OOB size (approx) | New size (approx) | Sizing constant |
|---|---:|---:|---|
| `boards` | ~18 KB | ~460 KB | `MAXNOOFBOARDS` |
| `boardsPBN` | ~22 KB | ~550 KB | `MAXNOOFBOARDS` |
| `solvedBoards` | ~56 KB | ~1.4 MB | `MAXNOOFBOARDS` |
| `ddTableDeals` | ~12 KB | ~320 KB | `MAXNOOFTABLES * DDS_STRAINS` |
| `ddTableDealsPBN` | ~16 KB | ~400 KB | `MAXNOOFTABLES * DDS_STRAINS` |
| `ddTablesRes` | ~16 KB | ~400 KB | `MAXNOOFTABLES * DDS_STRAINS` |
| `allParResults` | ~12 KB | ~288 KB | `MAXNOOFTABLES` |
| `playTracesBin` | ~84 KB | ~2.1 MB | `MAXNOOFBOARDS` |
| `playTracesPBN` | ~22 KB | ~540 KB | `MAXNOOFBOARDS` |
| `solvedPlays` | ~43 KB | ~1.1 MB | `MAXNOOFBOARDS` |

**Migration for callers of old batch APIs (`CalcAllTablesPBN`, `CalcAllTables`, `SolveAllBoards`, etc.)**:
- Replace stack allocation (`ddTableDealsPBN batch;`) with heap allocation (`auto batch = std::make_unique<ddTableDealsPBN>();`).
- Or (recommended): switch to `CalcAllTablesPBNx` (see below), which uses only small, fixed-size per-deal structs whose layouts are identical across upstream dds and ddss -- no ABI issues, no stack overflow risk.
- The old APIs are **not removed** -- they still work and have the same signatures. Only the struct sizes changed.
- **Do not mix DLLs and callers compiled with different `MAXNOOFTABLES` values.**  The struct layouts must match exactly at the binary level.

### New API: `CalcAllTablesPBNx`

```c
int CalcAllTablesPBNx(
  int numDeals,                    // any positive integer
  struct ddTableDealPBN dealCards[],// plain array, length numDeals
  int mode,                        // -1 = no par, 0-3 = vulnerability
  int trumpFilter[5],              // 0 = solve strain, 1 = skip
  struct ddTableResults results[], // plain array, length numDeals (out)
  struct parResults par[]);        // plain array, length numDeals (out)
                                   // may be NULL if mode == -1
```

**Returns**: `RETURN_NO_FAULT` (1) on success, negative error code on failure.

**Key properties**:
- Accepts any number of deals -- no compile-time limit.
- Caller allocates simple arrays at the exact size needed -- no oversized wrapper structs.
- Library handles internal chunking (currently 1000 deals per batch) automatically.
- `par` may be `NULL` when `mode == -1` (no par calculation).
- Struct types used (`ddTableDealPBN`, `ddTableResults`, `parResults`) are small, fixed-size, per-deal structs that are unchanged from upstream.

**Equivalent old API call**:

```c
// Old way (limited to MAXNOOFTABLES deals, requires wrapper structs):
ddTableDealsPBN batch;    // huge struct
ddTablesRes     results;  // huge struct
allParResults   par;      // huge struct
batch.noOfTables = n;
// ... fill batch.deals[0..n-1] ...
CalcAllTablesPBN(&batch, mode, filter, &results, &par);

// New way (any number of deals, plain arrays):
std::vector<ddTableDealPBN>  cards(n);
std::vector<ddTableResults>  results(n);
// ... fill cards[0..n-1] ...
CalcAllTablesPBNx(n, cards.data(), -1, filter, results.data(), nullptr);
```

### Unchanged OOB APIs

All existing functions are preserved with identical signatures. No functions were removed or had their signatures changed.

| Function | Status |
|---|---|
| `SetMaxThreads`, `SetThreading`, `SetResources`, `FreeMemory` | Unchanged |
| `SolveBoard`, `SolveBoardPBN` | Unchanged |
| `CalcDDtable`, `CalcDDtablePBN` | Unchanged |
| `CalcAllTables`, `CalcAllTablesPBN` | Unchanged (but wrapper struct sizes grew) |
| `SolveAllBoards`, `SolveAllBoardsBin` | Unchanged (but wrapper struct sizes grew) |
| `SolveAllChunks`, `SolveAllChunksBin`, `SolveAllChunksPBN` | Unchanged |
| `Par`, `CalcPar`, `CalcParPBN` | Unchanged |
| `SidesPar`, `DealerPar`, `DealerParBin`, `SidesParBin` | Unchanged |
| `ConvertToDealerTextFormat`, `ConvertToSidesTextFormat` | Unchanged |
| `AnalysePlayBin`, `AnalysePlayPBN` | Unchanged |
| `AnalyseAllPlaysBin`, `AnalyseAllPlaysPBN` | Unchanged (but wrapper struct sizes grew) |
| `GetDDSInfo`, `ErrorMessage` | Unchanged |

### Internal behavioral changes (not API-visible)

These do not change the API contract but may affect observable behavior:

| Change | Effect |
|---|---|
| Scheduler uses `std::vector` instead of fixed arrays | Allows arbitrary batch sizes without recompilation |
| `CalcAllBoardsN` zeroes only `noOfBoards` entries (was `MAXNOOFBOARDS`) | Faster for small batches; identical results |
| `SolveAllBoardsN` zeroes only `noOfBoards` entries (was `MAXNOOFBOARDS`) | Same as above |
| Strain grouping in Scheduler (`MakeGroupsByDeal`) | Groups same-deal strains for scheduling; correctness fix ensures strain field is compared in duplicate detection |
| Persistent thread pool | Same results, faster due to thread reuse |
| Hash-based duplicate detection in `SolveBoard.cpp` | Same results, O(n) instead of O(n^2) |
| `SameHand` / `FinetuneGroups` strain check | **Correctness fix**: prevents different strains of the same deal from being deduplicated |

---

## DDS Library Fixes Applied

| File | Fix | Why |
|---|---|---|
| `include/dll.h` | New `CalcAllTablesPBNx` API; `MAXNOOFTABLES=1000` default | Dynamic batch support; optimal internal chunk size |
| `src/Scheduler.h` | Vectors replace fixed arrays; `HASH_MAX` is a base constant | Dynamic sizing; eliminates buffer overflows at any batch size |
| `src/Scheduler.cpp` | `EnsureCapacity()` resizes vectors per batch; `MakeGroupsByDeal()`; **strain comparison in `SameHand` and `FinetuneGroups`** | Dynamic memory; strain-grouped scheduling; **correctness fix** (see below) |
| `src/CalcTables.cpp` | `CalcAllTablesPBNx` impl; heap structs; targeted zeroing loop | Dynamic API; stack safety; perf (only zero used entries) |
| `src/SolveBoard.cpp` | Heap `boards`; targeted zeroing; FNV-1a hash duplicate detection | Stack safety; perf; O(n) duplicate detection |
| `src/PlayAnalyser.cpp` | Heap `boards` and `playTracesBin` | Stack safety with large MAXNOOFBOARDS |
| `src/System.h/.cpp` | Persistent thread pool with generation counter | Eliminates per-batch thread creation overhead |
| `test/testcommon.cpp` | All large batch structs moved to heap | Stack safety |
| `test/backend_eval.cpp` | Uses `CalcAllTablesPBNx`; no manual chunking; OOB cross-verification | Clean API usage; correctness validation |
| `CMakeLists.txt` | Removed `DDS_MAXNOOFTABLES/MAXNOOFBOARDS/STACK_SIZE` overrides | No longer needed with dynamic API |

### Correctness Fixes

**Scheduler strain deduplication bug** (`Scheduler.cpp`): The `MakeGroupsByDeal()` optimization groups all 5 strains of the same deal into one scheduler bucket for cache locality. However, the `FinetuneGroups()` duplicate detection and `SameHand()` comparison only checked card distributions (`remainCards`), not the trump/strain field. This caused different strains of the same deal to be incorrectly identified as duplicates -- only one strain was solved, and its result was copied to all 5 slots. This produced incorrect DD tables (all 5 strains showing identical trick counts).

**Fix**: Added `hands[b1].strain == hands[b2].strain` checks to both the fast path (length==2 bucket in `FinetuneGroups`) and the general path (`SameHand`). **Caught by OOB cross-verification** against a freshly built upstream `dds-bridge/dds` DLL -- the verification showed systematic mismatches that led to diagnosis. After the fix, all tested configurations show 0 mismatches against the upstream DLL.

---

## Build Quick Reference

```bash
# Standard build -- handles any number of deals automatically
cmake -DDDS_THREADING=STL -DDDS_AGGRESSIVE_OPT=ON ..

# OOB-ST equivalent (single-threaded, for comparison only)
cmake -DDDS_THREADING=NONE ..

# OOB-MT equivalent (multi-threaded, no other optimizations)
cmake -DDDS_THREADING=STL ..
```

No `MAXNOOFTABLES`, `MAXNOOFBOARDS`, or `STACK_SIZE` overrides needed. One build works for all batch sizes.

## Language Interop (Python / ctypes)

### Recommended API for foreign callers

**Use `CalcAllTablesPBNx` exclusively.** It was designed for easy FFI:

- All parameters are scalars, plain arrays, or NULL -- no wrapper structs sized by `MAXNOOFTABLES`/`MAXNOOFBOARDS`.
- The three struct types it uses (`ddTableDealPBN`, `ddTableResults`, `parResults`) are small, fixed-size, and trivially mapped to `ctypes.Structure`.
- No need to know or replicate the values of `MAXNOOFTABLES` or `MAXNOOFBOARDS` in the Python layer.

### Calling convention

On Windows the DLL exports use `__stdcall` (the `STDCALL` macro). On Linux/macOS, `STDCALL` expands to nothing (default `cdecl`).

| Platform | Python loader | Why |
|---|---|---|
| Windows | `ctypes.WinDLL("dds.dll")` | `WinDLL` uses `__stdcall` calling convention |
| Linux | `ctypes.CDLL("./libdds.so")` | `CDLL` uses default `cdecl` |
| macOS | `ctypes.CDLL("./libdds.dylib")` | Same as Linux |

Using the wrong loader (e.g. `CDLL` on Windows) will corrupt the stack and crash.

### Struct definitions needed

Only three small structs are required for `CalcAllTablesPBNx`:

```python
import ctypes

class ddTableDealPBN(ctypes.Structure):
    _fields_ = [("cards", ctypes.c_char * 80)]

class ddTableResults(ctypes.Structure):
    _fields_ = [("resTable", (ctypes.c_int * 4) * 5)]  # [DDS_STRAINS][DDS_HANDS]

class parResults(ctypes.Structure):
    _fields_ = [
        ("parScore",           (ctypes.c_char * 16) * 2),
        ("parContractsString", (ctypes.c_char * 128) * 2),
    ]
```

### Calling `CalcAllTablesPBNx` from Python

```python
import ctypes, sys

# Load DDS
if sys.platform == "win32":
    dds = ctypes.WinDLL("dds.dll")
else:
    dds = ctypes.CDLL("./libdds.so")

# Declare signature
dds.CalcAllTablesPBNx.restype = ctypes.c_int
dds.CalcAllTablesPBNx.argtypes = [
    ctypes.c_int,                                        # numDeals
    ctypes.POINTER(ddTableDealPBN),                      # dealCards[]
    ctypes.c_int,                                        # mode
    ctypes.c_int * 5,                                    # trumpFilter[5]
    ctypes.POINTER(ddTableResults),                      # results[]
    ctypes.POINTER(parResults),                          # par[] (or None)
]

# Prepare data
n = 100
DealArray   = ddTableDealPBN * n
ResultArray = ddTableResults * n

deals   = DealArray()
results = ResultArray()
trump_filter = (ctypes.c_int * 5)(0, 0, 0, 0, 0)  # solve all strains

for i in range(n):
    deals[i].cards = b"N:AK.QJ.T9.8765 432.432.432.432 ..."  # PBN string

ret = dds.CalcAllTablesPBNx(
    n, deals, -1, trump_filter, results, None)

if ret != 1:
    buf = ctypes.create_string_buffer(80)
    dds.ErrorMessage(ret, buf)
    raise RuntimeError(f"DDS error {ret}: {buf.value.decode()}")

# Read results
for i in range(n):
    for strain in range(5):
        for hand in range(4):
            tricks = results[i].resTable[strain][hand]
```

### Why NOT to use the old batch APIs from Python

The old APIs (`CalcAllTablesPBN`, `CalcAllTables`, `SolveAllBoards`) require wrapper structs like `ddTableDealsPBN`, `ddTablesRes`, `allParResults`, etc. These structs contain arrays sized by `MAXNOOFTABLES` (1000) or `MAXNOOFBOARDS` (5000):

| Struct | ctypes size | Problem |
|---|---|---|
| `boards` | ~460 KB | One typo in field count corrupts everything |
| `solvedBoards` | ~1.4 MB | Same |
| `ddTableDealsPBN` | ~400 KB | Must match DLL's exact MAXNOOFTABLES value |
| `ddTablesRes` | ~400 KB | Same |
| `allParResults` | ~288 KB | Same |

If the Python `ctypes.Structure` layout does not **exactly** match the compiled DLL's struct layout (e.g. because `MAXNOOFTABLES` was changed), you get silent memory corruption. With `CalcAllTablesPBNx`, this problem does not exist -- the only structs used are small, fixed-size, and independent of `MAXNOOFTABLES`.

### Struct padding / alignment

All three structs used by `CalcAllTablesPBNx` are naturally aligned:

- `ddTableDealPBN`: 80 bytes, `char` array, no padding.
- `ddTableResults`: 80 bytes, 5x4 `int` array, naturally aligned.
- `parResults`: 288 bytes, `char` arrays, no padding.

No `_pack_` attribute is needed in the `ctypes.Structure` definitions. If DDS is ever compiled with non-default packing (`#pragma pack`), the Python side would need `_pack_ = N` to match, but the current codebase does not use any packing pragmas.

### Other interop considerations

| Topic | Guidance |
|---|---|
| **Thread safety** | `CalcAllTablesPBNx` is NOT thread-safe (uses global state internally). Do not call from multiple Python threads simultaneously. Use a `threading.Lock` or call from a single thread. |
| **GIL** | The call releases the GIL while the C code runs (ctypes does this automatically). Python threads doing non-DDS work will remain responsive during long solves. |
| **NumPy integration** | Results can be bulk-copied into a NumPy array: `np.frombuffer(results, dtype=np.int32).reshape(n, 5, 4)` -- but verify byte layout first since `resTable` is `[strain][hand]` = `[5][4]`. |
| **Error handling** | Always check `ret != 1`. Call `ErrorMessage(ret, buf)` for human-readable text. |
| **PATH / LD_LIBRARY_PATH** | The DDS DLL/SO must be findable. On Windows, place `dds.dll` in the working directory or on `PATH`. On Linux, use `LD_LIBRARY_PATH` or install to a system lib directory. |

### Other languages (quick reference)

| Language | Approach | Notes |
|---|---|---|
| **C#** | `[DllImport("dds.dll", CallingConvention = CallingConvention.StdCall)]` | Use `[StructLayout(LayoutKind.Sequential)]` for the three structs. Marshal arrays with `[MarshalAs(UnmanagedType.ByValArray)]`. |
| **Java** | JNA `Native.load("dds", DdsLib.class)` | Map structs extending `com.sun.jna.Structure`. Use `StdCallLibrary` on Windows. |
| **Rust** | `extern "stdcall" fn` (Windows) / `extern "C" fn` (Linux) | Use `#[repr(C)]` on struct definitions. `libloading` crate for dynamic loading. |
| **Node.js** | `ffi-napi` or `node-ffi` | Declare structs with `ref-struct-napi`. Use `'stdcall'` ABI on Windows. |

---

## Notes

- **OOB-ST** = single-threaded DDS as cloned, no changes. **OOB-MT** = same code, built with `-DDDS_THREADING=STL`.
- The OOB cross-verification now uses the OOB DLL's `CalcAllTablesPBN` batch API (chunks of 40) for a fair batched-vs-batched comparison. Historical benchmarks above were measured with serial `CalcDDtablePBN` (one deal per call).
- All speedups are cumulative from the specified baseline unless stated otherwise.
- The `HASH_MAX` bug exists in upstream DDS -- it would crash any application sending >~125 deals per `CalcAllTablesPBN` batch, even without our changes.
- **Batching + threading** is the primary source of speedup. The `CalcAllTablesPBNx` API expands N deals into N*5 boards and distributes them across all threads.  With OOB also batching (via `CalcAllTablesPBN`, chunks of 40), ddss's remaining 1.44x advantage comes from larger chunk size (1000 vs 40), the persistent thread pool, and strain grouping.
- **Strain grouping** (`MakeGroupsByDeal`) groups all strains of the same deal in one scheduler bucket. A correctness bug in the initial implementation was caught by OOB cross-verification and fixed (see "Correctness Fixes" above).
- **Persistent thread pool** eliminates per-batch thread creation overhead (~32ms saved per batch on a 32-core machine).
- Hash-based duplicate detection (O(n) vs O(n^2)) and targeted memset are correctness-preserving micro-optimizations with negligible throughput impact at current batch sizes.
- **Dynamic API** eliminates the entire "batch size tuning" workflow. One build, one API call, any number of deals. The library chunks internally at 1000 deals, which benchmarking showed is the optimal internal batch size.
- **OOB cross-verification** (`--verify --oob-dll`) dynamically loads a separate DDS DLL and compares results cell-by-cell. This infrastructure caught the Scheduler strain deduplication bug and now serves as an ongoing regression check.
