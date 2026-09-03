Introduction
============
DDS is a double-dummy solver of bridge hands.  It is provided as a Windows DLL and as C++ source code suitable for a number of operating systems.  It supports single-threading and multi-threading for improved performance.

DDS offers a wide range of functions, including par-score calculations.

This `ddss` fork adds performance optimizations (1.44x vs OOB batched, ~7.6x vs OOB serial, ~17.7x vs OOB single-threaded), a dynamic batch API (`CalcAllTablesPBNx`), and OOB cross-verification (`--verify`).  All results verified cell-by-cell against a freshly built upstream DDS DLL using a fair batched-vs-batched comparison.  See `METRICS_TRUTH_TABLE.md` for benchmarks and API change details.

Based on DDS 2.9.0, licensed under the Apache 2.0 license in the LICENSE file.

(c) Bo Haglund 2006-2014, (c) Bo Haglund / Soren Hein 2014-2018.


Credits
=======
Many people have generously contributed ideas, code and time to make DDS a great program.  While leaving out many people, we thank the following here.

The code in Par.cpp for calculating par scores and contracts is based on Matthew Kidd's perl code for ACBLmerge.  He has kindly given permission to include a C++ adaptation in DDS.

Alex Martelli cleaned up and ported code to Linux and to Mac OS X in 2006.  The code grew a bit outdated over time, and in 2014 Matthew Kidd contributed updates.

Brian Dickens found bugs in v2.7 and encouraged us to look at GitHub.  He also set up the entire historical archive and supervised our first baby steps on GitHub.

Foppe Hemminga maintains DDS on ArchLinux.  He also contributed a version of the documentation file completely in .md mark-up language.

Pierre Cossard contributed the code for multi-threading on the Mac using GDS.

Soren Hein made a number of contributions before becoming a co-author starting with v2.8 in 2014.

Robert Salita used AI (Claude Opus 4) to add batched solving, strain grouping, a persistent thread pool, a dynamic batch API (`CalcAllTablesPBNx`), and OOB cross-verification (`--verify`), achieving 1.44x throughput vs OOB batched (~7.6x vs OOB serial) on a 32-core machine. All results verified cell-by-cell against a freshly built upstream DDS DLL using a fair batched-vs-batched comparison.


Overview
========

The distribution consists of the following directories.

* **src**, the source code for the DDS library.
* **include**, where the public interface of the library is specified (`dll.h`).
* **lib**, the place where the library file is "installed" for test purposes.
* **doc**, where the library interface is documented and the algorithms behind DDS are explained at a high level.
* **hands**, a repository for input files to the test programs.
* **test**, a test program (`dtest`).
* **tools**, Python comparison scripts for backend evaluation.
* **examples**, some minimal programs showing how to interface in practice with a number of library functions.

