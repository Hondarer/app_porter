#!/usr/bin/env python3
"""porter-bench の結果 CSV から、条件と指標ごとの min、median、max を 1 つの CSV へまとめる。

結果ディレクトリ配下の `*.csv` を再帰的に読み、`run` 列が min、median、max の行だけを集める。
同じ (os, version, cond, params, metric) の行が複数ある場合は、後から読んだ行を採用する。

使い方: python3 porter_bench_aggregate.py <結果ディレクトリ> <出力 CSV>

詳細は app/porter/docs/performance-measurement.md を参照する。
"""

from __future__ import annotations

import argparse
import csv
import os
import sys
from pathlib import Path
from typing import Sequence

# 埋め込み版 Python (._pth) はスクリプトのディレクトリを sys.path に入れないため、明示的に追加する。
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from porter_bench_common import STATISTIC_RUNS, configure_output_encoding, read_result_rows  # noqa: E402

AggregateKey = tuple[str, str, str, str, str]


def collect_statistics(results_directory: Path, excluded: Path | None = None) -> dict[AggregateKey, dict[str, str]]:
    """結果ディレクトリ配下の CSV から、キーごとの min、median、max を集める。"""
    rows: dict[AggregateKey, dict[str, str]] = {}
    for path in sorted(results_directory.rglob("*.csv")):
        if excluded is not None and path.resolve() == excluded.resolve():
            continue
        for record in read_result_rows(path):
            if record.get("run") not in STATISTIC_RUNS:
                continue
            key = (record["os"], record["version"], record["cond"], record["params"], record["metric"])
            rows.setdefault(key, {})[record["run"]] = record["value"]
    return rows


def write_aggregate(rows: dict[AggregateKey, dict[str, str]], output_path: Path) -> None:
    """集めた統計量をキーの順に CSV へ書き出す。"""
    with output_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["os", "version", "cond", "params", "metric", "min", "median", "max"])
        for key in sorted(rows):
            statistics = rows[key]
            writer.writerow([*key, statistics.get("min", ""), statistics.get("median", ""), statistics.get("max", "")])


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="porter-bench の結果 CSV を 1 つの CSV へまとめる。")
    parser.add_argument("results_directory", type=Path, help="結果の CSV を置いたディレクトリ")
    parser.add_argument("output_csv", type=Path, help="出力する CSV のパス")
    args = parser.parse_args(argv)

    if not args.results_directory.is_dir():
        print(f"結果ディレクトリが見つかりません: {args.results_directory}", file=sys.stderr)
        return 2
    rows = collect_statistics(args.results_directory, args.output_csv)
    write_aggregate(rows, args.output_csv)
    print(f"{len(rows)} 行を {args.output_csv} へ出力しました。")
    return 0


if __name__ == "__main__":
    configure_output_encoding()
    sys.exit(main())
