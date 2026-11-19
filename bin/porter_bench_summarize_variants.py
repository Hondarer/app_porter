#!/usr/bin/env python3
"""結果ディレクトリ配下の版ごとの CSV から、各回の値の中央値、最小、最大を求めて版を並べる。

結果は `<結果ディレクトリ>/<版>/<条件名>.csv` の配置とする。版はディレクトリ名であり、
`porter_bench_ab.py` の出力がこの配置になる。
各回 (`run` 列が整数の行) の値から、条件と指標ごとに中央値、最小、最大を求める。
先頭の版を基準とし、ほかの版には基準に対する中央値の変化率を付ける。
3 番目以降の版には、直前の版に対する変化率も付ける。
最大が最小の 3 倍を超える組み合わせは、ばらつきの大きい条件として列挙する。

使い方:
  python3 porter_bench_summarize_variants.py <結果ディレクトリ> <出力 CSV> <出力 Markdown> [--variants 版1,版2,...]

--variants を省略した場合は、結果ディレクトリ直下のサブディレクトリを名前順に使う。

詳細は app/porter/docs/performance-measurement.md を参照する。
"""

from __future__ import annotations

import argparse
import csv
import os
import statistics
import sys
from pathlib import Path
from typing import Sequence

# 埋め込み版 Python (._pth) はスクリプトのディレクトリを sys.path に入れないため、明示的に追加する。
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from porter_bench_common import KEY_METRICS, configure_output_encoding, format_change, format_value, read_result_rows  # noqa: E402

# 最大が最小のこの倍率を超える組み合わせを、ばらつきが大きいと判定する。
SPREAD_RATIO = 3.0

# (条件名, 指標) から、版ごとの各回の値への表。
RunValues = dict[tuple[str, str], dict[str, list[float]]]


def discover_variants(results_directory: Path) -> list[str]:
    """結果ディレクトリ直下のサブディレクトリ名を、名前順で返す。"""
    return sorted(entry.name for entry in results_directory.iterdir() if entry.is_dir())


def comparison_pairs(variants: Sequence[str]) -> list[tuple[str, str]]:
    """(比べる版, 基準の版) の組を返す。先頭の版との比較に続けて、3 番目以降は直前の版との比較を並べる。"""
    pairs = [(variant, variants[0]) for variant in variants[1:]]
    pairs += [(variants[index], variants[index - 1]) for index in range(2, len(variants))]
    return pairs


def collect_run_values(results_directory: Path, variants: Sequence[str]) -> tuple[RunValues, dict[str, str]]:
    """各回の値を集める。条件名ごとの条件 (P1 から P5) も返す。"""
    runs: RunValues = {}
    conditions: dict[str, str] = {}
    for variant in variants:
        for path in sorted((results_directory / variant).glob("*.csv")):
            label = path.stem
            for record in read_result_rows(path):
                if not record.get("run", "").isdigit():
                    continue
                conditions[label] = record["cond"]
                try:
                    value = float(record["value"])
                except ValueError:
                    continue
                runs.setdefault((label, record["metric"]), {}).setdefault(variant, []).append(value)
    return runs, conditions


def summarize_variants(
    results_directory: Path, variants: Sequence[str], output_csv: Path, output_markdown: Path
) -> tuple[int, int]:
    """集計して CSV と Markdown を書き出す。(行数, ばらつきの大きい組み合わせの数) を返す。"""
    pairs = comparison_pairs(variants)
    runs, conditions = collect_run_values(results_directory, variants)

    rows = []
    spread = []
    for label, metric in sorted(runs):
        if metric not in KEY_METRICS.get(conditions[label], []):
            continue
        per_variant = runs[(label, metric)]
        stat: dict[str, tuple[float, float, float, list[float]]] = {}
        for variant in variants:
            values = per_variant.get(variant, [])
            if not values:
                continue
            stat[variant] = (statistics.median(values), min(values), max(values), values)
            lowest = min(values)
            if lowest > 0 and max(values) / lowest > SPREAD_RATIO:
                spread.append((label, metric, variant, values))
        medians = {variant: stat[variant][0] if variant in stat else None for variant in variants}
        rows.append((label, metric, stat, medians))

    with output_csv.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        header = ["condition", "metric"]
        for variant in variants:
            header += [f"{variant}_median", f"{variant}_min", f"{variant}_max", f"{variant}_n"]
        header += [f"{name}_vs_{base}" for name, base in pairs]
        writer.writerow(header)
        for label, metric, stat, medians in rows:
            line: list[object] = [label, metric]
            for variant in variants:
                if variant in stat:
                    line += [stat[variant][0], stat[variant][1], stat[variant][2], len(stat[variant][3])]
                else:
                    line += ["", "", "", 0]
            line += [format_change(medians.get(name), medians.get(base)) for name, base in pairs]
            writer.writerow(line)

    lines = [
        f"# 測定結果の集計 ({results_directory})",
        "",
        "各値は各回の中央値です。括弧内は最小と最大です。変化率は中央値どうしの比です。",
        "",
        "## 主要指標",
        "",
        "| 条件 | 指標 | " + " | ".join(variants) + " | " + " | ".join(f"{name}/{base}" for name, base in pairs) + " |",
        "| --- | --- | " + " | ".join("---" for _ in [*variants, *pairs]) + " |",
    ]
    for label, metric, stat, medians in rows:
        cells = []
        for variant in variants:
            if variant in stat:
                cells.append(
                    f"{format_value(stat[variant][0])} ({format_value(stat[variant][1])}..{format_value(stat[variant][2])})"
                )
            else:
                cells.append("-")
        ratios = " | ".join(format_change(medians.get(name), medians.get(base)) for name, base in pairs)
        lines.append(f"| {label} | {metric} | {' | '.join(cells)} | {ratios} |")
    lines += ["", f"## ばらつきの大きい組み合わせ (最大 / 最小 > {SPREAD_RATIO})", ""]
    if spread:
        for label, metric, variant, values in spread:
            lines.append(f"- {label} {metric} {variant}: {', '.join(format_value(value) for value in values)}")
    else:
        lines.append("該当なし。")
    output_markdown.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return len(rows), len(spread)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="版ごとの結果 CSV から、各回の値の統計量を版を並べて集計する。")
    parser.add_argument("results_directory", type=Path, help="<版>/<条件名>.csv の配置の結果ディレクトリ")
    parser.add_argument("output_csv", type=Path, help="出力する CSV のパス")
    parser.add_argument("output_markdown", type=Path, help="出力する Markdown のパス")
    parser.add_argument("--variants", help="版の一覧 (コンマ区切り。先頭が基準)。省略時はサブディレクトリの名前順")
    args = parser.parse_args(argv)

    if not args.results_directory.is_dir():
        print(f"結果ディレクトリが見つかりません: {args.results_directory}", file=sys.stderr)
        return 2
    if args.variants:
        variants = [name for name in args.variants.split(",") if name]
    else:
        variants = discover_variants(args.results_directory)
    if len(variants) < 2:
        print("比べる版が 2 つ以上必要です。", file=sys.stderr)
        return 2

    row_count, spread_count = summarize_variants(args.results_directory, variants, args.output_csv, args.output_markdown)
    print(f"{row_count} 行、ばらつきの大きい組み合わせ {spread_count} 件を {args.output_csv} と {args.output_markdown} へ出力しました。")
    return 0


if __name__ == "__main__":
    configure_output_encoding()
    sys.exit(main())
