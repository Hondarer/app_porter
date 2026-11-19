#!/usr/bin/env python3
"""porter-bench の実行ファイルを 2 つ以上受け取り、同じ条件を版ごとに交互に実行する。

変更の前後を比べるために、比べたい 2 つのコミットでそれぞれビルドした porter-bench を
`--exe <版の名前>=<実行ファイルのパス>` で指定する。
条件を外側、版を内側にして逐次実行するため、時間経過による環境の変化が特定の版へ偏らない。
実行ファイルには `--label <版の名前>` を渡すため、結果 CSV の `version` 列に版の名前が入る。
porter は共有ライブラリのため、版ごとに実行ファイルと同じディレクトリへ、その版の共有ライブラリを置く。
Linux では実行ファイルのディレクトリを LD_LIBRARY_PATH の先頭へ加え、Windows では OS が実行ファイルのディレクトリを先に探す。

結果は `<出力ディレクトリ>/<版の名前>/<条件名>.{csv,out,err}` に置く。
実行の記録は `<出力ディレクトリ>/run-log.txt` に追記する。

使い方:
  python3 porter_bench_ab.py --exe base=/path/to/base/porter-bench --exe target=/path/to/target/porter-bench \\
      --out-dir results --matrix basic --port 29000

終了コードは、すべての実行が成功した場合は 0、失敗または時間切れがあった場合は 1、引数の誤りは 2 とする。

詳細は app/porter/docs/performance-measurement.md を参照する。
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Sequence

# 埋め込み版 Python (._pth) はスクリプトのディレクトリを sys.path に入れないため、明示的に追加する。
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from porter_bench_common import configure_output_encoding  # noqa: E402

# 版の名前に使える文字。porter-bench の --label と同じ規則にする。
VERSION_NAME_PATTERN = re.compile(r"^[A-Za-z0-9._-]+$")

# 1 条件の実行を打ち切るまでの既定の時間 [s]。
DEFAULT_TIMEOUT_SECONDS = 900

# 版を内側で回すため、1 条件は (条件名, porter-bench へ渡す引数) の組で表す。
Condition = tuple[str, tuple[str, ...]]

# 各測定条件を 1 つずつ測る行列。
BASIC_MATRIX: tuple[Condition, ...] = (
    ("p1-b64-normal-t1", ("--cond", "p1")),
    ("p2-b64-ext", ("--cond", "p2")),
    ("p3-b5000-cb1ms", ("--cond", "p3")),
    ("p4-udp", ("--cond", "p4", "--proto", "udp")),
    ("p5-b64-normal-t1", ("--cond", "p5", "--tcp-type", "oneway")),
)


def build_tcp_matrix() -> tuple[Condition, ...]:
    """TCP の送信方式を、ペイロード長、送信フラグ、スレッド数の組み合わせで測る行列を作る。"""
    conditions: list[Condition] = [("p4-tcp", ("--cond", "p4", "--proto", "tcp", "--tcp-type", "oneway"))]
    for payload_bytes in (64, 5000):
        for flags in ("normal", "compress", "blocking", "compress_blocking"):
            for threads in (1, 4):
                conditions.append(
                    (
                        f"p5-b{payload_bytes}-{flags}-t{threads}",
                        (
                            "--cond", "p5", "--tcp-type", "oneway", "--bytes", str(payload_bytes),
                            "--flags", flags, "--threads", str(threads),
                        ),
                    )
                )
    conditions.append(("p5-bidir-b64-normal-t1", ("--cond", "p5", "--tcp-type", "bidir", "--bytes", "64")))
    return tuple(conditions)


def build_matrix(name: str) -> tuple[Condition, ...]:
    """行列の名前から条件の並びを返す。full は basic と tcp を、条件名の重複を除いてつなぐ。"""
    if name == "basic":
        return BASIC_MATRIX
    if name == "tcp":
        return build_tcp_matrix()
    if name == "full":
        seen = set()
        merged: list[Condition] = []
        for condition in (*BASIC_MATRIX, *build_tcp_matrix()):
            if condition[0] not in seen:
                seen.add(condition[0])
                merged.append(condition)
        return tuple(merged)
    raise ValueError(f"未知の行列です: {name}")


def parse_executable(text: str) -> tuple[str, Path]:
    """`<版の名前>=<パス>` を分解する。形式が誤っている場合は ValueError を送出する。"""
    name, separator, path_text = text.partition("=")
    if not separator or not path_text or VERSION_NAME_PATTERN.match(name) is None:
        raise ValueError(f"--exe は <版の名前>=<パス> の形式で指定してください (版の名前は英数字と . _ - のみ): {text}")
    return name, Path(path_text)


def build_command(
    executable: Path, version: str, condition_arguments: Sequence[str], port: int, output_csv: Path
) -> list[str]:
    """porter-bench を起動するコマンド ラインを作る。トレースは測定へ影響するため出力しない。"""
    return [
        str(executable),
        *condition_arguments,
        "--label",
        version,
        "--port",
        str(port),
        "--trace-level",
        "none",
        "--out",
        str(output_csv),
    ]


def build_environment(executable: Path, base: dict[str, str] | None = None) -> dict[str, str]:
    """実行ファイルのディレクトリを共有ライブラリの探索先の先頭へ加えた環境変数を返す。

    Linux では LD_LIBRARY_PATH の先頭へ加える。Windows は実行ファイルのディレクトリを先に探すため、変更しない。
    see: https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order
    """
    environment = dict(os.environ if base is None else base)
    if os.name == "posix":
        directory = str(executable.resolve().parent)
        existing = environment.get("LD_LIBRARY_PATH", "")
        environment["LD_LIBRARY_PATH"] = f"{directory}{os.pathsep}{existing}" if existing else directory
    return environment


def run_condition(
    command: Sequence[str],
    stdout_path: Path,
    stderr_path: Path,
    timeout_seconds: int,
    environment: dict[str, str] | None = None,
) -> str:
    """1 回の実行を行い、終了コードまたは timeout を文字列で返す。"""
    with stdout_path.open("wb") as stdout_stream, stderr_path.open("wb") as stderr_stream:
        try:
            completed = subprocess.run(
                list(command),
                stdout=stdout_stream,
                stderr=stderr_stream,
                timeout=timeout_seconds,
                check=False,
                env=environment,
            )
        except subprocess.TimeoutExpired:
            return "timeout"
    return str(completed.returncode)


def run_matrix(
    executables: Sequence[tuple[str, Path]],
    conditions: Sequence[Condition],
    out_directory: Path,
    port: int,
    timeout_seconds: int,
) -> int:
    """条件を外側、版を内側にして実行し、失敗した実行の数を返す。"""
    out_directory.mkdir(parents=True, exist_ok=True)
    log_path = out_directory / "run-log.txt"
    failed = 0
    for condition_name, condition_arguments in conditions:
        for version, executable in executables:
            version_directory = out_directory / version
            version_directory.mkdir(parents=True, exist_ok=True)
            output_csv = version_directory / f"{condition_name}.csv"
            command = build_command(executable, version, condition_arguments, port, output_csv)
            started = time.monotonic()
            result = run_condition(
                command,
                version_directory / f"{condition_name}.out",
                version_directory / f"{condition_name}.err",
                timeout_seconds,
                build_environment(executable),
            )
            elapsed = time.monotonic() - started
            if result != "0":
                failed += 1
            line = (
                f"{datetime.now().strftime('%H:%M:%S')}\t{version}\t{condition_name}\texit={result}"
                f"\tsec={elapsed:.1f}\t{' '.join(command[1:])}"
            )
            with log_path.open("a", encoding="utf-8") as log_stream:
                log_stream.write(line + "\n")
            print(line)
    return failed


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="porter-bench の実行ファイルを、同じ条件で版ごとに交互に実行する。")
    parser.add_argument("--exe", action="append", required=True, metavar="NAME=PATH",
                        help="版の名前と実行ファイルのパス。2 回以上指定する (先頭が基準)")
    parser.add_argument("--out-dir", type=Path, required=True, help="結果の出力ディレクトリ")
    parser.add_argument("--matrix", choices=("basic", "tcp", "full"), default="basic", help="測定する条件の行列")
    parser.add_argument("--port", type=int, default=29000, help="ループバックの基準ポート番号")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_SECONDS, help="1 回の実行の制限時間 [s]")
    args = parser.parse_args(argv)

    try:
        executables = [parse_executable(text) for text in args.exe]
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    if len(executables) < 2:
        print("--exe は 2 回以上指定してください。", file=sys.stderr)
        return 2
    names = [name for name, _ in executables]
    if len(set(names)) != len(names):
        print("版の名前が重複しています。", file=sys.stderr)
        return 2
    for name, path in executables:
        if not path.is_file():
            print(f"実行ファイルが見つかりません ({name}): {path}", file=sys.stderr)
            return 2
    if not 1 <= args.port <= 60000 or args.timeout < 1:
        print("--port は 1 から 60000、--timeout は 1 以上で指定してください。", file=sys.stderr)
        return 2

    failed = run_matrix(executables, build_matrix(args.matrix), args.out_dir, args.port, args.timeout)
    return 1 if failed else 0


if __name__ == "__main__":
    configure_output_encoding()
    sys.exit(main())
