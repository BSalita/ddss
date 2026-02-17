Introduction
============
DDS is a double-dummy solver of bridge hands.  It is provided as a Windows DLL and as C++ source code suitable for a number of operating systems.  It supports single-threading and multi-threading for improved performance.

DDS offers a wide range of functions, including par-score calculations.

This fork (`dds-bridge`) adds performance optimizations (up to ~98x vs out-of-box single-threaded) and a dynamic batch API (`CalcAllTablesPBNx`).  See `METRICS_TRUTH_TABLE.md` for benchmarks and API change details.

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

Robert Salita used AI (Claude Opus 4.6 Max) to refactor for 100x performance gain and greater flexibility.


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

There is a parallel distribution, [**ddd**](https://github.com/dds-bridge/ddd).  It consisting of an old driver program for DDS contributed under the GPL (not under the Apache license) by Flip Cronje, and updated by us to support the multi-threaded library file.


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

**Linux:**
```bash
cmake -DDDS_THREADING=STL -DDDS_AGGRESSIVE_OPT=ON ..
make -j$(nproc)
```
Requires `pthreads` (for `std::thread`) and `libdl` (for `dlopen`).  Both are auto-detected by CMake.

**macOS:**
```bash
cmake -DDDS_THREADING=STL -DDDS_AGGRESSIVE_OPT=ON ..
make -j$(sysctl -n hw.ncpu)
```

### Verifying the build

```bash
# Quick smoke test with a PBN URL
./dtest --pbn-source https://raw.githubusercontent.com/ContractBridge/pbn-files/refs/heads/master/1997-Cavendish_Invitational_Pairs_Tournament/ROUND1.PBN --report-dir smoke_test

# Random deals benchmark
./dtest --random-deals 1000 --report-dir benchmark
```


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

**Recommended: `CalcAllTablesPBNx` (dynamic API, new in this fork)**

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

These still work but require wrapper structs (`ddTableDealsPBN`, `ddTablesRes`, `allParResults`) that are now very large due to internal `MAXNOOFTABLES=1000`.  New code should use `CalcAllTablesPBNx` instead.

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
| `-k` | `--reduced-cards` | `n` | `13` | Cards per hand in random-deals mode (`1..13`). |
| `-e` | `--seed` | `n` | `42` | Random seed for deal generation. |
| `-g` | `--report-dir` | `p` | `dds_compare_reports` | Output directory for reports (JSON, CSV). |
| `-p` | `--pbn-source` | `s` | *(none)* | Local PBN file path or URL. Enables PBN evaluation mode. |
| `-w` | `--html-report` | `f` | *(none)* | Write a readable HTML report file (intended for PBN evaluation mode). |

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

# Reduced-endgame test (4 cards/hand)
./dtest --random-deals 1000 --reduced-cards 4 --report-dir endgame_test
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

* `run_all.bat`
  * Orchestrates the full workflow:
    1) `rebuild_dtest.bat`
    2) `smoke_test.bat`
  * Stops on the first failure and returns non-zero exit code on error.

Recommended usage:

```
run_all.bat
```

Or run manually:

```
rebuild_dtest.bat
smoke_test.bat
```

Docs
====

| Document | Description |
|----------|-------------|
| `doc/dll-description.md` | Upstream DDS 2.8.2 API reference (structs, functions, return codes). |
| `METRICS_TRUTH_TABLE.md` | **This fork's** performance benchmarks, API changes vs upstream DDS 2.9.0, dynamic API docs, and Python/language interop guide. |

Bugs
====
Please report bugs as GitHub issues.

