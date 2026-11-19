#!/usr/bin/env python3
"""interop_matrix.py の単体テスト。プロセスは起動しない。"""

from __future__ import annotations

import contextlib
import dataclasses
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path, PurePosixPath, PureWindowsPath

# 埋め込み版 Python (._pth) はスクリプトのディレクトリを sys.path に入れないため、明示的に追加する。
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# 試験対象の bin/ を明示的に追加する。PYTHONPATH には依存しない。
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "bin"))

import interop_matrix as matrix  # noqa: E402

def make_settings(
    windows_root: str = r"D:\repos\kit",
    wsl_root: str = "/work/kit",
    output_directory: Path = Path("/tmp/out"),
    selected_cases: tuple[str, ...] = (),
) -> matrix.InteropSettings:
    return matrix.InteropSettings(
        windows_root=PureWindowsPath(windows_root),
        wsl_root=PurePosixPath(wsl_root),
        wsl_distribution="TestDistro",
        windows_address="10.0.0.1",
        wsl_address="10.0.0.2",
        output_directory=output_directory,
        selected_cases=selected_cases,
    )


def find_case(label: str) -> matrix.InteropCase:
    return next(case for case in matrix.all_cases() if case.label == label)


class AllCasesTest(unittest.TestCase):
    def test_twelve_cases_in_order(self) -> None:
        labels = [case.label for case in matrix.all_cases()]
        self.assertEqual(12, len(labels))
        self.assertEqual("psk/unicast", labels[0])
        self.assertEqual("psk/multicast", labels[5])
        self.assertEqual("cert/unicast", labels[6])
        self.assertEqual("cert/multicast", labels[11])

    def test_service_id_and_port(self) -> None:
        case = find_case("cert/tcp")
        self.assertEqual(9, case.index)
        self.assertEqual(78109, case.service_id)
        self.assertEqual(59108, case.port)

    def test_select_cases_filters_without_changing_ids(self) -> None:
        settings = make_settings(selected_cases=("cert/tcp", "psk/unicast"))
        selected = matrix.select_cases(settings)
        self.assertEqual(["psk/unicast", "cert/tcp"], [case.label for case in selected])
        self.assertEqual(78109, selected[1].service_id)

    def test_select_cases_without_filter_returns_all(self) -> None:
        self.assertEqual(12, len(matrix.select_cases(make_settings())))


class ToWslPathTest(unittest.TestCase):
    def test_drive_letter_is_lowercased(self) -> None:
        self.assertEqual("/mnt/d/Users/a/b.json", matrix.to_wsl_path(r"D:\Users\a\b.json"))

    def test_pure_windows_path(self) -> None:
        self.assertEqual("/mnt/c/Temp/x", matrix.to_wsl_path(PureWindowsPath(r"C:\Temp\x")))

    def test_forward_slash_input(self) -> None:
        self.assertEqual("/mnt/e/a/b", matrix.to_wsl_path("E:/a/b"))


