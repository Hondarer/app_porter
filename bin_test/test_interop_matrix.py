#!/usr/bin/env python3
"""interop_matrix.py の単体テスト。プロセスは起動しない。"""

from __future__ import annotations

import contextlib
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
    def test_unimplemented_direction_is_rejected(self) -> None:
        with self.assertRaises(NotImplementedError):
            matrix.run_case(make_settings(), find_case("psk/unicast"), "wsl", "windows")


class FormatLogTest(unittest.TestCase):
    def test_prefix_per_side(self) -> None:
        text = matrix.format_log({"windows": ["a"], "wsl": ["b", "c"]})
        self.assertEqual("win: a\nwsl: b\nwsl: c\n", text)


if __name__ == "__main__":
    unittest.main()
