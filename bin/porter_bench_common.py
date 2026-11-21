"""porter の性能測定 (porter-bench) の結果を扱うスクリプトの共通処理。

結果の CSV の列は `os,version,cond,params,run,metric,value` である。
`run` は各回の番号 (1 以上の整数) と、条件全体の `min`、`median`、`max` を取る。
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path
from typing import Iterator

# 条件ごとの主要な指標。要約ではこの順に表示する。
KEY_METRICS: dict[str, list[str]] = {
    "P1": ["throughput_msg_s", "api_p50_us", "api_p95_us", "api_p99_us", "notify_p50_us", "notify_p95_us",
           "notify_p99_us", "cpu_time_ms", "send_full_count"],
    "P2": ["rtt_p50_us", "rtt_p95_us", "rtt_p99_us", "throughput_msg_s", "dropped_count", "send_full_count"],
    "P3": ["notify_p50_us", "notify_p95_us", "notify_p99_us", "notify_max_us", "peak_rss_inc_bytes",
           "peak_priv_inc_bytes", "after1s_rss_inc_bytes", "after1s_priv_inc_bytes", "dropped_count",
           "send_full_count"],
    "P4": ["close_p50_us", "close_p95_us", "close_p99_us", "rss_slope_bytes_per_cycle",
           "priv_slope_bytes_per_cycle", "handles_slope_per_cycle", "threads_slope_per_cycle"],
    "P5": ["throughput_msg_s", "api_p50_us", "api_p95_us", "api_p99_us", "notify_p50_us", "notify_p95_us",
           "notify_p99_us", "cpu_time_ms", "send_full_count"],
    "P6": ["throughput_msg_s", "healthy_received_min", "healthy_disconnect_count", "slow_received_min",
           "slow_disconnect_count", "send_full_count"],
}

# 条件名の表記から取り除く、測定の長さだけを決めるパラメーターの接頭辞。
LENGTH_PARAMETER_PREFIXES = ("count=", "warmup=", "cycles=")

# 各回の値を表す run 列の値。
STATISTIC_RUNS = ("min", "median", "max")


def read_result_rows(path: Path) -> Iterator[dict[str, str]]:
    """porter-bench が出力した CSV を 1 行ずつ辞書で返す。"""
    with path.open(encoding="utf-8", newline="") as stream:
        yield from csv.DictReader(stream)


def short_params(params: str) -> str:
    """測定の長さだけを決めるパラメーターを除いた、条件の表記を返す。"""
    return ";".join(item for item in params.split(";") if not item.startswith(LENGTH_PARAMETER_PREFIXES))


def format_change(new: float | None, base: float | None) -> str:
    """基準に対する変化率を符号付きの百分率で返す。基準が 0 または欠けている場合は空文字列を返す。"""
    if new is None or base is None or base == 0:
        return ""
    return f"{(new / base - 1) * 100:+.1f}%"


def format_value(value: float | None) -> str:
    """表示用に値を整形する。100 未満は小数 3 桁、それ以上は小数 1 桁とする。"""
    if value is None:
        return "-"
    return f"{value:.3f}" if abs(value) < 100 else f"{value:.1f}"


def configure_output_encoding() -> None:
    """標準出力と標準エラー出力の文字コードを UTF-8 にする。コマンドとして起動する場合に呼ぶ。"""
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
