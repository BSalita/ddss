"""
DDS backend compare harness.

Implements the proposal's compare-mode artifacts and fail-fast gates.
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
    p = argparse.ArgumentParser(description="DDS CPU vs candidate compare runner")
    p.add_argument("--dds-backend", choices=["cpu", "compare"], default="compare")
    p.add_argument("--compare-target", choices=["fast", "hybrid", "exact"], default="fast")
    p.add_argument("--cpu-file", required=True, help="CPU baseline table with cpu_0..cpu_19 columns")
    p.add_argument("--candidate-file", required=True, help="Candidate table with cand_0..cand_19 or pred_0..pred_19")
    p.add_argument("--output-dir", default="artifacts/dds_compare")
    p.add_argument("--num-deals", type=int, default=0)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--report-error-analysis", choices=["off", "basic", "full"], default="full")

    # Exact policy
    p.add_argument("--fail-on-any-exact-mismatch", action="store_true")

    # Fast policy
    p.add_argument("--max-off-by-one-rate", type=float, default=0.03)
    p.add_argument("--max-off-by-two-rate", type=float, default=0.001)
    p.add_argument("--max-contract-disagreement-rate", type=float, default=0.02)
    p.add_argument("--max-par-disagreement-rate", type=float, default=0.02)
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


def _derive_contract_value(vec: np.ndarray) -> int:
    # Simple contract proxy: best value across all 20 strain/declarer cells.
    return int(np.max(vec))


def _derive_par_value(vec: np.ndarray) -> int:
    # Cheap par proxy in absence of full scoring context.
    return int(np.max(vec) - np.min(vec))


def _fast_error_stats(delta: np.ndarray) -> Dict:
    abs_delta = np.abs(delta)
    stats = {
        "mean_error": float(delta.mean()),
        "median_error": float(np.median(delta)),
        "std_error": float(delta.std()),
        "min_error": int(delta.min()),
        "max_error": int(delta.max()),
        "percentiles": {
            "p5": float(np.percentile(delta, 5)),
            "p25": float(np.percentile(delta, 25)),
            "p50": float(np.percentile(delta, 50)),
            "p75": float(np.percentile(delta, 75)),
            "p95": float(np.percentile(delta, 95)),
            "p99": float(np.percentile(delta, 99)),
        },
        "mae": float(abs_delta.mean()),
        "rmse": float(math.sqrt(np.square(delta).mean())),
        "abs_error_quartiles": {
            "q1": float(np.percentile(abs_delta, 25)),
            "q2": float(np.percentile(abs_delta, 50)),
            "q3": float(np.percentile(abs_delta, 75)),
        },
        "tail_rates": {
            "|delta|>=1": float((abs_delta >= 1).mean()),
            "|delta|>=2": float((abs_delta >= 2).mean()),
            "|delta|>=3": float((abs_delta >= 3).mean()),
        },
    }
    return stats


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
    off_by_one_rate = float((abs_delta == 1).mean())
    off_by_two_plus_rate = float((abs_delta >= 2).mean())

    per_deal_contract_disagree = []
    per_deal_par_disagree = []
    for i in range(cpu.shape[0]):
        c_cpu = _derive_contract_value(cpu[i])
        c_cand = _derive_contract_value(cand[i])
        p_cpu = _derive_par_value(cpu[i])
        p_cand = _derive_par_value(cand[i])
        per_deal_contract_disagree.append(int(c_cpu != c_cand))
        per_deal_par_disagree.append(int(p_cpu != p_cand))

    contract_disagreement_rate = float(np.mean(per_deal_contract_disagree))
    par_disagreement_rate = float(np.mean(per_deal_par_disagree))

    summary = {
        "dds_backend": args.dds_backend,
        "compare_target": args.compare_target,
        "num_deals": int(cpu.shape[0]),
        "num_cells": int(cpu.size),
        "exact_match_rate": exact_match_rate,
        "mae": mae,
        "off_by_one_rate": off_by_one_rate,
        "off_by_two_plus_rate": off_by_two_plus_rate,
        "contract_disagreement_rate": contract_disagreement_rate,
        "par_disagreement_rate": par_disagreement_rate,
    }

    # Mismatch table
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
    if args.compare_target == "exact":
        if args.fail_on_any_exact_mismatch and len(mismatch_df) > 0:
            fail = True
            fail_reasons.append(
                f"Exact mismatch count {len(mismatch_df)} > 0 with --fail-on-any-exact-mismatch"
            )
    else:
        if off_by_one_rate > args.max_off_by_one_rate:
            fail = True
            fail_reasons.append(
                f"off_by_one_rate {off_by_one_rate:.6f} exceeds threshold {args.max_off_by_one_rate:.6f}"
            )
        if off_by_two_plus_rate > args.max_off_by_two_rate:
            fail = True
            fail_reasons.append(
                f"off_by_two_plus_rate {off_by_two_plus_rate:.6f} exceeds threshold {args.max_off_by_two_rate:.6f}"
            )
        if contract_disagreement_rate > args.max_contract_disagreement_rate:
            fail = True
            fail_reasons.append(
                "contract_disagreement_rate "
                f"{contract_disagreement_rate:.6f} exceeds threshold {args.max_contract_disagreement_rate:.6f}"
            )
        if par_disagreement_rate > args.max_par_disagreement_rate:
            fail = True
            fail_reasons.append(
                f"par_disagreement_rate {par_disagreement_rate:.6f} exceeds threshold {args.max_par_disagreement_rate:.6f}"
            )

        if args.report_error_analysis != "off":
            fast_stats = _fast_error_stats(delta.flatten())
            _write_json(fast_stats, out_dir / "dds_compare_stats.json")

            # 14x14 confusion matrix over trick values 0..13.
            matrix = np.zeros((14, 14), dtype=np.int64)
            clipped_cpu = np.clip(cpu, 0, 13)
            clipped_cand = np.clip(cand, 0, 13)
            for truth, pred in zip(clipped_cpu.flatten(), clipped_cand.flatten()):
                matrix[int(truth), int(pred)] += 1

            cm_df = pd.DataFrame(matrix)
            cm_df.to_csv(out_dir / "dds_compare_confusion_matrix.csv", index=False)

            # Slices by strain/declarer over all deals.
            slice_rows = []
            for combo in range(20):
                d = delta[:, combo]
                ad = np.abs(d)
                strain = combo // 4
                declarer = combo % 4
                slice_rows.append(
                    {
                        "combo_idx": combo,
                        "strain": strain,
                        "declarer": declarer,
                        "exact_rate": float((d == 0).mean()),
                        "mae": float(ad.mean()),
                        "off_by_one_rate": float((ad == 1).mean()),
                        "off_by_two_plus_rate": float((ad >= 2).mean()),
                    }
                )
            slice_df = pd.DataFrame(slice_rows)
            _safe_write_parquet(slice_df, out_dir / "dds_compare_slices.parquet")

            # Calibration table if confidence columns are present.
            conf_cols = [f"conf_{i}" for i in range(20)]
            if all(c in cand_df.columns for c in conf_cols):
                conf = cand_df[conf_cols].to_numpy(dtype=np.float32)
                correct = (delta == 0).astype(np.float32)

                bins = np.linspace(0.0, 1.0, 11)
                rows = []
                ece = 0.0
                total = conf.size
                for b in range(10):
                    lo = bins[b]
                    hi = bins[b + 1]
                    mask = (conf >= lo) & (conf < hi if b < 9 else conf <= hi)
                    count = int(mask.sum())
                    if count == 0:
                        continue
                    acc = float(correct[mask].mean())
                    avg_conf = float(conf[mask].mean())
                    ece += abs(acc - avg_conf) * (count / total)
                    rows.append(
                        {
                            "bin_lo": lo,
                            "bin_hi": hi,
                            "count": count,
                            "accuracy": acc,
                            "avg_confidence": avg_conf,
                        }
                    )
                calibration = {"ece": float(ece), "reliability_table": rows}
                _write_json(calibration, out_dir / "dds_compare_calibration.json")

    if fail_reasons:
        summary["policy_fail_reasons"] = fail_reasons
        _write_json(summary, out_dir / "dds_compare_summary.json")

    print(json.dumps(summary, indent=2))
    print(f"Mismatch report: {mismatch_path}")

    if fail:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
