"""
DDS backend compare harness.

Compares CPU exact baseline against a candidate result set.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np
import pandas as pd


def _read_table(path: Path) -> pd.DataFrame:
    suffix = path.suffix.lower()
    if suffix == ".parquet":
        return pd.read_parquet(path)
    if suffix in {".csv", ".txt"}:
        return pd.read_csv(path)
    if suffix == ".jsonl":
        return pd.read_json(path, lines=True)
    raise ValueError(f"Unsupported file format: {path}")


def _safe_write_parquet(df: pd.DataFrame, path: Path) -> Path:
    try:
        df.to_parquet(path, index=False)
        return path
    except Exception:
        fallback = path.with_suffix(".csv")
        df.to_csv(fallback, index=False)
        return fallback


def _write_json(data: Dict, path: Path) -> None:
    with path.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="DDS CPU exact compare runner")
    p.add_argument("--cpu-file", required=True, help="CPU baseline table with cpu_0..cpu_19 columns")
    p.add_argument("--candidate-file", required=True, help="Candidate table with cand_0..cand_19 or pred_0..pred_19")
    p.add_argument("--output-dir", default="artifacts/dds_compare")
    p.add_argument("--num-deals", type=int, default=0)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--fail-on-any-mismatch", action="store_true")
    return p.parse_args()


def _resolve_cols(df: pd.DataFrame, preferred: str) -> List[str]:
    cols = [f"{preferred}_{i}" for i in range(20)]
    if all(c in df.columns for c in cols):
        return cols
    raise ValueError(f"Missing required columns for prefix '{preferred}'")


def _resolve_candidate_cols(df: pd.DataFrame) -> List[str]:
    cand = [f"cand_{i}" for i in range(20)]
    pred = [f"pred_{i}" for i in range(20)]
    if all(c in df.columns for c in cand):
        return cand
    if all(c in df.columns for c in pred):
        return pred
    raise ValueError("Candidate file must contain cand_0..cand_19 or pred_0..pred_19")


def _subset(cpu: pd.DataFrame, cand: pd.DataFrame, num_deals: int, seed: int) -> Tuple[pd.DataFrame, pd.DataFrame]:
    n = min(len(cpu), len(cand))
    cpu = cpu.iloc[:n].reset_index(drop=True)
    cand = cand.iloc[:n].reset_index(drop=True)
    if num_deals > 0 and num_deals < n:
        idx = np.random.default_rng(seed).choice(n, size=num_deals, replace=False)
        idx = np.sort(idx)
        cpu = cpu.iloc[idx].reset_index(drop=True)
        cand = cand.iloc[idx].reset_index(drop=True)
    return cpu, cand


def main() -> None:
    args = _parse_args()
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    cpu_df = _read_table(Path(args.cpu_file))
    cand_df = _read_table(Path(args.candidate_file))
    cpu_df, cand_df = _subset(cpu_df, cand_df, args.num_deals, args.seed)

    cpu_cols = _resolve_cols(cpu_df, "cpu")
    cand_cols = _resolve_candidate_cols(cand_df)

    cpu = cpu_df[cpu_cols].to_numpy(dtype=np.int64)
    cand = cand_df[cand_cols].to_numpy(dtype=np.int64)
    delta = cand - cpu
    abs_delta = np.abs(delta)

    exact_match_rate = float((delta == 0).mean())
    mae = float(abs_delta.mean())
    mismatch_count = int((delta != 0).sum())

    summary = {
        "num_deals": int(cpu.shape[0]),
        "num_cells": int(cpu.size),
        "exact_match_rate": exact_match_rate,
        "mae": mae,
        "mismatch_count": mismatch_count,
    }

    mismatch_rows = []
    for deal_idx in range(cpu.shape[0]):
        for k in range(20):
            if delta[deal_idx, k] != 0:
                mismatch_rows.append(
                    {
                        "deal_idx": deal_idx,
                        "combo_idx": k,
                        "cpu_value": int(cpu[deal_idx, k]),
                        "candidate_value": int(cand[deal_idx, k]),
                        "delta": int(delta[deal_idx, k]),
                    }
                )
    mismatch_df = pd.DataFrame(mismatch_rows)
    mismatch_path = _safe_write_parquet(mismatch_df, out_dir / "dds_compare_mismatches.parquet")

    hist = {
        "delta_histogram": {
            str(int(v)): int((delta == v).sum())
            for v in range(int(delta.min()), int(delta.max()) + 1)
        },
        "abs_delta_histogram": {
            str(int(v)): int((abs_delta == v).sum())
            for v in range(int(abs_delta.max()) + 1)
        },
    }

    _write_json(summary, out_dir / "dds_compare_summary.json")
    _write_json(hist, out_dir / "dds_compare_histograms.json")

    fail = False
    fail_reasons: List[str] = []
    if args.fail_on_any_mismatch and mismatch_count > 0:
        fail = True
        fail_reasons.append(
            f"Mismatch count {mismatch_count} > 0 with --fail-on-any-mismatch"
        )

    if fail_reasons:
        summary["policy_fail_reasons"] = fail_reasons
        _write_json(summary, out_dir / "dds_compare_summary.json")

    print(json.dumps(summary, indent=2))
    print(f"Mismatch report: {mismatch_path}")

    if fail:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
