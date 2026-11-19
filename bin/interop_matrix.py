#!/usr/bin/env python3
"""porter の Windows と WSL の相互接続を、認証 2 種 x 通信種別 6 種の 12 組み合わせで確認する。

Windows 上の Python で実行する。Windows 側の porter-test.exe を直接、WSL 側の porter-test を
wsl.exe 経由で起動する。現在の向きは「Windows が sender、WSL が receiver」である。
unicast_bidir だけ、受信後に WSL から Windows へも送信する。

詳細は app/porter/docs/interop-test.md を参照する。
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Mapping, Sequence

# 試験対象のライブラリ (PATH と LD_LIBRARY_PATH に加える app の名前)。
DEPENDENCY_APPS = ("cplat", "porter", "cjson", "zlib", "openssl")
# 試験用の固定値。PSK の鍵文字列。
PRE_SHARED_KEY = "interop-cross-platform-psk-20261118"
# 試験用の証明書の置き場 (リポジトリ ルートからの相対パス)。
CERTIFICATE_DIRECTORY = "app/porter/test/src/libporter/auth_test/data"
AUTH_KINDS = ("psk", "cert")
COMMUNICATION_TYPES = (
    "unicast",
    "unicast_bidir",
    "unicast_bidir_n1",
    "tcp",
    "tcp_bidir",
    "multicast",
)
OPERATING_SYSTEMS = ("windows", "wsl")
ROLES = ("sender", "receiver")
# 設定ファイルの名前とログの行頭に使う短い名前。
SHORT_NAMES = {"windows": "win", "wsl": "wsl"}
DISPLAY_NAMES = {"windows": "Windows", "wsl": "WSL"}
# porter-test のログに出る文言。
OPEN_FAILED = "開けませんでした"
OPENED = "opened successfully"
NOT_CONNECTED = "未接続のため送信できません"

BASE_SERVICE_ID = 78100
BASE_PORT = 59000
PORT_STEP = 12


@dataclass(frozen=True)
class InteropSettings:
    """試験全体の設定。環境固有の値はすべてここへ集める。"""

    windows_root: PureWindowsPath
    wsl_root: PurePosixPath
    wsl_distribution: str
    windows_address: str
    wsl_address: str
    output_directory: Path
    selected_cases: tuple[str, ...] = ()


@dataclass(frozen=True)
class InteropCase:
    """試験の 1 件。認証と通信種別の組み合わせ、およびサービス ID とポートの起点。"""

    auth: str
    communication_type: str
    index: int

    @property
    def label(self) -> str:
        return f"{self.auth}/{self.communication_type}"

    @property
    def service_id(self) -> int:
        return BASE_SERVICE_ID + self.index

    @property
    def port(self) -> int:
        return BASE_PORT + self.index * PORT_STEP


def all_cases() -> list[InteropCase]:
    """12 組み合わせを、実行順に返す。"""
    pairs = [(auth, kind) for auth in AUTH_KINDS for kind in COMMUNICATION_TYPES]
    return [InteropCase(auth, kind, index) for index, (auth, kind) in enumerate(pairs)]


def to_wsl_path(windows_path) -> str:
    """Windows のパスを、WSL から見える /mnt/<ドライブ>/ 形式へ変換する。"""
    text = str(windows_path).replace("\\", "/")
    return "/mnt/" + text[0].lower() + text[2:]


def build_service_config(
    settings: InteropSettings, case: InteropCase, side: str
) -> dict:
    """指定した側 (windows または wsl) が読む porter の設定を、辞書として組み立てる。"""
    if side not in OPERATING_SYSTEMS:
        raise ValueError(f"side は {OPERATING_SYSTEMS} のいずれかを指定してください: {side}")
    kind = case.communication_type
    port = case.port
    is_windows = side == "windows"
    config = {
        "global": {
            "udp_health_interval_ms": 500,
            "udp_health_timeout_ms": 5000,
            "tcp_health_interval_ms": 500,
            "tcp_health_timeout_ms": 5000,
            "tcp_close_timeout_ms": 1000,
        },
        "services": {},
    }
    if kind == "unicast_bidir_n1" and not is_windows:
        service = {
            "type": "unicast_bidir_n1",
            "dst_addr1": settings.wsl_address,
            "dst_port": port + 1,
            "max_peers": 4,
        }
    elif kind == "unicast_bidir_n1":
        # Windows 側は、N:1 の受信側に接続する 1:1 の双方向として動かす。
        service = {
            "type": "unicast_bidir",
            "src_addr1": settings.windows_address,
            "src_port": port,
            "dst_addr1": settings.wsl_address,
            "dst_port": port + 1,
        }
    elif kind == "multicast":
        # 送信側と受信側で同じ定義を共有する。src は送信側 (Windows)、
        # dst は受信側がグループ参加に使う NIC (WSL)。
        service_id = case.service_id
        group = f"239.255.{service_id % 250}.{(service_id % 200) + 1}"
        service = {
            "type": "multicast",
            "src_addr1": settings.windows_address,
            "src_port": port,
            "dst_addr1": settings.wsl_address,
            "dst_port": port + 1,
            "multicast_group": group,
        }
    elif kind in ("tcp", "tcp_bidir"):
        service = {
            "type": kind,
            "dst_addr1": settings.wsl_address,
            "dst_port": port + 1,
        }
    else:
        service = {
            "type": kind,
            "src_addr1": settings.windows_address,
            "src_port": port,
            "dst_addr1": settings.wsl_address,
            "dst_port": port + 1,
        }
    if case.auth == "psk":
        service.update(auth="psk", encrypt_key=PRE_SHARED_KEY)
    elif is_windows:
        directory = settings.windows_root / CERTIFICATE_DIRECTORY
        service.update(
            auth="cert",
            ca_file=str(directory / "ca.pem"),
            peer_names=["porter.test"],
            cert_file=str(directory / "server.pem"),
            key_file=str(directory / "server-key.pem"),
        )
    else:
        directory = settings.wsl_root / CERTIFICATE_DIRECTORY
        service.update(
            auth="cert",
            ca_file=str(directory / "ca.pem"),
            peer_names=["urn:porter:peer"],
            cert_file=str(directory / "client.pem"),
            key_file=str(directory / "client-key.pem"),
        )
    config["services"][str(case.service_id)] = service
    return config


def config_file_path(settings: InteropSettings, case: InteropCase, side: str) -> Path:
    """設定ファイルの出力先を返す。"""
    return settings.output_directory / (
        f"{case.auth}-{case.communication_type}-{SHORT_NAMES[side]}.json"
    )


def build_library_directories(root) -> list[str]:
    """依存ライブラリの置き場 (prod/lib) を、依存順に返す。"""
    return [str(root / "app" / name / "prod" / "lib") for name in DEPENDENCY_APPS]


def build_windows_environment(
    settings: InteropSettings, base_environment: Mapping[str, str]
) -> dict[str, str]:
    """Windows 側の子プロセス用の環境変数を作る。PATH の先頭へ依存ライブラリを足す。"""
    environment = dict(base_environment)
    directories = build_library_directories(settings.windows_root)
    environment["PATH"] = ";".join(directories + [environment.get("PATH", "")])
    return environment


def build_wsl_library_path(settings: InteropSettings) -> str:
    """WSL 側の LD_LIBRARY_PATH の値を作る。"""
    return ":".join(build_library_directories(settings.wsl_root))


def build_command(
    settings: InteropSettings,
    operating_system: str,
    role: str,
    config_path,
    service_id: int,
) -> list[str]:
    """どちらの OS でどの役割の porter-test を起動するかから、コマンド ラインを組み立てる。

    config_path は Windows 側から見た設定ファイルのパスである。WSL 側では /mnt/ 形式へ変換する。
    """
    if operating_system not in OPERATING_SYSTEMS:
        raise ValueError(f"未対応の OS です: {operating_system}")
    if role not in ROLES:
        raise ValueError(f"未対応の役割です: {role}")
    if operating_system == "windows":
        executable = settings.windows_root / "app/porter/prod/cbin/porter-test.exe"
        return [str(executable), "-l", "INFO", role, str(config_path), str(service_id)]
    executable = settings.wsl_root / "app/porter/prod/cbin/porter-test"
    linux_command = (
        f"LD_LIBRARY_PATH={build_wsl_library_path(settings)} {executable}"
        f" -l INFO {role} {to_wsl_path(config_path)} {service_id}"
    )
    return [
        "wsl.exe", "-d", settings.wsl_distribution, "--", "bash", "-lc", linux_command,
    ]


def build_argument_parser() -> argparse.ArgumentParser:
    """コマンド ラインの引数解析器を作る。"""
    parser = argparse.ArgumentParser(
        description="porter の Windows と WSL の相互接続 12 組み合わせを実行する。"
        " Windows 上の Python で実行する。",
    )
    parser.add_argument("--windows-root", required=True,
                        help="Windows 側のリポジトリ ルート (例: D:\\repos\\c-modernization-kit)")
    parser.add_argument("--wsl-root", required=True,
                        help="WSL 側のリポジトリ ルートの絶対パス")
    parser.add_argument("--wsl-distribution", required=True,
                        help="WSL のディストリビューション名")
    parser.add_argument("--windows-address", required=True,
                        help="Windows 側のアドレス (WSL から見える IPv4)")
    parser.add_argument("--wsl-address", required=True,
                        help="WSL 側のアドレス (Windows から見える IPv4)")
    parser.add_argument("--output-directory",
                        help="設定、ログ、results.json の出力先。省略時は一時ディレクトリを新規に作る")
    parser.add_argument("cases", nargs="*", metavar="auth/type",
                        help="実行する組み合わせ (例: psk/unicast)。省略時は 12 組み合わせすべて")
    return parser


def parse_arguments(argv: Sequence[str]) -> InteropSettings:
    """引数を解析し、設定オブジェクトを返す。出力先の作成は行わない場合がある (指定時)。"""
    parser = build_argument_parser()
    arguments = parser.parse_args(argv)
    known_labels = {case.label for case in all_cases()}
    unknown = [label for label in arguments.cases if label not in known_labels]
    if unknown:
        parser.error(
            f"未知の組み合わせです: {', '.join(unknown)} (指定できる値: {', '.join(sorted(known_labels))})"
        )
    if arguments.output_directory:
        output_directory = Path(arguments.output_directory)
        output_directory.mkdir(parents=True, exist_ok=True)
    else:
        output_directory = Path(tempfile.mkdtemp(prefix="porter-interop-"))
    return InteropSettings(
        windows_root=PureWindowsPath(arguments.windows_root),
        wsl_root=PurePosixPath(arguments.wsl_root),
        wsl_distribution=arguments.wsl_distribution,
        windows_address=arguments.windows_address,
        wsl_address=arguments.wsl_address,
        output_directory=output_directory,
        selected_cases=tuple(arguments.cases),
    )


def select_cases(settings: InteropSettings) -> list[InteropCase]:
    """絞り込みに合う組み合わせを、実行順に返す。サービス ID とポートは絞り込みに影響されない。"""
    selected = set(settings.selected_cases)
    return [case for case in all_cases() if not selected or case.label in selected]


def read_lines(process: subprocess.Popen, line_queue: queue.Queue) -> None:
    """子プロセスの出力を 1 行ずつキューへ流す。終了時は <EOF> を流す。"""
    for raw in iter(process.stdout.readline, b""):
        line_queue.put(raw.rstrip().decode("utf-8", errors="replace"))
    line_queue.put("<EOF>")


def wait_until(queues, logs, check, seconds) -> bool:
    """条件が成立するまで、各キューの行をログへ移しながら待つ。"""
    end = time.time() + seconds
    while time.time() < end:
        for side, line_queue in queues.items():
            try:
                logs[side].append(line_queue.get(timeout=0.08))
            except queue.Empty:
                pass
        if check():
            return True
    return False


def has(lines, *words) -> bool:
    """いずれかの行にいずれかの語が含まれるかを返す。"""
    return any(word in line for line in lines for word in words)


def start_process(command, environment, line_queue) -> subprocess.Popen:
    """コマンドを起動し、出力を読むスレッドを開始する。"""
    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
        env=environment,
    )
    threading.Thread(target=read_lines, args=(process, line_queue), daemon=True).start()
    return process


def write_config(data: dict, path: Path) -> Path:
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    return path


def send_line(process: subprocess.Popen, text: str) -> None:
    process.stdin.write(f"{text}\n".encode("utf-8"))
    process.stdin.flush()


def run_case(
    settings: InteropSettings,
    case: InteropCase,
    sender_os: str = "windows",
    receiver_os: str = "wsl",
):
    """1 件の試験を実行し、(状態, 詳細, ログ) を返す。

    sender_os の OS で sender を、receiver_os の OS で receiver を起動する。
    現在は windows から wsl への向きだけを実装している。
    """
    if (sender_os, receiver_os) != ("windows", "wsl"):
        raise NotImplementedError(
            f"未実装の向きです: sender={sender_os}, receiver={receiver_os}"
        )
    sender_name = DISPLAY_NAMES[sender_os]
    receiver_name = DISPLAY_NAMES[receiver_os]
    sender_short = SHORT_NAMES[sender_os]
    receiver_short = SHORT_NAMES[receiver_os]
    auth, kind = case.auth, case.communication_type
    service_id = case.service_id
    config_paths = {
        side: write_config(
            build_service_config(settings, case, side), config_file_path(settings, case, side)
        )
        for side in OPERATING_SYSTEMS
    }
    environments = {
        "windows": build_windows_environment(settings, os.environ),
        "wsl": None,
    }
    queues = {"windows": queue.Queue(), "wsl": queue.Queue()}
    logs = {"windows": [], "wsl": []}
    sender_process = receiver_process = None
    try:
        receiver_process = start_process(
            build_command(settings, receiver_os, "receiver", config_paths[receiver_os], service_id),
            environments[receiver_os],
            queues[receiver_os],
        )
        receiver_logs = logs[receiver_os]
        if not wait_until(
            {receiver_os: queues[receiver_os]}, logs,
            lambda: has(receiver_logs, OPENED, OPEN_FAILED), 12,
        ):
            return "BLOCKED", f"{receiver_name} receiver open timeout", logs
        if has(receiver_logs, OPEN_FAILED):
            return "FAIL", f"{receiver_name} receiver open failed", logs
        sender_process = start_process(
            build_command(settings, sender_os, "sender", config_paths[sender_os], service_id),
            environments[sender_os],
            queues[sender_os],
        )
        sender_logs = logs[sender_os]
        if not wait_until(
            queues, logs, lambda: has(sender_logs, OPENED, OPEN_FAILED), 15,
        ):
            return "BLOCKED", f"{sender_name} sender open timeout", logs
        if has(sender_logs, OPEN_FAILED):
            return "FAIL", f"{sender_name} sender open failed", logs
        if kind in ("unicast_bidir", "unicast_bidir_n1", "tcp_bidir", "tcp"):
            # 送信の前に、UDP のセッション確立と TCP の接続を待つ。
            wait_until(queues, logs, lambda: has(receiver_logs, "CONNECTED", "接続確立"), 7)
        time.sleep(0.25)
        token = f"INTEROP_{auth.upper()}_{kind.upper()}_{sender_short.upper()}"
        # sender は接続の確立前の送信を「未接続」で拒否する。拒否された場合は、
        # 1 秒待って送り直す (最大 15 回)。送り直した回数は結果に記録する。
        got = False
        attempt = 0
        for attempt in range(1, 16):
            refused = sum(NOT_CONNECTED in line for line in sender_logs)
            send_line(sender_process, f"send {token}")
            wait_until(
                queues, logs,
                lambda: has(receiver_logs, token)
                or sum(NOT_CONNECTED in line for line in sender_logs) > refused,
                12,
            )
            if has(receiver_logs, token):
                got = True
                break
            if sum(NOT_CONNECTED in line for line in sender_logs) == refused:
                got = False
                break
            got = False
            wait_until(queues, logs, lambda: False, 1)
        reverse = None
        if kind == "unicast_bidir":
            reverse = False
            if got:
                reverse_token = f"INTEROP_{auth.upper()}_{kind.upper()}_{receiver_short.upper()}"
                send_line(receiver_process, f"send {reverse_token}")
                reverse = wait_until(queues, logs, lambda: has(sender_logs, reverse_token), 8)
        status = "PASS" if got and reverse is not False else "FAIL"
        detail = (
            f"{sender_short}_to_{receiver_short}={got}"
            f" {receiver_short}_to_{sender_short}={reverse} send_attempts={attempt}"
        )
        return status, detail, logs
    except Exception as error:  # 試験の駆動の失敗も結果として記録する
        return "ERROR", repr(error), logs
    finally:
        for process in (sender_process, receiver_process):
            if process and process.poll() is None:
                try:
                    process.stdin.write(b"exit\n")
                    process.stdin.flush()
                    process.wait(timeout=3)
                except Exception:
                    process.kill()


def format_log(logs: Mapping[str, list[str]]) -> str:
    """ログの行頭へ OS の短い名前を付け、ファイルへ書く文字列にする。"""
    return "\n".join(
        f"{SHORT_NAMES[side]}: {line}" for side in OPERATING_SYSTEMS for line in logs[side]
    ) + "\n"


def main(argv: Sequence[str] | None = None) -> int:
    settings = parse_arguments(sys.argv[1:] if argv is None else argv)
    results = []
    for case in select_cases(settings):
        status, detail, logs = run_case(settings, case)
        log_path = settings.output_directory / f"{case.auth}-{case.communication_type}.log"
        log_path.write_text(format_log(logs), encoding="utf-8")
        results.append({"case": case.label, "status": status, "detail": detail})
        print(f"{case.label}: {status} ({detail})", flush=True)
        time.sleep(1)
    (settings.output_directory / "results.json").write_text(
        json.dumps(results, indent=2), encoding="utf-8"
    )
    passed = sum(1 for result in results if result["status"] == "PASS")
    print(f"TOTAL: {passed}/{len(results)} PASS", flush=True)
    print(f"出力先: {settings.output_directory}", flush=True)
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