class BuildServiceConfigTest(unittest.TestCase):
    def service_of(self, label: str, side: str, settings=None) -> dict:
        settings = settings or make_settings()
        case = find_case(label)
        config = matrix.build_service_config(settings, case, side)
        return config["services"][str(case.service_id)]

    def test_global_section(self) -> None:
        config = matrix.build_service_config(make_settings(), find_case("psk/unicast"), "windows")
        self.assertEqual(
            {
                "udp_health_interval_ms": 500,
                "udp_health_timeout_ms": 5000,
                "tcp_health_interval_ms": 500,
                "tcp_health_timeout_ms": 5000,
                "tcp_close_timeout_ms": 1000,
            },
            config["global"],
        )

    def test_unicast_psk(self) -> None:
        expected = {
            "type": "unicast",
            "src_addr1": "10.0.0.1",
            "src_port": 59000,
            "dst_addr1": "10.0.0.2",
            "dst_port": 59001,
            "auth": "psk",
            "encrypt_key": matrix.PRE_SHARED_KEY,
        }
        self.assertEqual(expected, self.service_of("psk/unicast", "windows"))
        self.assertEqual(expected, self.service_of("psk/unicast", "wsl"))

    def test_n1_differs_by_side(self) -> None:
        windows_side = self.service_of("psk/unicast_bidir_n1", "windows")
        wsl_side = self.service_of("psk/unicast_bidir_n1", "wsl")
        self.assertEqual("unicast_bidir", windows_side["type"])
        self.assertEqual("10.0.0.1", windows_side["src_addr1"])
        self.assertEqual("unicast_bidir_n1", wsl_side["type"])
        self.assertEqual(4, wsl_side["max_peers"])
        self.assertNotIn("src_addr1", wsl_side)
        self.assertEqual(59025, wsl_side["dst_port"])

    def test_tcp_has_only_destination(self) -> None:
        for label in ("psk/tcp", "psk/tcp_bidir"):
            service = self.service_of(label, "windows")
            self.assertNotIn("src_addr1", service)
            self.assertEqual("10.0.0.2", service["dst_addr1"])

    def test_multicast_group(self) -> None:
        service = self.service_of("psk/multicast", "wsl")
        self.assertEqual("239.255.105.106", service["multicast_group"])
        self.assertEqual("multicast", service["type"])
        self.assertEqual(service, self.service_of("psk/multicast", "windows"))

    def test_cert_windows_side_uses_windows_paths(self) -> None:
        service = self.service_of("cert/tcp", "windows")
        base = r"D:\repos\kit\app\porter\test\src\libporter\auth_test\data"
        self.assertEqual("cert", service["auth"])
        self.assertEqual(base + r"\ca.pem", service["ca_file"])
        self.assertEqual(base + r"\server.pem", service["cert_file"])
        self.assertEqual(base + r"\server-key.pem", service["key_file"])
        self.assertEqual(["porter.test"], service["peer_names"])
        self.assertNotIn("encrypt_key", service)

    def test_cert_wsl_side_uses_posix_paths(self) -> None:
        service = self.service_of("cert/tcp", "wsl")
        base = "/work/kit/app/porter/test/src/libporter/auth_test/data"
        self.assertEqual(base + "/ca.pem", service["ca_file"])
        self.assertEqual(base + "/client.pem", service["cert_file"])
        self.assertEqual(base + "/client-key.pem", service["key_file"])
        self.assertEqual(["urn:porter:peer"], service["peer_names"])

    def test_unknown_side_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            matrix.build_service_config(make_settings(), find_case("psk/tcp"), "linux")


class ConfigFilePathTest(unittest.TestCase):
    def test_name_uses_short_side_name(self) -> None:
        settings = make_settings(output_directory=Path("/tmp/out"))
        path = matrix.config_file_path(settings, find_case("cert/tcp_bidir"), "windows")
        self.assertEqual(Path("/tmp/out/cert-tcp_bidir-win.json"), path)


class BuildCommandTest(unittest.TestCase):
    def test_windows_receiver(self) -> None:
        settings = make_settings()
        command = matrix.build_command(
            settings, "windows", "receiver", PureWindowsPath(r"C:\out\a.json"), 78100
        )
        self.assertEqual(
            [
                r"D:\repos\kit\app\porter\prod\cbin\porter-test.exe",
                "-l", "INFO", "receiver", r"C:\out\a.json", "78100",
            ],
            command,
        )

    def test_wsl_receiver(self) -> None:
        settings = make_settings()
        command = matrix.build_command(
            settings, "wsl", "receiver", PureWindowsPath(r"C:\out\a.json"), 78100
        )
        library_path = ":".join(
            f"/work/kit/app/{name}/prod/lib" for name in ("cplat", "porter", "cjson", "zlib", "openssl")
        )
        self.assertEqual(
            ["wsl.exe", "-d", "TestDistro", "--", "bash", "-lc",
             f"LD_LIBRARY_PATH={library_path} /work/kit/app/porter/prod/cbin/porter-test"
             " -l INFO receiver /mnt/c/out/a.json 78100"],
            command,
        )

    def test_wsl_sender_role(self) -> None:
        command = matrix.build_command(
            make_settings(), "wsl", "sender", PureWindowsPath(r"D:\o\b.json"), 78101
        )
        self.assertTrue(command[-1].endswith(" -l INFO sender /mnt/d/o/b.json 78101"))

    def test_invalid_arguments_are_rejected(self) -> None:
        settings = make_settings()
        path = PureWindowsPath(r"C:\a.json")
        with self.assertRaises(ValueError):
            matrix.build_command(settings, "linux", "sender", path, 1)
        with self.assertRaises(ValueError):
            matrix.build_command(settings, "wsl", "observer", path, 1)


