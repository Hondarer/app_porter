#!/usr/bin/env python3
"""porter_bench_*.py の単体テスト。porter-bench の実行ファイルの代わりに、小さな CSV と代用の実行ファイルを使う。"""

from __future__ import annotations

import contextlib
import io
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path

# 埋め込み版 Python (._pth) はスクリプトのディレクトリを sys.path に入れないため、明示的に追加する。
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# 試験対象の bin/ を明示的に追加する。PYTHONPATH には依存しない。
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "bin"))

import porter_bench_aggregate as aggregate  # noqa: E402
import porter_bench_common as common  # noqa: E402
import porter_bench_ab as runner  # noqa: E402
import porter_bench_summarize as summarize  # noqa: E402
import porter_bench_summarize_variants as variants_summary  # noqa: E402

HEADER = "os,version,cond,params,run,metric,value\n"
P5_PARAMS = "tcp_type=oneway;bytes=64;flags=normal;threads=1;count=100;warmup=10"


def csv_text(version: str, values: list[float], metric: str = "throughput_msg_s", cond: str = "P5",
             params: str = P5_PARAMS, os_name: str = "linux") -> str:
    """各回の値と、min、median、max の行を持つ CSV の本文を作る。"""
    lines = [HEADER.rstrip("\n")]
    for index, value in enumerate(values, start=1):
        lines.append(f"{os_name},{version},{cond},{params},{index},{metric},{value:.3f}")
    ordered = sorted(values)
    median = ordered[len(ordered) // 2] if len(ordered) % 2 else (ordered[len(ordered) // 2 - 1] + ordered[len(ordered) // 2]) / 2
    for run, value in (("min", ordered[0]), ("median", median), ("max", ordered[-1])):
        lines.append(f"{os_name},{version},{cond},{params},{run},{metric},{value:.3f}")
    return "\n".join(lines) + "\n"


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="")


class CommonTest(unittest.TestCase):
    def test_short_params_drops_length_parameters(self) -> None:
        self.assertEqual("tcp_type=oneway;bytes=64;flags=normal;threads=1", common.short_params(P5_PARAMS))

    def test_format_change(self) -> None:
        self.assertEqual("+50.0%", common.format_change(150.0, 100.0))
        self.assertEqual("-25.0%", common.format_change(75.0, 100.0))
        self.assertEqual("", common.format_change(1.0, 0.0))
        self.assertEqual("", common.format_change(None, 1.0))

    def test_format_value(self) -> None:
        self.assertEqual("1.235", common.format_value(1.23456))
        self.assertEqual("1234.6", common.format_value(1234.56))
        self.assertEqual("-", common.format_value(None))

    def test_key_metrics_cover_all_conditions(self) -> None:
        self.assertEqual({"P1", "P2", "P3", "P4", "P5"}, set(common.KEY_METRICS))
        self.assertIn("send_full_count", common.KEY_METRICS["P5"])


class AggregateTest(unittest.TestCase):
    def test_collects_statistics_recursively_and_ignores_run_rows(self) -> None:
        with tempfile.TemporaryDirectory() as root_text:
            root = Path(root_text)
            write(root / "base" / "p5.csv", csv_text("base", [100.0, 200.0, 300.0]))
            write(root / "target" / "sub" / "p5.csv", csv_text("target", [150.0, 250.0]))
            rows = aggregate.collect_statistics(root)
        self.assertEqual(2, len({key[1] for key in rows}))
        base_key = ("linux", "base", "P5", P5_PARAMS, "throughput_msg_s")
        self.assertEqual({"min": "100.000", "median": "200.000", "max": "300.000"}, rows[base_key])
        target_key = ("linux", "target", "P5", P5_PARAMS, "throughput_msg_s")
        self.assertEqual("200.000", rows[target_key]["median"])

    def test_main_writes_sorted_csv_and_skips_output_inside_results(self) -> None:
        with tempfile.TemporaryDirectory() as root_text:
            root = Path(root_text)
            write(root / "b.csv", csv_text("target", [1.0, 3.0]))
            write(root / "a.csv", csv_text("base", [2.0, 4.0], os_name="windows"))
            output = root / "aggregate.csv"
            for _ in range(2):
                with contextlib.redirect_stdout(io.StringIO()) as captured:
                    code = aggregate.main([str(root), str(output)])
                self.assertEqual(0, code)
            lines = output.read_text(encoding="utf-8").splitlines()
            self.assertIn("2 行", captured.getvalue())
        self.assertEqual("os,version,cond,params,metric,min,median,max", lines[0])
        self.assertEqual(3, len(lines))
        self.assertTrue(lines[1].startswith("linux,target,"))
        self.assertTrue(lines[2].startswith("windows,base,"))

    def test_main_rejects_missing_directory(self) -> None:
        with tempfile.TemporaryDirectory() as root_text:
            with contextlib.redirect_stderr(io.StringIO()) as captured:
                code = aggregate.main([str(Path(root_text) / "none"), str(Path(root_text) / "out.csv")])
        self.assertEqual(2, code)
        self.assertIn("見つかりません", captured.getvalue())


