#!/usr/bin/env python3
"""porter_bench_aggregate.py の出力から、条件ごとに主要な指標の中央値を 2 つの版で並べる。

版は結果 CSV の `version` 列の値 (porter-bench の `--label`) で指定する。
各指標は「版の名前=中央値 [最小..最大]」の形で表示し、基準に対する中央値の変化率を付ける。

使い方: python3 porter_bench_summarize.py <集計 CSV> --base <基準の版> --target <比べる版>

詳細は app/porter/docs/performance-measurement.md を参照する。
"""

from __future__ import annotations

import argparse
import csv
import os
import sys
from collections import defaultdict
from pathlib import Path
from typing import Sequence

# 埋め込み版 Python (._pth) はスクリプトのディレクトリを sys.path に入れないため、明示的に追加する。
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from porter_bench_common import KEY_METRICS, configure_output_encoding, format_change, short_params  # noqa: E402

Statistics = tuple[str, str, str]


def load_aggregate(path: Path) -> dict[tuple[str, str, str], dict[tuple[str, str], Statistics]]:
    """集計 CSV を、(os, cond, params) ごとの (version, metric) から (min, median, max) への表へ読み込む。"""
    data: dict[tuple[str, str, str], dict[tuple[str, str], Statistics]] = defaultdict(dict)
    with path.open(encoding="utf-8", newline="") as stream:
        for record in csv.DictReader(stream):
            group = (record["os"], record["cond"], record["params"])
            data[group][(record["version"], record["metric"])] = (record["min"], record["median"], record["max"])
    return data


def summarize(path: Path, base: str, target: str) -> list[str]:
    """基準と比べる版を並べた要約の行を返す。"""
    data = load_aggregate(path)
    lines: list[str] = []
    for group in sorted(data):
        os_name, cond, params = group
        statistics = data[group]
        lines.append(f"## {os_name} {cond} {short_params(params)}")
        for metric in KEY_METRICS.get(cond, []):
            cells = []
            for version in (base, target):
                found = statistics.get((version, metric))
                cells.append(f"{version}={found[1]} [{found[0]}..{found[2]}]" if found else f"{version}=-")
            base_stat = statistics.get((base, metric))
            target_stat = statistics.get((target, metric))
            change = ""
            if base_stat and target_stat:
                try:
                    change = format_change(float(target_stat[1]), float(base_stat[1]))
                except ValueError:
                    change = ""
            suffix = f" ({change})" if change else ""
            lines.append(f"  {metric}: {' | '.join(cells)}{suffix}")
    return lines


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="集計 CSV から、基準と比べる版の主要な指標を並べる。")
    parser.add_argument("aggregate_csv", type=Path, help="porter_bench_aggregate.py の出力")
    parser.add_argument("--base", required=True, help="基準とする版の名前 (version 列の値)")
    parser.add_argument("--target", required=True, help="比べる版の名前 (version 列の値)")
    args = parser.parse_args(argv)

    if not args.aggregate_csv.is_file():
        print(f"集計 CSV が見つかりません: {args.aggregate_csv}", file=sys.stderr)
        return 2
    for line in summarize(args.aggregate_csv, args.base, args.target):
        print(line)
    return 0


if __name__ == "__main__":
    configure_output_encoding()
    sys.exit(main())
