#!/usr/bin/env python3
"""porter の Windows と WSL の相互接続を確認する。

Windows 上の Python で実行する。Windows 側の porter-test.exe を直接、WSL 側の porter-test を
wsl.exe 経由で起動する。

向きは --direction で選ぶ (windows-to-wsl、wsl-to-windows、both)。
シナリオは --scenario で選ぶ。
  basic        認証 2 種 x 通信種別 6 種の 12 組み合わせで、1 件のメッセージの到達を確認する。
               unicast_bidir だけ、受信後に逆向きにも送信する。
  flow-control 受信側に flow_control_capacity を設定し、受信コールバックを遅らせた受信側へ
               porter-test の burst で連続して送信し、全件の順序どおりの受信と切断の不在を確認する。

詳細は app/porter/docs/interop-test.md を参照する。
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import os
import queue
import re
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
# 1 つの向きとシナリオの組が使うサービス ID とポートの個数。組ごとにずらして、
# 続けて実行する試験の間でポートの使い回しによる待ちを避ける。
CASES_PER_BLOCK = 12

# 向きの名前と、(sender の OS, receiver の OS)。
DIRECTIONS = {
    "windows-to-wsl": ("windows", "wsl"),
    "wsl-to-windows": ("wsl", "windows"),
}
DEFAULT_DIRECTION = "windows-to-wsl"
DIRECTION_CHOICES = tuple(DIRECTIONS) + ("both",)
SCENARIOS = ("basic", "flow-control")
DEFAULT_SCENARIO = "basic"
# flow-control シナリオの対象の通信種別 (認証は psk だけ)。
FLOW_CONTROL_TYPES = ("unicast", "unicast_bidir", "tcp", "multicast")
# 双方向の通信種別は、フロー制御を両端に設定する。
BIDIRECTIONAL_TYPES = ("unicast_bidir", "unicast_bidir_n1", "tcp_bidir")

# flow-control シナリオの設定と根拠。
#
# 容量 2 MB。要求のしきい値 (flow_control_suppress_percent の既定 80 %) は 1,677,721 [byte] で、
# 容量までの余裕は 419,431 [byte] である。これは max_message_size の既定 65,535 [byte] 以上であり、
# 設定の検査 (余裕が max_message_size 以上) を満たす。send_queue_depth は既定 (1,024) のままにする。
FLOW_CONTROL_CAPACITY = 2 * 1024 * 1024
FLOW_CONTROL_SUPPRESS_PERCENT = 80
MAX_MESSAGE_SIZE = 65535
# burst の件数とバイト数。16,384 [byte] x 400 件 = 約 6.3 MiB で、容量の約 3 倍、要求のしきい値の約 4 倍である。
# 抑制がなければ、受信側の通知キューは 2 MB を超え、切断に至る量である。
DEFAULT_BURST_COUNT = 400
DEFAULT_BURST_BYTES = 16384
# 受信コールバックの 1 件あたりの遅れ [ms]。受信側の消費は約 800 [KB/s] になり、
# 送信側 (ネットワークの速度) より十分に遅いため、しきい値を超える。全件の消費に約 8 秒かかる。
DEFAULT_RECV_DELAY_MS = 20
# 全件の消費に要する時間 (件数 x 遅れ) に足す待ちの余裕 [秒]。
# 抑制と解除の繰り返し (要求の再送周期 200 ms、有効期限 1,000 ms) と、プロセスの起動の揺れを見込む。
DEFAULT_WAIT_MARGIN_SECONDS = 60
# 送信側の接続の確立前に burst が拒否された (POTR_ERR_DISCONNECTED = -20) 場合の、送り直しの上限。
BURST_START_ATTEMPTS = 15
NOT_CONNECTED_CODE = "-20"
# porter-test の表示と porter のトレースの文言。
RECEIVER_DISCONNECTED = "切断検知"
SUPPRESSION_MARKERS = ("flow control suppressed DATA", "requested suppression")
RESUME_MARKERS = ("flow control resumed DATA", "resumed (occupancy")
BURST_RESULT_PATTERN = re.compile(
    r"BURST_RESULT received=(\d+) expected=(\d+) out_of_order=(\d+)"
)
BURST_SENT_PATTERN = re.compile(
    r"BURST_SENT sent=(\d+) expected=(\d+) full=(\d+) elapsed_ms=(\d+)"
    r" bytes=(\d+) blocking=(\d+) result=(\S+)"
)


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
    direction: str = DEFAULT_DIRECTION
    scenario: str = DEFAULT_SCENARIO
    burst_count: int = DEFAULT_BURST_COUNT
    burst_bytes: int = DEFAULT_BURST_BYTES
    recv_delay_ms: int = DEFAULT_RECV_DELAY_MS
    wait_margin_seconds: int = DEFAULT_WAIT_MARGIN_SECONDS
    burst_blocking: bool = True


@dataclass(frozen=True)
class InteropCase:
    """試験の 1 件。認証と通信種別の組み合わせ、およびサービス ID とポートの起点。"""

    auth: str
    communication_type: str
    index: int
    direction: str = DEFAULT_DIRECTION
    scenario: str = DEFAULT_SCENARIO

    @property
    def label(self) -> str:
        return f"{self.auth}/{self.communication_type}"

    @property
    def display_label(self) -> str:
        """向きとシナリオが既定と異なる場合だけ、それらを付けた表示用の名前。"""
        suffix = ""
        if self.direction != DEFAULT_DIRECTION:
            suffix += f" [{self.direction}]"
        if self.scenario != DEFAULT_SCENARIO:
            suffix += f" [{self.scenario}]"
        return self.label + suffix

    @property
    def stem(self) -> str:
        """設定とログのファイル名の共通部分。向きとシナリオが既定と異なる場合だけ、それらを足す。"""
        name = f"{self.auth}-{self.communication_type}"
        if self.direction != DEFAULT_DIRECTION:
            name += f"-{self.direction}"
        if self.scenario != DEFAULT_SCENARIO:
            name += f"-{self.scenario}"
        return name

    @property
    def block(self) -> int:
        return DIRECTIONS_ORDER.index(self.direction) + len(DIRECTIONS) * SCENARIOS.index(self.scenario)

    @property
    def service_id(self) -> int:
        return BASE_SERVICE_ID + self.block * CASES_PER_BLOCK + self.index

    @property
    def port(self) -> int:
        return BASE_PORT + (self.block * CASES_PER_BLOCK + self.index) * PORT_STEP

    @property
    def sender_os(self) -> str:
        return DIRECTIONS[self.direction][0]

    @property
    def receiver_os(self) -> str:
        return DIRECTIONS[self.direction][1]


DIRECTIONS_ORDER = tuple(DIRECTIONS)


def all_cases() -> list[InteropCase]:
    """12 組み合わせを、実行順に返す。"""
    pairs = [(auth, kind) for auth in AUTH_KINDS for kind in COMMUNICATION_TYPES]
    return [InteropCase(auth, kind, index) for index, (auth, kind) in enumerate(pairs)]


def scenario_cases(scenario: str) -> list[InteropCase]:
    """シナリオの対象の組み合わせを返す。flow-control は psk の 4 種だけ。"""
    if scenario == "flow-control":
        return [
            case for case in all_cases()
            if case.auth == "psk" and case.communication_type in FLOW_CONTROL_TYPES
        ]
    return all_cases()


def selected_directions(direction: str) -> list[str]:
    """--direction の値から、実行する向きの名前を実行順に返す。"""
    if direction == "both":
        return list(DIRECTIONS_ORDER)
    if direction not in DIRECTIONS:
        raise ValueError(f"未知の向きです: {direction}")
    return [direction]


def check_flow_control_capacity(
    capacity: int, suppress_percent: int, max_message_size: int
) -> bool:
    """要求のしきい値から容量までの余裕が max_message_size 以上かを返す (config.md の検査と同じ式)。"""
    return capacity - capacity * suppress_percent // 100 >= max_message_size


def to_wsl_path(windows_path) -> str:
    """Windows のパスを、WSL から見える /mnt/<ドライブ>/ 形式へ変換する。"""
    text = str(windows_path).replace("\\", "/")
    return "/mnt/" + text[0].lower() + text[2:]


def build_service_config(
    settings: InteropSettings, case: InteropCase, side: str
) -> dict:
    """指定した側 (windows または wsl) が読む porter の設定を、辞書として組み立てる。

    アドレスは、case の向きで決まる sender の OS を src、receiver の OS を dst に割り当てる。
    証明書は、sender の役割の側が server.pem、receiver の役割の側が client.pem を使う。
    flow-control シナリオでは、receiver の側 (双方向の通信種別では両端) に flow_control_capacity を足す。
    """
    if side not in OPERATING_SYSTEMS:
        raise ValueError(f"side は {OPERATING_SYSTEMS} のいずれかを指定してください: {side}")
    kind = case.communication_type
    port = case.port
    addresses = {"windows": settings.windows_address, "wsl": settings.wsl_address}
    sender_address = addresses[case.sender_os]
    receiver_address = addresses[case.receiver_os]
    is_receiver = side == case.receiver_os
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
    if kind == "unicast_bidir_n1" and is_receiver:
        service = {
            "type": "unicast_bidir_n1",
            "dst_addr1": receiver_address,
            "dst_port": port + 1,
            "max_peers": 4,
        }
    elif kind == "unicast_bidir_n1":
        # sender の側は、N:1 の受信側に接続する 1:1 の双方向として動かす。
        service = {
            "type": "unicast_bidir",
            "src_addr1": sender_address,
            "src_port": port,
            "dst_addr1": receiver_address,
            "dst_port": port + 1,
        }
    elif kind == "multicast":
        # 送信側と受信側で同じ定義を共有する。src は送信側、
        # dst は受信側がグループ参加に使う NIC のアドレス。
        service_id = case.service_id
        group = f"239.255.{service_id % 250}.{(service_id % 200) + 1}"
        service = {
            "type": "multicast",
            "src_addr1": sender_address,
            "src_port": port,
            "dst_addr1": receiver_address,
            "dst_port": port + 1,
            "multicast_group": group,
        }
    elif kind in ("tcp", "tcp_bidir"):
        service = {
            "type": kind,
            "dst_addr1": receiver_address,
            "dst_port": port + 1,
        }
    else:
        service = {
            "type": kind,
            "src_addr1": sender_address,
            "src_port": port,
            "dst_addr1": receiver_address,
            "dst_port": port + 1,
        }
    if case.auth == "psk":
        service.update(auth="psk", encrypt_key=PRE_SHARED_KEY)
    else:
        # 証明書は OS ではなく役割に対応させる。ファイルの置き場は、読む側の OS のリポジトリ ルートである。
        root = settings.windows_root if side == "windows" else settings.wsl_root
        directory = root / CERTIFICATE_DIRECTORY
        if is_receiver:
            certificate, key, peer_names = "client.pem", "client-key.pem", ["urn:porter:peer"]
        else:
            certificate, key, peer_names = "server.pem", "server-key.pem", ["porter.test"]
        service.update(
            auth="cert",
            ca_file=str(directory / "ca.pem"),
            peer_names=peer_names,
            cert_file=str(directory / certificate),
            key_file=str(directory / key),
        )
    if case.scenario == "flow-control" and (is_receiver or kind in BIDIRECTIONAL_TYPES):
        service["flow_control_capacity"] = FLOW_CONTROL_CAPACITY
    config["services"][str(case.service_id)] = service
    return config


def config_file_path(settings: InteropSettings, case: InteropCase, side: str) -> Path:
    """設定ファイルの出力先を返す。"""
    return settings.output_directory / f"{case.stem}-{SHORT_NAMES[side]}.json"


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
    extra_arguments: Sequence[str] = (),
) -> list[str]:
    """どちらの OS でどの役割の porter-test を起動するかから、コマンド ラインを組み立てる。

    config_path は Windows 側から見た設定ファイルのパスである。WSL 側では /mnt/ 形式へ変換する。
    extra_arguments は、-l INFO の次、役割の前に足す porter-test のオプション (例: --recv-delay-ms 20)。
    """
    if operating_system not in OPERATING_SYSTEMS:
        raise ValueError(f"未対応の OS です: {operating_system}")
    if role not in ROLES:
        raise ValueError(f"未対応の役割です: {role}")
    if operating_system == "windows":
        executable = settings.windows_root / "app/porter/prod/cbin/porter-test.exe"
        return [
            str(executable), "-l", "INFO", *extra_arguments,
            role, str(config_path), str(service_id),
        ]
    executable = settings.wsl_root / "app/porter/prod/cbin/porter-test"
    options = " ".join(["-l", "INFO", *extra_arguments])
    linux_command = (
        f"LD_LIBRARY_PATH={build_wsl_library_path(settings)} {executable}"
        f" {options} {role} {to_wsl_path(config_path)} {service_id}"
    )
    return [
        "wsl.exe", "-d", settings.wsl_distribution, "--", "bash", "-lc", linux_command,
    ]


def build_argument_parser() -> argparse.ArgumentParser:
    """コマンド ラインの引数解析器を作る。"""
    parser = argparse.ArgumentParser(
        description="porter の Windows と WSL の相互接続を実行する。"
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
    parser.add_argument("--direction", choices=DIRECTION_CHOICES, default=DEFAULT_DIRECTION,
                        help="sender と receiver の向き。windows-to-wsl は Windows が sender、"
                        "wsl-to-windows は WSL が sender、both は両方を続けて実行する"
                        f" (既定: {DEFAULT_DIRECTION})")
    parser.add_argument("--scenario", choices=SCENARIOS, default=DEFAULT_SCENARIO,
                        help="basic は 1 件のメッセージの到達の確認。flow-control は、遅らせた受信側へ"
                        f"連続して送信するフロー制御の確認 (既定: {DEFAULT_SCENARIO})")
    parser.add_argument("--burst-count", type=int, default=DEFAULT_BURST_COUNT,
                        help=f"flow-control: burst で送る件数 (既定: {DEFAULT_BURST_COUNT})")
    parser.add_argument("--burst-bytes", type=int, default=DEFAULT_BURST_BYTES,
                        help="flow-control: burst の 1 件のバイト数。16 以上"
                        f" {MAX_MESSAGE_SIZE} 以下 (既定: {DEFAULT_BURST_BYTES})")
    parser.add_argument("--recv-delay-ms", type=int, default=DEFAULT_RECV_DELAY_MS,
                        help="flow-control: 受信コールバックを 1 件ごとに遅らせる時間 [ms]"
                        f" (既定: {DEFAULT_RECV_DELAY_MS})")
    parser.add_argument("--wait-margin-seconds", type=int, default=DEFAULT_WAIT_MARGIN_SECONDS,
                        help="flow-control: 全件の消費に要する時間 (件数 x 遅れ) に足す待ちの余裕 [秒]"
                        f" (既定: {DEFAULT_WAIT_MARGIN_SECONDS})")
    # UDP の通信種別では、非ブロッキングの連続送信が受信側の OS の受信バッファーをあふれさせ、
    # フロー制御と無関係の欠落と切断が起きるため、既定は POTR_SEND_BLOCKING とする。
    parser.add_argument("--burst-nonblocking", dest="burst_blocking", action="store_false",
                        help="flow-control: burst を非ブロッキングで送信し、POTR_ERR_FULL を受けて送り直す"
                        " (省略時は burst に -b を付け、POTR_SEND_BLOCKING で送信する)")
    parser.add_argument("cases", nargs="*", metavar="auth/type",
                        help="実行する組み合わせ (例: psk/unicast)。省略時は、シナリオの対象すべて"
                        " (basic は 12 組み合わせ、flow-control は psk の 4 種)")
    return parser


def parse_arguments(argv: Sequence[str]) -> InteropSettings:
    """引数を解析し、設定オブジェクトを返す。出力先の作成は行わない場合がある (指定時)。"""
    parser = build_argument_parser()
    arguments = parser.parse_args(argv)
    known_labels = {case.label for case in scenario_cases(arguments.scenario)}
    if not 1 <= arguments.burst_count <= 1000000:
        parser.error("--burst-count は 1 以上 1000000 以下で指定してください")
    if not 16 <= arguments.burst_bytes <= MAX_MESSAGE_SIZE:
        parser.error(f"--burst-bytes は 16 以上 {MAX_MESSAGE_SIZE} 以下で指定してください")
    if not 0 <= arguments.recv_delay_ms <= 600000:
        parser.error("--recv-delay-ms は 0 以上 600000 以下で指定してください")
    if arguments.wait_margin_seconds < 0:
        parser.error("--wait-margin-seconds は 0 以上で指定してください")
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
        direction=arguments.direction,
        scenario=arguments.scenario,
        burst_count=arguments.burst_count,
        burst_bytes=arguments.burst_bytes,
        recv_delay_ms=arguments.recv_delay_ms,
        wait_margin_seconds=arguments.wait_margin_seconds,
        burst_blocking=arguments.burst_blocking,
    )


def select_cases(settings: InteropSettings) -> list[InteropCase]:
    """実行する組み合わせを、向きごとに実行順で返す。

    サービス ID とポートは絞り込みに影響されない。向きとシナリオの組ごとに、番号の範囲をずらす。
    """
    selected = set(settings.selected_cases)
    cases = []
    for direction in selected_directions(settings.direction):
        for case in scenario_cases(settings.scenario):
            if not selected or case.label in selected:
                cases.append(
                    dataclasses.replace(case, direction=direction, scenario=settings.scenario)
                )
    return cases


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


@dataclass
class CaseSession:
    """1 件の試験が使う、設定ファイル、プロセス、出力のキューとログ。"""

    settings: InteropSettings
    case: InteropCase
    sender_os: str
    receiver_os: str
    config_paths: dict
    environments: dict
    queues: dict
    logs: dict
    processes: dict


def create_session(
    settings: InteropSettings, case: InteropCase, sender_os: str, receiver_os: str
) -> CaseSession:
    """設定ファイルを書き出し、試験の状態を作る。プロセスはまだ起動しない。"""
    config_paths = {
        side: write_config(
            build_service_config(settings, case, side), config_file_path(settings, case, side)
        )
        for side in OPERATING_SYSTEMS
    }
    return CaseSession(
        settings=settings,
        case=case,
        sender_os=sender_os,
        receiver_os=receiver_os,
        config_paths=config_paths,
        environments={
            "windows": build_windows_environment(settings, os.environ),
            "wsl": None,
        },
        queues={"windows": queue.Queue(), "wsl": queue.Queue()},
        logs={"windows": [], "wsl": []},
        processes={"sender": None, "receiver": None},
    )


def open_services(session: CaseSession, receiver_arguments: Sequence[str] = ()):
    """receiver、sender の順に起動し、どちらも開くまで待つ。

    失敗した場合は (状態, 詳細) を返す。成功した場合は None を返す。
    receiver_arguments は、receiver の porter-test へ足すオプションである。
    """
    settings, case = session.settings, session.case
    sender_os, receiver_os = session.sender_os, session.receiver_os
    sender_name = DISPLAY_NAMES[sender_os]
    receiver_name = DISPLAY_NAMES[receiver_os]
    logs, queues = session.logs, session.queues
    session.processes["receiver"] = start_process(
        build_command(
            settings, receiver_os, "receiver", session.config_paths[receiver_os],
            case.service_id, receiver_arguments,
        ),
        session.environments[receiver_os],
        queues[receiver_os],
    )
    receiver_logs = logs[receiver_os]
    if not wait_until(
        {receiver_os: queues[receiver_os]}, logs,
        lambda: has(receiver_logs, OPENED, OPEN_FAILED), 12,
    ):
        return "BLOCKED", f"{receiver_name} receiver open timeout"
    if has(receiver_logs, OPEN_FAILED):
        return "FAIL", f"{receiver_name} receiver open failed"
    session.processes["sender"] = start_process(
        build_command(
            settings, sender_os, "sender", session.config_paths[sender_os], case.service_id
        ),
        session.environments[sender_os],
        queues[sender_os],
    )
    sender_logs = logs[sender_os]
    if not wait_until(queues, logs, lambda: has(sender_logs, OPENED, OPEN_FAILED), 15):
        return "BLOCKED", f"{sender_name} sender open timeout"
    if has(sender_logs, OPEN_FAILED):
        return "FAIL", f"{sender_name} sender open failed"
    return None


def close_services(session: CaseSession) -> None:
    """起動したプロセスへ exit を送って終了させる。終了しない場合は強制終了する。"""
    for process in (session.processes["sender"], session.processes["receiver"]):
        if process and process.poll() is None:
            try:
                process.stdin.write(b"exit\n")
                process.stdin.flush()
                process.wait(timeout=3)
            except Exception:
                process.kill()


def wait_for_connection(session: CaseSession) -> None:
    """送信の前に、UDP のセッション確立と TCP の接続を待つ。"""
    kind = session.case.communication_type
    receiver_logs = session.logs[session.receiver_os]
    if kind in ("unicast_bidir", "unicast_bidir_n1", "tcp_bidir", "tcp"):
        wait_until(
            session.queues, session.logs,
            lambda: has(receiver_logs, "CONNECTED", "接続確立"), 7,
        )
    time.sleep(0.25)


def run_basic(session: CaseSession):
    """basic シナリオ。sender から receiver へ 1 件を送り、届くことを確かめる。

    unicast_bidir だけ、成功後に receiver から sender へも送る。(状態, 詳細) を返す。
    """
    case = session.case
    auth, kind = case.auth, case.communication_type
    sender_os, receiver_os = session.sender_os, session.receiver_os
    sender_short = SHORT_NAMES[sender_os]
    receiver_short = SHORT_NAMES[receiver_os]
    queues, logs = session.queues, session.logs
    sender_logs, receiver_logs = logs[sender_os], logs[receiver_os]
    sender_process = session.processes["sender"]
    receiver_process = session.processes["receiver"]
    wait_for_connection(session)
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
    return status, detail


def parse_burst_result(lines: Sequence[str]):
    """受信側の最後の BURST_RESULT の行を解析し、(受信数, 期待数, 順序の乱れ) の辞書を返す。なければ None。"""
    found = None
    for line in lines:
        match = BURST_RESULT_PATTERN.search(line)
        if match:
            found = {
                "received": int(match.group(1)),
                "expected": int(match.group(2)),
                "out_of_order": int(match.group(3)),
            }
    return found


def parse_burst_sent(lines: Sequence[str], start_index: int = 0):
    """送信側の BURST_SENT の行を start_index 以降から探し、最後の 1 件を辞書で返す。なければ None。"""
    found = None
    for line in lines[start_index:]:
        match = BURST_SENT_PATTERN.search(line)
        if match:
            found = {
                "sent": int(match.group(1)),
                "expected": int(match.group(2)),
                "full": int(match.group(3)),
                "elapsed_ms": int(match.group(4)),
                "bytes": int(match.group(5)),
                "blocking": int(match.group(6)),
                "result": match.group(7),
            }
    return found


def count_disconnects(lines: Sequence[str], start_index: int = 0) -> int:
    """start_index 以降の、切断の通知 (porter-test の「切断検知」) の行数を返す。"""
    return sum(RECEIVER_DISCONNECTED in line for line in lines[start_index:])


def judge_burst(count: int, sent, received, disconnects: int, suppressed: bool, resumed: bool):
    """burst の結果から (状態, 詳細) を返す。

    PASS の条件は、sender が count 件を送り終え (result=ok)、receiver が count 件を順序の乱れなく受け、
    切断の通知がなく、抑制の開始と解除の両方がトレースに出ていること。
    """
    problems = []
    if sent is None:
        problems.append("BURST_SENT なし")
    elif sent["result"] != "ok" or sent["sent"] != count:
        problems.append(f"送信が完了しない (sent={sent['sent']} result={sent['result']})")
    if received is None:
        problems.append("BURST_RESULT なし")
    else:
        if received["expected"] != count or received["received"] != count:
            problems.append(
                f"受信が不足 (received={received['received']} expected={received['expected']})"
            )
        if received["out_of_order"] != 0:
            problems.append(f"順序の乱れ (out_of_order={received['out_of_order']})")
    if disconnects:
        problems.append(f"切断の通知 {disconnects} 件")
    if not suppressed:
        problems.append("抑制の開始のトレースなし")
    if not resumed:
        problems.append("抑制の解除のトレースなし")
    summary = (
        f"sent={sent['sent'] if sent else None} full={sent['full'] if sent else None}"
        f" elapsed_ms={sent['elapsed_ms'] if sent else None}"
        f" received={received['received'] if received else None}"
        f" out_of_order={received['out_of_order'] if received else None}"
        f" disconnects={disconnects} suppressed={suppressed} resumed={resumed}"
    )
    if problems:
        return "FAIL", f"{summary}: {'; '.join(problems)}"
    return "PASS", summary


def flow_control_wait_seconds(count: int, recv_delay_ms: int, margin_seconds: int) -> float:
    """burst の完了を待つ上限 [秒]。受信側が全件を消費する時間 (件数 x 遅れ) に余裕を足す。"""
    return count * recv_delay_ms / 1000 + margin_seconds


def build_burst_command(count: int, byte_count: int, blocking: bool) -> str:
    """sender の porter-test へ入力する burst コマンドを組み立てる。"""
    option = "-b " if blocking else ""
    return f"burst {option}{count} {byte_count}"


def run_flow_control(session: CaseSession):
    """flow-control シナリオ。遅らせた receiver へ burst で連続して送信し、結果を判定する。

    burst が接続の確立前に拒否された (sent=0 で result=error=-20) 場合は、1 秒待って送り直す。
    切断の通知は、burst の開始以後の行だけを数える。(状態, 詳細) を返す。
    """
    settings = session.settings
    queues, logs = session.queues, session.logs
    sender_logs, receiver_logs = logs[session.sender_os], logs[session.receiver_os]
    sender_process = session.processes["sender"]
    wait_for_connection(session)
    limit = flow_control_wait_seconds(
        settings.burst_count, settings.recv_delay_ms, settings.wait_margin_seconds
    )
    command = build_burst_command(
        settings.burst_count, settings.burst_bytes, settings.burst_blocking
    )
    sender_start = len(sender_logs)
    receiver_start = len(receiver_logs)
    sent = None
    for _ in range(BURST_START_ATTEMPTS):
        send_line(sender_process, command)
        wait_until(queues, logs, lambda: parse_burst_sent(sender_logs, sender_start), limit)
        sent = parse_burst_sent(sender_logs, sender_start)
        if sent is None or not (sent["sent"] == 0 and sent["result"] == f"error={NOT_CONNECTED_CODE}"):
            break
        sender_start = len(sender_logs)
        receiver_start = len(receiver_logs)
        wait_until(queues, logs, lambda: False, 1)
    wait_until(queues, logs, lambda: parse_burst_result(receiver_logs[receiver_start:]), limit)
    received = parse_burst_result(receiver_logs[receiver_start:])
    both = sender_logs[sender_start:] + receiver_logs[receiver_start:]
    disconnects = count_disconnects(both)
    suppressed = has(both, *SUPPRESSION_MARKERS)
    resumed = has(both, *RESUME_MARKERS)
    return judge_burst(settings.burst_count, sent, received, disconnects, suppressed, resumed)


def run_case(
    settings: InteropSettings,
    case: InteropCase,
    sender_os: str = "windows",
    receiver_os: str = "wsl",
):
    """1 件の試験を実行し、(状態, 詳細, ログ) を返す。

    sender_os の OS で sender を、receiver_os の OS で receiver を起動する。
    case.scenario が basic なら 1 件のメッセージの到達を、flow-control なら burst によるフロー制御を確認する。
    """
    if (sender_os, receiver_os) not in DIRECTIONS.values():
        raise ValueError(f"未対応の向きです: sender={sender_os}, receiver={receiver_os}")
    session = create_session(settings, case, sender_os, receiver_os)
    try:
        if case.scenario == "flow-control":
            arguments = ["--recv-delay-ms", str(settings.recv_delay_ms)]
        else:
            arguments = []
        failure = open_services(session, arguments)
        if failure is not None:
            return failure[0], failure[1], session.logs
        if case.scenario == "flow-control":
            status, detail = run_flow_control(session)
        else:
            status, detail = run_basic(session)
        return status, detail, session.logs
    except Exception as error:  # 試験の駆動の失敗も結果として記録する
        return "ERROR", repr(error), session.logs
    finally:
        close_services(session)


def format_log(logs: Mapping[str, list[str]]) -> str:
    """ログの行頭へ OS の短い名前を付け、ファイルへ書く文字列にする。"""
    return "\n".join(
        f"{SHORT_NAMES[side]}: {line}" for side in OPERATING_SYSTEMS for line in logs[side]
    ) + "\n"


def main(argv: Sequence[str] | None = None) -> int:
    settings = parse_arguments(sys.argv[1:] if argv is None else argv)
    results = []
    for case in select_cases(settings):
        status, detail, logs = run_case(settings, case, case.sender_os, case.receiver_os)
        log_path = settings.output_directory / f"{case.stem}.log"
        log_path.write_text(format_log(logs), encoding="utf-8")
        results.append({"case": case.display_label, "status": status, "detail": detail})
        print(f"{case.display_label}: {status} ({detail})", flush=True)
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