class SummarizeTest(unittest.TestCase):
    def test_compares_two_versions_with_change_rate(self) -> None:
        with tempfile.TemporaryDirectory() as root_text:
            root = Path(root_text)
            write(root / "base.csv", csv_text("base", [100.0, 200.0, 300.0]))
            write(root / "target.csv", csv_text("target", [200.0, 300.0, 400.0]))
            aggregate_path = root / "aggregate.csv"
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(0, aggregate.main([str(root), str(aggregate_path)]))
            lines = summarize.summarize(aggregate_path, "base", "target")
        self.assertEqual("## linux P5 tcp_type=oneway;bytes=64;flags=normal;threads=1", lines[0])
        self.assertEqual(
            "  throughput_msg_s: base=200.000 [100.000..300.000] | target=300.000 [200.000..400.000] (+50.0%)",
            lines[1],
        )
        # データのない指標は「-」で表示する。
        self.assertIn("  api_p50_us: base=- | target=-", lines)

    def test_missing_version_has_no_change_rate(self) -> None:
        with tempfile.TemporaryDirectory() as root_text:
            root = Path(root_text)
            write(root / "base.csv", csv_text("base", [100.0, 200.0]))
            aggregate_path = root / "aggregate.csv"
            with contextlib.redirect_stdout(io.StringIO()):
                aggregate.main([str(root), str(aggregate_path)])
            lines = summarize.summarize(aggregate_path, "base", "target")
        self.assertEqual("  throughput_msg_s: base=150.000 [100.000..200.000] | target=-", lines[1])

    def test_main_prints_summary_and_rejects_missing_file(self) -> None:
        with tempfile.TemporaryDirectory() as root_text:
            root = Path(root_text)
            write(root / "base.csv", csv_text("base", [10.0]))
            aggregate_path = root / "aggregate.csv"
            with contextlib.redirect_stdout(io.StringIO()):
                aggregate.main([str(root), str(aggregate_path)])
            with contextlib.redirect_stdout(io.StringIO()) as captured:
                code = summarize.main([str(aggregate_path), "--base", "base", "--target", "target"])
            with contextlib.redirect_stderr(io.StringIO()):
                missing = summarize.main([str(root / "none.csv"), "--base", "a", "--target", "b"])
        self.assertEqual(0, code)
        self.assertIn("## linux P5", captured.getvalue())
        self.assertEqual(2, missing)


