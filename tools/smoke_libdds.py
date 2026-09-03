#!/usr/bin/env python3
"""Load libdds.so (or dds.dll) and call CalcAllTablesPBNx on one deal.

Mirrors the ctypes ABI used by mlBridge/dds_ddss.py:
  CalcAllTablesPBNx, SetMaxThreads, ErrorMessage via CDLL (cdecl) on Linux.

Usage:
  python3 tools/smoke_libdds.py
  python3 tools/smoke_libdds.py dist/linux-x86_64/libdds.so
"""

from __future__ import annotations

import ctypes
import sys
from pathlib import Path


class ddTableDealPBN(ctypes.Structure):
    _fields_ = [("cards", ctypes.c_char * 80)]


class ddTableResults(ctypes.Structure):
    _fields_ = [("resTable", (ctypes.c_int * 4) * 5)]


class parResults(ctypes.Structure):
    _fields_ = [
        ("parScore", (ctypes.c_char * 16) * 2),
        ("parContractsString", (ctypes.c_char * 128) * 2),
    ]


# examples/hands.cpp deal 0 (list100.txt first board).
PBN = b"N:QJ6.K652.J85.T98 873.J97.AT764.Q4 K5.T83.KQ9.A7652 AT942.AQ4.32.KJ3"


def default_lib() -> Path:
    root = Path(__file__).resolve().parents[1]
    if sys.platform == "win32":
        return root / "build-cmake" / "Release" / "dds.dll"
    for candidate in (
        root / "dist" / "linux-x86_64" / "libdds.so",
        root / "build-linux" / "libdds.so",
        root / "libdds.so",
    ):
        if candidate.is_file():
            return candidate
    return root / "dist" / "linux-x86_64" / "libdds.so"


def main() -> int:
    lib_path = Path(sys.argv[1]) if len(sys.argv) > 1 else default_lib()
    if not lib_path.is_file():
        print(f"FAIL: library not found: {lib_path}", file=sys.stderr)
        return 1

    print(f"Loading {lib_path}")
    if sys.platform == "win32":
        dds = ctypes.WinDLL(str(lib_path))
    else:
        dds = ctypes.CDLL(str(lib_path))

    dds.SetMaxThreads.restype = None
    dds.SetMaxThreads.argtypes = [ctypes.c_int]
    dds.SetMaxThreads(0)

    dds.ErrorMessage.restype = None
    dds.ErrorMessage.argtypes = [ctypes.c_int, ctypes.c_char_p]

    dds.CalcAllTablesPBNx.restype = ctypes.c_int
    dds.CalcAllTablesPBNx.argtypes = [
        ctypes.c_int,
        ctypes.POINTER(ddTableDealPBN),
        ctypes.c_int,
        ctypes.c_int * 5,
        ctypes.POINTER(ddTableResults),
        ctypes.POINTER(parResults),
    ]

    deals = (ddTableDealPBN * 1)()
    deals[0].cards = PBN
    results = (ddTableResults * 1)()
    trump_filter = (ctypes.c_int * 5)(0, 0, 0, 0, 0)

    ret = dds.CalcAllTablesPBNx(1, deals, -1, trump_filter, results, None)
    if ret != 1:
        buf = ctypes.create_string_buffer(80)
        dds.ErrorMessage(ret, buf)
        print(f"FAIL: CalcAllTablesPBNx returned {ret}: {buf.value!r}", file=sys.stderr)
        return 1

    table = results[0]
    # list100.txt TABLE for this deal: strain-major, N E S W
    expected = (
        (5, 8, 5, 8),
        (6, 6, 6, 6),
        (5, 7, 5, 7),
        (7, 5, 7, 5),
        (6, 6, 6, 6),
    )
    for strain in range(5):
        got = tuple(table.resTable[strain][hand] for hand in range(4))
        if got != expected[strain]:
            print(
                f"FAIL: strain {strain} got {got} expected {expected[strain]}",
                file=sys.stderr,
            )
            return 1

    print("NT North tricks:", table.resTable[4][0])
    print("OK: lib loaded, SetMaxThreads(0), CalcAllTablesPBNx solved 1 deal")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