There is a parallel distribution, [**ddd**](https://github.com/dds-bridge/ddd).  It consists of an old driver program for DDS contributed under the GPL (not under the Apache license) by Flip Cronje, and updated by us to support the multi-threaded library file.


Installation
============

### Prerequisites

| Component | Windows | Linux | macOS |
|-----------|---------|-------|-------|
| C++17 compiler | Visual Studio 2022 (MSVC 19.30+) | g++ 9+ or clang 10+ | Xcode clang 12+ or Homebrew g++ |
| Build system | CMake 3.21+ | CMake 3.21+ | CMake 3.21+ |
| Threading (optional) | Built-in (STL, WinAPI) | pthreads (auto-detected) | pthreads (auto-detected) |

### Building the DDS library and test program

All platforms use the same CMake workflow.  The build produces two artifacts:
- `dds.dll` / `libdds.so` / `libdds.dylib` -- the DDS shared library.
- `dtest` / `dtest.exe` -- the test/benchmark executable.

**Standard build (recommended):**

```bash
mkdir build && cd build

# Multi-threaded with all optimizations
cmake -DDDS_THREADING=STL -DDDS_AGGRESSIVE_OPT=ON ..
cmake --build . --config Release
```

**Single-threaded build (for comparison only):**

```bash
cmake -DDDS_THREADING=NONE ..
cmake --build . --config Release
```

### CMake options

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `DDS_THREADING` | `NONE`, `STL`, `OPENMP`, `WINAPI` | `STL` | Threading backend. `STL` recommended for all platforms. |
| `DDS_AGGRESSIVE_OPT` | `ON`/`OFF` | `OFF` | Enables `/Ox /GL /LTCG` (MSVC) or `-O3 -flto -march=native` (GCC/Clang). |

No `MAXNOOFTABLES`, `MAXNOOFBOARDS`, or stack-size overrides are needed.  One build handles any batch size.

### Platform notes

**Windows (MSVC):**
```
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -DDDS_THREADING=STL -DDDS_AGGRESSIVE_OPT=ON ..
cmake --build . --config Release
```

**Linux (local machine, with cmake + g++):**
```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DDDS_THREADING=STL
cmake --build build-linux --target dds dtest -j$(nproc)
# Output: build-linux/libdds.so  and  build-linux/dtest
```

**Linux (postmortem / wslc `python:3.12-slim`, recommended for the container `.so`):**
```powershell
# From this repo on Windows, builds inside a throwaway slim container:
powershell -ExecutionPolicy Bypass -File tools\build_linux_so.ps1
```
```bash
# Same steps inside Debian/Ubuntu (the script apt-get installs cmake g++ make if missing):
bash tools/build_linux_so.sh
python3 tools/smoke_libdds.py dist/linux-x86_64/libdds.so
./build-linux/dtest --random-deals 5 --seed 42 --numthr 2 --report-dir /tmp/dtest_linux_smoke
```

Do **not** turn on `DDS_AGGRESSIVE_OPT` for a `.so` shipped in the postmortem image: it adds `-march=native`, which can crash on a different CPU than the build host.  Release already uses `-O3`.

CMake output:
- `build-linux/libdds.so` (also copied to `dist/linux-x86_64/libdds.so`)
- `build-linux/dtest`

Runtime deps of `libdds.so` (Debian bookworm / `python:3.12-slim`): `libstdc++.so.6`, `libgcc_s.so.1`, `libm.so.6`, `libpthread` (glibc), `libc.so.6`.  `libdl` is needed by `dtest` (`dlopen` of OOB engines), not by `libdds.so` itself.  All of these are already in `python:3.12-slim`.

`src/ops/deploy_postmortem.ps1` stages this artifact and `Dockerfile.postmortem` installs it as:

```
COPY libdds.so /usr/local/lib/libdds.so
```

mlBridge `dds_ddss.py` loads `/usr/local/lib/libdds.so` on Linux (`ctypes.CDLL`, cdecl), with `DDSS_DLL_PATH` as an override.  Fallback search includes `dist/linux-x86_64/libdds.so` and `build-linux/libdds.so` next to this repo.

**macOS:**
```bash
cmake -DDDS_THREADING=STL -DDDS_AGGRESSIVE_OPT=ON ..
make -j$(sysctl -n hw.ncpu)
```

### Verifying the build

```bash
# Quick smoke test with a PBN URL
./dtest --pbn-source raw.githubusercontent.com/ContractBridge/pbn-files/refs/heads/master/1997-Cavendish_Invitational_Pairs_Tournament/ROUND1.PBN --report-dir smoke_test

# Random deals benchmark
./dtest --random-deals 1000 --report-dir benchmark

# Cross-verify against upstream DDS DLL (requires dds_oob.dll)
./dtest --random-deals 100 --verify
```

### OOB cross-verification

The `--verify` flag enables cell-by-cell comparison of DD results against one or more upstream (out-of-box) DDS DLLs loaded at runtime.  This catches correctness regressions introduced by optimizations, and doubles as an engine benchmark: `--oob-dll` may be repeated to compare several engines (e.g. dds 2.9 and dds 3.0) side by side against ddss.

**Setup (Windows, one command):**

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_oob_dlls.ps1
```

This clones `dds-bridge/dds` at pinned commits into `external/` (gitignored), builds a 2.9 DLL and a 3.0 DLL with MSVC, and installs them next to `dtest.exe` as `dds_oob.dll` and `dds3_oob.dll`.  It also patches an upstream DDS 3.0 project-file bug (`solution/dds_native.vcxproj` omits `library/src/system/parallel_boards.cpp`, causing unresolved externals at link time).

**Setup (manual):**
1. Build an upstream `dds-bridge/dds` DLL from source (or obtain one).  Both the 2.9 branch and the 3.0 rewrite (develop branch, `dds_native.dll` from `solution/dds_native.vcxproj`) export the required legacy C API.
2. Place it as `dds_oob.dll` (Windows) or `dds_oob.so` (Linux/macOS) next to the `dtest` executable.  A DDS 3.0 DLL named `dds3_oob.dll`/`dds3_oob.so` in the same directory is picked up automatically as a second engine.  Or specify paths explicitly with one or more `--oob-dll` flags.

**How it works:**
- Each OOB DLL is dynamically loaded via `LoadLibraryA` (Windows) or `dlopen` (Linux/macOS) and labeled by its own `GetDDSInfo` version string (e.g. `dds 2.9.0`, `dds 3.0.0`).
- Deals are solved in batches of 40 through each OOB DLL's `CalcAllTablesPBN` batch API -- giving OOB the same cross-deal parallelism that ddss gets from `CalcAllTablesPBNx`.  OOB-compatible batch structs (sized at the upstream `MAXNOOFTABLES=40`) are used to avoid ABI mismatch with the ddss-compiled structs.  The layout is shared by DDS 2.9 and DDS 3.0.
- Each OOB DLL must export `CalcAllTablesPBN` (all standard upstream builds do).  Verification fails fast if it is missing.
- Results are compared cell-by-cell against the ddss results, and pairwise between the reference engines.
- An `Engine Comparison Summary` table reports each engine's elapsed time, tables/s, speed relative to ddss, and mismatch count vs ddss, followed by pairwise reference-engine checks and an overall PASS/FAIL.
- Mismatches are written to `oob_mismatches.csv` in the report directory with `engine_a`/`engine_b` columns.
- HTML reports (when `--html-report` is used) include one `<engine>_dd20` column per engine, highlighting any disagreements.

**Behavior:**
- `--verify` alone: looks for `dds_oob.dll`/`dds_oob.so` (and optionally `dds3_oob.dll`/`dds3_oob.so`) next to the executable; errors if neither is found.
- `--oob-dll path` alone: implicitly enables verification using the specified DLL(s).
- Neither flag: no verification, normal solve only.


Supported systems
=================

The code is cross-platform and builds on:

* **Windows**: Visual Studio 2022 (MSVC), MinGW g++, Cygwin g++.
* **Linux**: g++ 9+, clang 10+, with `pthreads` and `libdl`.
* **macOS**: Xcode clang 12+, Homebrew g++.

All platforms use the same `CMakeLists.txt`.  The library is built as a shared library (`.dll`, `.so`, or `.dylib`).

There's an example .Net wrapper on https://github.com/anorsich/dds.net (not supported by us).


Usage
=====

### Library API

DDS provides two batch API approaches:

**Recommended: `CalcAllTablesPBNx` (dynamic API, new in this ddss fork)**

Solves any number of deals in a single call.  The library handles internal chunking, memory allocation, and thread distribution automatically.  No compile-time size limits.  This is the preferred API for new code and for language interop (Python, C#, etc.).

```c
int CalcAllTablesPBNx(
  int numDeals,                     // any positive integer
  struct ddTableDealPBN dealCards[], // plain array, length numDeals
  int mode,                         // -1 = no par, 0-3 = vulnerability
  int trumpFilter[5],               // 0 = solve strain, 1 = skip
  struct ddTableResults results[],  // plain array, length numDeals (out)
  struct parResults par[]);         // plain array or NULL if mode == -1
```

See `METRICS_TRUTH_TABLE.md` for complete documentation including Python ctypes examples, struct definitions, and language interop guidance.

**Legacy: `CalcAllTablesPBN` / `CalcAllTables`**

These still work but require wrapper structs (`ddTableDealsPBN`, `ddTablesRes`, `allParResults`) that are larger due to internal `MAXNOOFTABLES=1000`.  New code should use `CalcAllTablesPBNx` instead.

### ABI compatibility warning

The ddss fork increases `MAXNOOFTABLES` from 40 to 1000 (and `MAXNOOFBOARDS` from 200 to 5000).  Because many DDS structs contain fixed-size arrays dimensioned by these constants, **ddss structs are significantly larger than upstream dds structs**:

| Struct | Upstream (dds) | ddss | Reason |
|--------|---------------|------|--------|
| `ddTableDealsPBN` | ~16 KB | ~400 KB | `MAXNOOFTABLES * DDS_STRAINS` |
| `ddTablesRes` | ~16 KB | ~400 KB | `MAXNOOFTABLES * DDS_STRAINS` |
| `allParResults` | ~12 KB | ~288 KB | `MAXNOOFTABLES` |
| `boards` | ~26 KB | ~650 KB | `MAXNOOFBOARDS` |

This has two consequences:

1. **Stack overflow risk.**  Code that stack-allocates these structs (e.g. `boards bo;` or `ddTableDealsPBN batch;`) will consume far more stack space than with upstream dds and may crash.  Use heap allocation instead: `auto bo = std::make_unique<boards>();`.

2. **Binary incompatibility with upstream dds.**  The ddss DLL and an upstream dds DLL are **not ABI-compatible** for any API function that passes these large structs (e.g. `CalcAllTablesPBN`, `SolveAllBoards`).  You cannot swap one DLL for the other without recompiling the caller.  The new `CalcAllTablesPBNx` API avoids this problem entirely -- it uses only small, fixed-size per-deal structs that are identical across builds.

### Thread configuration

DDS auto-configures the number of threads based on available cores and memory.  The user can override this by calling `SetMaxThreads()` or `SetResources()`.  On Windows, `SetMaxThreads` is called automatically when the DLL loads.  On Linux/macOS, call `SetMaxThreads(0)` explicitly for auto-configuration.

Test program (`dtest`) command-line reference
=============================================

The `dtest` program supports DDS solver testing, random deal generation, PBN evaluation, backend comparison reports, and HTML report output.

### All options

| Short | Long | Argument | Default | Description |
|:-----:|------|----------|---------|-------------|
| `-f` | `--file` | `s` | `input.txt` | Input file path, or a number `n` (e.g. `100` means `../hands/list100.txt`). |
| `-s` | `--solver` | `s` | `solve` | Solver mode: `solve`, `calc`, `play`, `par`, `dealerpar`. |
| `-t` | `--threading` | `t` | `default` | Threading backend: `default`, `none`, `winapi`, `openmp`, `gcd`, `boost`, `stl`, `tbb`, `stlimpl`, `pplimpl`. |
| `-n` | `--numthr` | `n` | `0` | Maximum number of threads (`0` = DDS decides). |
| `-m` | `--memory` | `n` | `0` | Total DDS memory in MB (`0` = DDS decides). |
| `-r` | `--random-deals` | `n` | `0` | Generate `n` random deals and solve (`0` = disabled). |
| `-e` | `--seed` | `n` | `42` | Random seed for deal generation. |
| `-g` | `--report-dir` | `p` | `dds_compare_reports` | Output directory for reports (JSON, CSV). |
| `-p` | `--pbn-source` | `s` | *(none)* | Local PBN file path or URL. Enables PBN evaluation mode. |
| `-w` | `--html-report` | `f` | *(none)* | Write a readable HTML report file (intended for PBN evaluation mode). |
| `-o` | `--oob-dll` | `p` | *(auto)* | Path to an OOB (upstream) DDS DLL for cross-verification. May be repeated to compare several engines. Implicitly enables `--verify`. Default: `dds_oob.dll` (plus `dds3_oob.dll` if present) next to the executable. |
| `-v` | `--verify` | *(none)* | `off` | Enable OOB cross-verification of DD results. Requires `--oob-dll` or `dds_oob.dll` in the executable's directory. |

Run `dtest` with no arguments to see the built-in help text.

### Examples

```bash
# Random deals benchmark
./dtest --random-deals 10000 --report-dir dds_compare_reports --seed 42

# PBN source mode (local file) with HTML report
./dtest --pbn-source ../hands/list100.txt \
  --report-dir pbn_reports --html-report pbn_reports/report.html

# PBN source mode (URL)
./dtest --pbn-source https://example.com/deals.pbn --report-dir pbn_reports_url

# OOB cross-verification (uses dds_oob.dll next to executable)
./dtest --random-deals 100 --verify

# OOB cross-verification with explicit DLL path
./dtest --random-deals 100 --verify --oob-dll /path/to/upstream/dds.dll

# Three-way comparison: ddss vs dds 2.9 vs dds 3.0
./dtest --random-deals 500 \
  --oob-dll /path/to/dds29/dds.dll \
  --oob-dll /path/to/dds3/dds_native.dll

# PBN evaluation with OOB verification and HTML report
./dtest --pbn-source deals.pbn --verify --html-report report.html
```

Windows helper scripts (`.bat`)
===============================

These scripts live in the repository root and are intended for Windows CMD use.

* `rebuild_dtest.bat`
  * Configures and builds `dtest` with CMake + Visual Studio 2022.
  * Includes a fallback path-trim step to recover from the CMD "input line is too long" environment issue.
  * Output binary: `build-cmake\Release\dtest.exe`.

* `smoke_test.bat`
  * Runs a PBN smoke test using `build-cmake\Release\dtest.exe`
    with a PBN URL source and HTML output.
  * Writes artifacts to `smoke_probe`.

Docs
====

| Document | Description |
|----------|-------------|
| `doc/dll-description.md` | Upstream DDS 2.8.2 API reference (structs, functions, return codes). |
| `METRICS_TRUTH_TABLE.md` | **ddss fork** performance benchmarks, API changes vs upstream DDS 2.9.0, dynamic API docs, and Python/language interop guide. |

Bugs
====
Please report bugs as GitHub issues.