class SummarizeVariantsTest(unittest.TestCase):
    def test_comparison_pairs(self) -> None:
        self.assertEqual([("b", "a")], variants_summary.comparison_pairs(["a", "b"]))
        self.assertEqual(
            [("b", "a"), ("c", "a"), ("d", "a"), ("c", "b"), ("d", "c")],
            variants_summary.comparison_pairs(["a", "b", "c", "d"]),
        )

    def test_summary_csv_markdown_and_spread(self) -> None:
        with tempfile.TemporaryDirectory() as root_text:
            root = Path(root_text)
            write(root / "base" / "p5-b64.csv", csv_text("base", [100.0, 110.0, 120.0]))
            write(root / "target" / "p5-b64.csv", csv_text("target", [200.0, 220.0, 1000.0]))
            write(root / "target" / "p4-tcp.csv",
                  csv_text("target", [5.0, 6.0], metric="close_p50_us", cond="P4", params="proto=tcp"))
            out_csv = root / "summary.csv"
            out_md = root / "summary.md"
            with contextlib.redirect_stdout(io.StringIO()) as captured:
                code = variants_summary.main([str(root), str(out_csv), str(out_md), "--variants", "base,target"])
            csv_lines = out_csv.read_text(encoding="utf-8").splitlines()
            markdown = out_md.read_text(encoding="utf-8")
        self.assertEqual(0, code)
        self.assertIn("2 行、ばらつきの大きい組み合わせ 1 件", captured.getvalue())
        self.assertEqual(
            "condition,metric,base_median,base_min,base_max,base_n,target_median,target_min,target_max,target_n,target_vs_base",
            csv_lines[0],
        )
        self.assertEqual("p4-tcp,close_p50_us,,,,0,5.5,5.0,6.0,2,", csv_lines[1])
        self.assertEqual("p5-b64,throughput_msg_s,110.0,100.0,120.0,3,220.0,200.0,1000.0,3,+100.0%", csv_lines[2])
        self.assertIn("| p5-b64 | throughput_msg_s | 110.0 (100.0..120.0)", markdown)
        self.assertIn("| +100.0% |", markdown)
        self.assertIn("- p5-b64 throughput_msg_s target: 200.0, 220.0, 1000.0", markdown)

    def test_variants_default_to_sorted_subdirectories(self) -> None:
        with tempfile.TemporaryDirectory() as root_text:
            root = Path(root_text)
            write(root / "b" / "x.csv", csv_text("b", [1.0]))
            write(root / "a" / "x.csv", csv_text("a", [1.0]))
            self.assertEqual(["a", "b"], variants_summary.discover_variants(root))

    def test_requires_two_variants(self) -> None:
        with tempfile.TemporaryDirectory() as root_text:
            root = Path(root_text)
            write(root / "only" / "x.csv", csv_text("only", [1.0]))
            with contextlib.redirect_stderr(io.StringIO()) as captured:
                code = variants_summary.main([str(root), str(root / "s.csv"), str(root / "s.md")])
        self.assertEqual(2, code)
        self.assertIn("2 つ以上", captured.getvalue())