class EnvironmentTest(unittest.TestCase):
    def test_windows_path_is_prepended(self) -> None:
        environment = matrix.build_windows_environment(make_settings(), {"PATH": r"C:\base", "X": "1"})
        parts = environment["PATH"].split(";")
        self.assertEqual(r"D:\repos\kit\app\cplat\prod\lib", parts[0])
        self.assertEqual(r"D:\repos\kit\app\openssl\prod\lib", parts[4])
        self.assertEqual(r"C:\base", parts[5])
        self.assertEqual("1", environment["X"])

    def test_base_environment_is_not_modified(self) -> None:
        base = {"PATH": "p"}
        matrix.build_windows_environment(make_settings(), base)
        self.assertEqual({"PATH": "p"}, base)


class ParseArgumentsTest(unittest.TestCase):
    REQUIRED = [
        "--windows-root", r"D:\repos\kit",
        "--wsl-root", "/work/kit",
        "--wsl-distribution", "TestDistro",
        "--windows-address", "10.0.0.1",
        "--wsl-address", "10.0.0.2",
    ]

    def test_required_and_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "new"
            settings = matrix.parse_arguments(self.REQUIRED + ["--output-directory", str(target)])
            self.assertEqual(PureWindowsPath(r"D:\repos\kit"), settings.windows_root)
            self.assertEqual(PurePosixPath("/work/kit"), settings.wsl_root)
            self.assertEqual("TestDistro", settings.wsl_distribution)
            self.assertEqual("10.0.0.1", settings.windows_address)
            self.assertEqual("10.0.0.2", settings.wsl_address)
            self.assertEqual(target, settings.output_directory)
            self.assertTrue(target.is_dir())
            self.assertEqual((), settings.selected_cases)

    def test_default_output_directory_is_new_temporary_directory(self) -> None:
        settings = matrix.parse_arguments(self.REQUIRED)
        try:
            self.assertTrue(settings.output_directory.is_dir())
            self.assertTrue(settings.output_directory.name.startswith("porter-interop-"))
        finally:
            settings.output_directory.rmdir()

    def test_case_filter(self) -> None:
        settings = matrix.parse_arguments(self.REQUIRED + ["psk/unicast", "cert/tcp"])
        try:
            self.assertEqual(("psk/unicast", "cert/tcp"), settings.selected_cases)
        finally:
            settings.output_directory.rmdir()

    def test_missing_required_argument_exits(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            matrix.parse_arguments(["--wsl-root", "/work/kit"])

    def test_unknown_case_exits(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            matrix.parse_arguments(self.REQUIRED + ["psk/unknown"])


class RunCaseDirectionTest(unittest.TestCase):
    def test_unsupported_direction_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            matrix.run_case(make_settings(), find_case("psk/unicast"), "wsl", "wsl")
        with self.assertRaises(ValueError):
            matrix.run_case(make_settings(), find_case("psk/unicast"), "linux", "windows")


def reverse_case(label: str, scenario: str = "basic") -> matrix.InteropCase:
    return dataclasses.replace(find_case(label), direction="wsl-to-windows", scenario=scenario)


def flow_case(label: str, direction: str = "windows-to-wsl") -> matrix.InteropCase:
    return dataclasses.replace(find_case(label), direction=direction, scenario="flow-control")


class ReverseDirectionConfigTest(unittest.TestCase):
    def service_of(self, case: matrix.InteropCase, side: str) -> dict:
        config = matrix.build_service_config(make_settings(), case, side)
        return config["services"][str(case.service_id)]

    def test_addresses_are_swapped_for_unicast(self) -> None:
        service = self.service_of(reverse_case("psk/unicast"), "wsl")
        self.assertEqual("10.0.0.2", service["src_addr1"])
        self.assertEqual("10.0.0.1", service["dst_addr1"])
        self.assertEqual(service, self.service_of(reverse_case("psk/unicast"), "windows"))

    def test_tcp_destination_is_the_receiver_windows(self) -> None:
        service = self.service_of(reverse_case("psk/tcp"), "wsl")
        self.assertNotIn("src_addr1", service)
        self.assertEqual("10.0.0.1", service["dst_addr1"])

    def test_multicast_source_is_wsl(self) -> None:
        service = self.service_of(reverse_case("psk/multicast"), "windows")
        self.assertEqual("10.0.0.2", service["src_addr1"])
        self.assertEqual("10.0.0.1", service["dst_addr1"])

    def test_n1_receiver_is_windows(self) -> None:
        case = reverse_case("psk/unicast_bidir_n1")
        receiver_side = self.service_of(case, "windows")
        sender_side = self.service_of(case, "wsl")
        self.assertEqual("unicast_bidir_n1", receiver_side["type"])
        self.assertEqual("10.0.0.1", receiver_side["dst_addr1"])
        self.assertEqual("unicast_bidir", sender_side["type"])
        self.assertEqual("10.0.0.2", sender_side["src_addr1"])
        self.assertEqual("10.0.0.1", sender_side["dst_addr1"])

    def test_cert_files_follow_the_role(self) -> None:
        case = reverse_case("cert/tcp")
        sender_side = self.service_of(case, "wsl")
        receiver_side = self.service_of(case, "windows")
        wsl_base = "/work/kit/app/porter/test/src/libporter/auth_test/data"
        windows_base = r"D:\repos\kit\app\porter\test\src\libporter\auth_test\data"
        self.assertEqual(wsl_base + "/server.pem", sender_side["cert_file"])
        self.assertEqual(["porter.test"], sender_side["peer_names"])
        self.assertEqual(windows_base + r"\client.pem", receiver_side["cert_file"])
        self.assertEqual(windows_base + r"\client-key.pem", receiver_side["key_file"])
        self.assertEqual(["urn:porter:peer"], receiver_side["peer_names"])

    def test_default_direction_cert_files_are_unchanged(self) -> None:
        service = matrix.build_service_config(make_settings(), find_case("cert/tcp"), "wsl")
        self.assertTrue(
            service["services"]["78109"]["cert_file"].endswith("/client.pem")
        )

    def test_identifiers_do_not_collide_between_directions(self) -> None:
        forward = find_case("psk/tcp")
        reverse = reverse_case("psk/tcp")
        self.assertNotEqual(forward.service_id, reverse.service_id)
        self.assertNotEqual(forward.port, reverse.port)
        self.assertEqual("psk-tcp", forward.stem)
        self.assertEqual("psk-tcp-wsl-to-windows", reverse.stem)
        self.assertEqual("psk/tcp [wsl-to-windows]", reverse.display_label)
        self.assertEqual(("wsl", "windows"), (reverse.sender_os, reverse.receiver_os))

    def test_identifiers_do_not_collide_between_scenarios(self) -> None:
        ports = {
            case.port
            for direction in matrix.DIRECTIONS
            for scenario in matrix.SCENARIOS
            for case in map(
                lambda c: dataclasses.replace(c, direction=direction, scenario=scenario),
                matrix.all_cases(),
            )
        }
        self.assertEqual(12 * 4, len(ports))


class FlowControlConfigTest(unittest.TestCase):
    def service_of(self, case: matrix.InteropCase, side: str) -> dict:
        config = matrix.build_service_config(make_settings(), case, side)
        return config["services"][str(case.service_id)]

    def test_capacity_is_set_on_the_receiver_only_for_one_way_types(self) -> None:
        case = flow_case("psk/unicast")
        self.assertEqual(matrix.FLOW_CONTROL_CAPACITY, self.service_of(case, "wsl")["flow_control_capacity"])
        self.assertNotIn("flow_control_capacity", self.service_of(case, "windows"))

    def test_capacity_follows_the_receiver_in_the_reverse_direction(self) -> None:
        case = flow_case("psk/tcp", "wsl-to-windows")
        self.assertIn("flow_control_capacity", self.service_of(case, "windows"))
        self.assertNotIn("flow_control_capacity", self.service_of(case, "wsl"))

    def test_capacity_is_set_on_both_ends_for_bidirectional_types(self) -> None:
        case = flow_case("psk/unicast_bidir")
        self.assertIn("flow_control_capacity", self.service_of(case, "windows"))
        self.assertIn("flow_control_capacity", self.service_of(case, "wsl"))

    def test_send_queue_depth_is_left_at_default(self) -> None:
        service = self.service_of(flow_case("psk/unicast"), "wsl")
        self.assertNotIn("send_queue_depth", service)
        self.assertNotIn("max_message_size", service)

    def test_basic_scenario_has_no_capacity(self) -> None:
        self.assertNotIn(
            "flow_control_capacity", self.service_of(find_case("psk/unicast"), "wsl")
        )

    def test_capacity_satisfies_the_margin_rule(self) -> None:
        self.assertTrue(
            matrix.check_flow_control_capacity(
                matrix.FLOW_CONTROL_CAPACITY,
                matrix.FLOW_CONTROL_SUPPRESS_PERCENT,
                matrix.MAX_MESSAGE_SIZE,
            )
        )
        self.assertFalse(matrix.check_flow_control_capacity(100000, 80, 65535))

    def test_burst_is_large_enough_to_cross_the_threshold(self) -> None:
        threshold = matrix.FLOW_CONTROL_CAPACITY * matrix.FLOW_CONTROL_SUPPRESS_PERCENT // 100
        self.assertGreater(matrix.DEFAULT_BURST_COUNT * matrix.DEFAULT_BURST_BYTES, threshold)

    def test_scenario_cases_are_psk_four_types(self) -> None:
        labels = [case.label for case in matrix.scenario_cases("flow-control")]
        self.assertEqual(
            ["psk/unicast", "psk/unicast_bidir", "psk/tcp", "psk/multicast"], labels
        )


class FlowControlCommandTest(unittest.TestCase):
    def test_receiver_command_has_the_delay_option(self) -> None:
        command = matrix.build_command(
            make_settings(), "wsl", "receiver", PureWindowsPath(r"C:\out\a.json"), 78100,
            ["--recv-delay-ms", "20"],
        )
        self.assertIn(" -l INFO --recv-delay-ms 20 receiver /mnt/c/out/a.json 78100", command[-1])

    def test_windows_receiver_command_has_the_delay_option(self) -> None:
        command = matrix.build_command(
            make_settings(), "windows", "receiver", PureWindowsPath(r"C:\out\a.json"), 78100,
            ["--recv-delay-ms", "5"],
        )
        self.assertEqual(
            ["-l", "INFO", "--recv-delay-ms", "5", "receiver", r"C:\out\a.json", "78100"],
            command[1:],
        )

    def test_burst_command(self) -> None:
        self.assertEqual("burst 400 16384", matrix.build_burst_command(400, 16384, False))
        self.assertEqual("burst -b 10 64", matrix.build_burst_command(10, 64, True))

    def test_wait_seconds(self) -> None:
        self.assertEqual(68.0, matrix.flow_control_wait_seconds(400, 20, 60))
        self.assertEqual(60.0, matrix.flow_control_wait_seconds(400, 0, 60))


class BurstParseTest(unittest.TestCase):
    RESULT = "BURST_RESULT received=400 expected=400 out_of_order=0"
    SENT = (
        "BURST_SENT sent=400 expected=400 full=123 elapsed_ms=8100"
        " bytes=16384 blocking=0 result=ok"
    )

    def test_parse_result_takes_the_last_line_even_with_prefix(self) -> None:
        lines = [
            "noise",
            "BURST_RESULT received=1 expected=400 out_of_order=0",
            "[1] \x1b[0m" + self.RESULT,
        ]
        self.assertEqual(
            {"received": 400, "expected": 400, "out_of_order": 0},
            matrix.parse_burst_result(lines),
        )

    def test_parse_result_without_line(self) -> None:
        self.assertIsNone(matrix.parse_burst_result(["a", "BURST_PROGRESS received=1 expected=2 out_of_order=0"]))

    def test_parse_sent(self) -> None:
        self.assertEqual(
            {"sent": 400, "expected": 400, "full": 123, "elapsed_ms": 8100,
             "bytes": 16384, "blocking": 0, "result": "ok"},
            matrix.parse_burst_sent([self.SENT]),
        )

    def test_parse_sent_error_result(self) -> None:
        line = "BURST_SENT sent=0 expected=10 full=0 elapsed_ms=1 bytes=64 blocking=1 result=error=-20"
        self.assertEqual("error=-20", matrix.parse_burst_sent([line])["result"])

    def test_parse_sent_respects_start_index(self) -> None:
        self.assertIsNone(matrix.parse_burst_sent([self.SENT, "x"], 1))
        self.assertIsNotNone(matrix.parse_burst_sent(["x", self.SENT], 1))

    def test_count_disconnects_after_start(self) -> None:
        lines = ["[サービス 1] 切断検知", "a", "[サービス 1] 切断検知"]
        self.assertEqual(2, matrix.count_disconnects(lines))
        self.assertEqual(1, matrix.count_disconnects(lines, 1))
        self.assertEqual(0, matrix.count_disconnects(["[サービス 1] 接続確立"]))


class JudgeBurstTest(unittest.TestCase):
    SENT = {"sent": 400, "expected": 400, "full": 5, "elapsed_ms": 9000,
            "bytes": 16384, "blocking": 0, "result": "ok"}
    RECEIVED = {"received": 400, "expected": 400, "out_of_order": 0}

    def judge(self, sent=SENT, received=RECEIVED, disconnects=0, suppressed=True, resumed=True):
        return matrix.judge_burst(400, sent, received, disconnects, suppressed, resumed)

    def test_pass(self) -> None:
        status, detail = self.judge()
        self.assertEqual("PASS", status)
        self.assertIn("full=5", detail)
        self.assertIn("suppressed=True resumed=True", detail)

    def test_missing_messages_fail(self) -> None:
        status, detail = self.judge(received={"received": 399, "expected": 400, "out_of_order": 0})
        self.assertEqual("FAIL", status)
        self.assertIn("受信が不足", detail)

    def test_out_of_order_fails(self) -> None:
        status, detail = self.judge(received={"received": 400, "expected": 400, "out_of_order": 2})
        self.assertEqual("FAIL", status)
        self.assertIn("順序の乱れ", detail)

    def test_disconnect_fails(self) -> None:
        status, detail = self.judge(disconnects=1)
        self.assertEqual("FAIL", status)
        self.assertIn("切断の通知", detail)

    def test_no_suppression_trace_fails(self) -> None:
        self.assertEqual("FAIL", self.judge(suppressed=False)[0])
        self.assertEqual("FAIL", self.judge(resumed=False)[0])

    def test_missing_lines_fail(self) -> None:
        status, detail = self.judge(sent=None, received=None)
        self.assertEqual("FAIL", status)
        self.assertIn("BURST_SENT なし", detail)
        self.assertIn("BURST_RESULT なし", detail)

    def test_incomplete_send_fails(self) -> None:
        sent = dict(self.SENT, sent=10, result="full_timeout")
        self.assertEqual("FAIL", self.judge(sent=sent)[0])


class ScenarioArgumentsTest(unittest.TestCase):
    REQUIRED = ParseArgumentsTest.REQUIRED

    def parse(self, extra):
        settings = matrix.parse_arguments(self.REQUIRED + extra)
        self.addCleanup(settings.output_directory.rmdir)
        return settings

    def test_defaults_keep_the_existing_behavior(self) -> None:
        settings = self.parse([])
        self.assertEqual("windows-to-wsl", settings.direction)
        self.assertEqual("basic", settings.scenario)
        self.assertEqual(12, len(matrix.select_cases(settings)))

    def test_direction_both_runs_both_directions(self) -> None:
        settings = self.parse(["--direction", "both"])
        cases = matrix.select_cases(settings)
        self.assertEqual(24, len(cases))
        self.assertEqual({"windows-to-wsl", "wsl-to-windows"}, {case.direction for case in cases})
        self.assertEqual("windows-to-wsl", cases[0].direction)

    def test_direction_reverse_only(self) -> None:
        settings = self.parse(["--direction", "wsl-to-windows", "psk/tcp"])
        cases = matrix.select_cases(settings)
        self.assertEqual(["psk/tcp"], [case.label for case in cases])
        self.assertEqual("wsl-to-windows", cases[0].direction)

    def test_flow_control_scenario(self) -> None:
        settings = self.parse(["--scenario", "flow-control", "--burst-count", "10",
                               "--burst-bytes", "64", "--recv-delay-ms", "3",
                               "--wait-margin-seconds", "5", "--burst-nonblocking"])
        self.assertEqual("flow-control", settings.scenario)
        self.assertEqual((10, 64, 3, 5, False),
                         (settings.burst_count, settings.burst_bytes, settings.recv_delay_ms,
                          settings.wait_margin_seconds, settings.burst_blocking))
        cases = matrix.select_cases(settings)
        self.assertEqual(4, len(cases))
        self.assertTrue(all(case.scenario == "flow-control" for case in cases))

    def test_flow_control_defaults(self) -> None:
        settings = self.parse(["--scenario", "flow-control"])
        self.assertEqual(
            (matrix.DEFAULT_BURST_COUNT, matrix.DEFAULT_BURST_BYTES,
             matrix.DEFAULT_RECV_DELAY_MS, matrix.DEFAULT_WAIT_MARGIN_SECONDS, True),
            (settings.burst_count, settings.burst_bytes, settings.recv_delay_ms,
             settings.wait_margin_seconds, settings.burst_blocking),
        )

    def test_invalid_values_exit(self) -> None:
        invalid = (
            ["--direction", "sideways"],
            ["--scenario", "other"],
            ["--burst-count", "0"],
            ["--burst-bytes", "15"],
            ["--burst-bytes", "65536"],
            ["--recv-delay-ms", "-1"],
            ["--wait-margin-seconds", "-1"],
            ["--scenario", "flow-control", "cert/tcp"],
            ["--scenario", "flow-control", "psk/tcp_bidir"],
        )
        for extra in invalid:
            with self.subTest(extra=extra):
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    matrix.parse_arguments(self.REQUIRED + ["--output-directory", tempfile.gettempdir()] + extra)


class FormatLogTest(unittest.TestCase):
    def test_prefix_per_side(self) -> None:
        text = matrix.format_log({"windows": ["a"], "wsl": ["b", "c"]})
        self.assertEqual("win: a\nwsl: b\nwsl: c\n", text)


if __name__ == "__main__":
    unittest.main()