class RunnerTest(unittest.TestCase):
    def test_parse_executable(self) -> None:
        self.assertEqual(("base", Path("/a/b")), runner.parse_executable("base=/a/b"))
        self.assertEqual(("v1.0", Path("C:/x=y/porter-bench.exe")), runner.parse_executable("v1.0=C:/x=y/porter-bench.exe"))
        for text in ("base", "=/a", "base=", "ba se=/a", "a,b=/c"):
            with self.assertRaises(ValueError):
                runner.parse_executable(text)

    def test_matrices(self) -> None:
        self.assertEqual(5, len(runner.build_matrix("basic")))
        tcp = runner.build_matrix("tcp")
        self.assertEqual(18, len(tcp))
        self.assertEqual("p4-tcp", tcp[0][0])
        self.assertEqual("p5-bidir-b64-normal-t1", tcp[-1][0])
        full_names = [name for name, _ in runner.build_matrix("full")]
        self.assertEqual(len(full_names), len(set(full_names)))
        self.assertEqual(22, len(full_names))
        with self.assertRaises(ValueError):
            runner.build_matrix("unknown")

    @unittest.skipUnless(os.name == "posix", "LD_LIBRARY_PATH は POSIX 限定")
    def test_build_environment_prepends_executable_directory(self) -> None:
        with tempfile.TemporaryDirectory() as root_text:
            root = Path(root_text).resolve()
            executable = root / "porter-bench"
            executable.write_text("", encoding="utf-8")
            empty = runner.build_environment(executable, {})
            existing = runner.build_environment(executable, {"LD_LIBRARY_PATH": "/opt/lib", "OTHER": "x"})
        self.assertEqual(str(root), empty["LD_LIBRARY_PATH"])
        self.assertEqual(f"{root}{os.pathsep}/opt/lib", existing["LD_LIBRARY_PATH"])
        self.assertEqual("x", existing["OTHER"])

    def test_build_command(self) -> None:
        # パスの区切り文字は OS によって異なるため、期待値も Path から作る。
        executable = Path("/x/porter-bench")
        output_csv = Path("/o/p1.csv")
        command = runner.build_command(executable, "base", ("--cond", "p1"), 29000, output_csv)
        self.assertEqual(
            [str(executable), "--cond", "p1", "--label", "base", "--port", "29000", "--trace-level", "none",
             "--out", str(output_csv)],
            command,
        )

    @unittest.skipUnless(os.name == "posix", "代用の実行ファイルはシェル スクリプトのため POSIX 限定")
    def test_alternates_versions_per_condition_and_counts_failures(self) -> None:
        with tempfile.TemporaryDirectory() as root_text:
            root = Path(root_text)
            fake_script = (
                "#!/bin/sh\n"
                'label=""; out=""\n'
                'while [ $# -gt 0 ]; do\n'
                '  case "$1" in --label) label="$2";; --out) out="$2";; esac\n'
                '  shift\n'
                'done\n'
                'echo "run $label ${LD_LIBRARY_PATH%%:*}" >> "$FAKE_ORDER_FILE"\n'
                'if [ "$label" = "target" ] && [ -n "$FAKE_FAIL" ]; then exit 3; fi\n'
                'printf "os,version,cond,params,run,metric,value\\n" > "$out"\n'
            )
            executables = []
            for name in ("base", "target"):
                path = root / f"fake-{name}"
                path.write_text(fake_script, encoding="utf-8")
                path.chmod(path.stat().st_mode | stat.S_IXUSR)
                executables.append((name, path))
            order_file = root / "order.txt"
            conditions = runner.build_matrix("basic")[:2]
            previous = {key: os.environ.get(key) for key in ("FAKE_ORDER_FILE", "FAKE_FAIL")}
            os.environ["FAKE_ORDER_FILE"] = str(order_file)
            os.environ.pop("FAKE_FAIL", None)
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    failed = runner.run_matrix(executables, conditions, root / "out", 29000, 30)
                    os.environ["FAKE_FAIL"] = "1"
                    failed_with_error = runner.run_matrix(executables, conditions, root / "out2", 29000, 30)
            finally:
                for key, value in previous.items():
                    if value is None:
                        os.environ.pop(key, None)
                    else:
                        os.environ[key] = value
            order = order_file.read_text(encoding="utf-8").splitlines()
            log = (root / "out" / "run-log.txt").read_text(encoding="utf-8").splitlines()
            self.assertTrue((root / "out" / "base" / "p1-b64-normal-t1.csv").is_file())
            self.assertTrue((root / "out" / "target" / "p2-b64-ext.csv").is_file())
        self.assertEqual(0, failed)
        self.assertEqual(2, failed_with_error)
        self.assertEqual(
            [f"run base {root.resolve()}", f"run target {root.resolve()}"] * 4,
            order,
        )
        self.assertEqual(4, len(log))
        self.assertIn("\tbase\tp1-b64-normal-t1\texit=0\t", log[0])
        self.assertIn("\ttarget\tp1-b64-normal-t1\texit=0\t", log[1])

    def test_main_rejects_bad_arguments(self) -> None:
        with tempfile.TemporaryDirectory() as root_text:
            root = Path(root_text)
            existing = root / "exe"
            existing.write_text("", encoding="utf-8")
            cases = [
                ["--exe", f"a={existing}", "--out-dir", str(root / "o")],
                ["--exe", f"a={existing}", "--exe", f"a={existing}", "--out-dir", str(root / "o")],
                ["--exe", f"a={existing}", "--exe", f"b={root / 'none'}", "--out-dir", str(root / "o")],
                ["--exe", f"a={existing}", "--exe", "bad", "--out-dir", str(root / "o")],
                ["--exe", f"a={existing}", "--exe", f"b={existing}", "--out-dir", str(root / "o"), "--port", "0"],
            ]
            for argv in cases:
                with contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(2, runner.main(argv), argv)


if __name__ == "__main__":
    unittest.main()
