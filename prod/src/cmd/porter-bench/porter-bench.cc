/**
 *******************************************************************************
 *  @file           porter-bench.cc
 *  @brief          porter の性能とリソース挙動を測定するコマンドを実装します。
 *  @author         Tetsuo Honda
 *  @date           2026/11/20
 *  @version        1.0.0
 *
 *  porter の公開 API だけを使い、ループバックで送受信して条件ごとの指標を CSV で出力します。\n
 *  変更の前後を比べる場合は、比べたい 2 つのコミットでそれぞれビルドした実行ファイルを、
 *  同じ条件で実行して結果を比較します。\n
 *  実行ファイルが出力する CSV の `version` 列には、`--label` で指定した名前を記録します。\n
 *  測定の手順と結果の見方は `app/porter/docs/performance-measurement.md` を参照してください。
 *
 *  @par            測定条件
 *  | 条件 | 内容                                                                              |
 *  | ---- | --------------------------------------------------------------------------------- |
 *  | p1   | UDP の片方向送信 (`POTR_TYPE_UNICAST`)。送信スレッドごとに同時未完了数を制限する。 |
 *  | p2   | N:1 の双方向 UDP で 4 クライアントが要求を送り、サーバーが返信する。             |
 *  | p3   | 受信コールバックが 1 件ごとに待機する場合の通知遅延とメモリ増加量を測る。         |
 *  | p4   | open、接続待ち、1 件の送受信、close を反復し、メモリ、ハンドル、スレッドの増加を測る。 |
 *  | p5   | p1 と同等の負荷を TCP で実行する。                                                |
 *
 *  Table: 測定条件
 *
 *  @par            使用方法
    @code{.sh}
    porter-bench --cond <p1|p2|p3|p4|p5> [options]
    @endcode
 *
 *  @par            主なオプション
 *  | オプション                   | 説明                                                         |
 *  | ---------------------------- | ------------------------------------------------------------ |
 *  | --cond \<p1\|p2\|p3\|p4\|p5\> | 測定条件 (必須)                                              |
 *  | --label \<name\>             | CSV の version 列に記録する名前 (既定値: current)             |
 *  | --out \<path\>               | CSV の出力先 (省略時は標準出力)                               |
 *  | --help                       | すべてのオプションを表示する                                 |
 *
 *  Table: porter-bench の主なコマンド ライン オプション
 *
 *  @par            終了コード
 *  | 値 | 意味                                                 |
 *  | -- | ---------------------------------------------------- |
 *  | 0  | 測定に成功し、CSV を出力した。                       |
 *  | 1  | 引数の誤り、または測定の失敗 (CSV は出力しない)。     |
 *
 *  Table: porter-bench の終了コード
 *
 *  @par            使用例
    @code{.sh}
    porter-bench --cond p5 --tcp-type oneway --bytes 5000 --flags blocking --threads 4 --label target --out p5.csv
    @endcode
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <mmsystem.h>

/*
 * GetProcessMemoryInfo() と timeBeginPeriod() のために、リンク時に必要なライブラリを指定する。
 * see: https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo
 * see: https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod
 */
#ifdef _MSC_VER
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "winmm.lib")
#endif /* _MSC_VER */
#else
#include <dirent.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#endif

#include <cplat/console/console.h>
#include <porter.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

/* ===== OS 依存処理: 時刻、CPU 時間、メモリ、ハンドル、スレッド ===== */

static uint64_t mono_now_ns()
{
#ifdef _WIN32
    static LARGE_INTEGER freq = []() {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const uint64_t q = static_cast<uint64_t>(now.QuadPart);
    const uint64_t f = static_cast<uint64_t>(freq.QuadPart);
    const uint64_t sec = q / f;
    const uint64_t rem = q % f;
    return sec * 1000000000ULL + (rem * 1000000000ULL) / f;
#else
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
#endif
}

static double process_cpu_ms()
{
#ifdef _WIN32
    FILETIME ft_create{};
    FILETIME ft_exit{};
    FILETIME ft_kernel{};
    FILETIME ft_user{};
    if (!GetProcessTimes(GetCurrentProcess(), &ft_create, &ft_exit, &ft_kernel, &ft_user))
    {
        return 0.0;
    }
    ULARGE_INTEGER k{};
    k.LowPart = ft_kernel.dwLowDateTime;
    k.HighPart = ft_kernel.dwHighDateTime;
    ULARGE_INTEGER u{};
    u.LowPart = ft_user.dwLowDateTime;
    u.HighPart = ft_user.dwHighDateTime;
    const uint64_t total_100ns = k.QuadPart + u.QuadPart;
    return static_cast<double>(total_100ns) * 1e-4;
#else
    struct rusage ru {};
    if (getrusage(RUSAGE_SELF, &ru) != 0)
    {
        return 0.0;
    }
    const double u_ms = static_cast<double>(ru.ru_utime.tv_sec) * 1000.0 +
                        static_cast<double>(ru.ru_utime.tv_usec) / 1000.0;
    const double k_ms = static_cast<double>(ru.ru_stime.tv_sec) * 1000.0 +
                        static_cast<double>(ru.ru_stime.tv_usec) / 1000.0;
    return u_ms + k_ms;
#endif
}

struct MemSample
{
    uint64_t rss_bytes = 0;
    uint64_t peak_rss_bytes = 0;
    uint64_t priv_bytes = 0;
    uint64_t handles = 0;
    uint64_t threads = 0;
};

#ifdef _WIN32
/*
 * 自プロセスのスレッド数を数える。
 * see: https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot
 */
static uint64_t count_windows_threads()
{
    const DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    uint64_t count = 0;
    if (Thread32First(snap, &te))
    {
        do
        {
            if (te.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(te.th32OwnerProcessID))
            {
                if (te.th32OwnerProcessID == pid)
                {
                    ++count;
                }
            }
            te.dwSize = sizeof(te);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return count;
}
#else
static bool parse_proc_status_u64(const char *line, const char *key, uint64_t *out_val)
{
    const size_t key_len = std::strlen(key);
    if (std::strncmp(line, key, key_len) != 0)
    {
        return false;
    }
    const char *p = line + key_len;
    while (*p == ' ' || *p == '\t')
    {
        ++p;
    }
    char *endp = nullptr;
    const unsigned long long val = std::strtoull(p, &endp, 10);
    if (endp == p)
    {
        return false;
    }
    *out_val = static_cast<uint64_t>(val);
    return true;
}

/*
 * 自プロセスのファイル記述子の数を数える。
 * see: https://man7.org/linux/man-pages/man5/proc_pid_fd.5.html
 */
static uint64_t count_linux_fds()
{
    DIR *d = opendir("/proc/self/fd");
    if (d == nullptr)
    {
        return 0;
    }
    uint64_t count = 0;
    struct dirent *ent = nullptr;
    while ((ent = readdir(d)) != nullptr)
    {
        if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0)
        {
            continue;
        }
        ++count;
    }
    closedir(d);
    /* opendir() 自身が開いた記述子の分を引く。 */
    return (count > 0) ? (count - 1) : 0;
}
#endif

static MemSample sample_memory(bool include_handles_threads = false)
{
    MemSample s{};
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc)))
    {
        s.rss_bytes = static_cast<uint64_t>(pmc.WorkingSetSize);
        s.peak_rss_bytes = static_cast<uint64_t>(pmc.PeakWorkingSetSize);
        s.priv_bytes = static_cast<uint64_t>(pmc.PrivateUsage);
    }
    if (include_handles_threads)
    {
        DWORD h = 0;
        if (GetProcessHandleCount(GetCurrentProcess(), &h))
        {
            s.handles = static_cast<uint64_t>(h);
        }
        s.threads = count_windows_threads();
    }
#else
    /*
     * VmRSS、VmHWM、RssAnon、Threads を読む。RssAnon は Windows の PrivateUsage に相当する値として扱う。
     * see: https://man7.org/linux/man-pages/man5/proc_pid_status.5.html
     */
    FILE *fp = std::fopen("/proc/self/status", "r");
    if (fp != nullptr)
    {
        char buf[512];
        while (std::fgets(buf, sizeof(buf), fp) != nullptr)
        {
            uint64_t val = 0;
            if (parse_proc_status_u64(buf, "VmRSS:", &val))
            {
                s.rss_bytes = val * 1024ULL;
            }
            else if (parse_proc_status_u64(buf, "VmHWM:", &val))
            {
                s.peak_rss_bytes = val * 1024ULL;
            }
            else if (parse_proc_status_u64(buf, "RssAnon:", &val))
            {
                s.priv_bytes = val * 1024ULL;
            }
            else if (parse_proc_status_u64(buf, "Threads:", &val))
            {
                s.threads = val;
            }
        }
        std::fclose(fp);
    }
    if (include_handles_threads)
    {
        s.handles = count_linux_fds();
    }
#endif
    return s;
}

static const char *os_name()
{
#ifdef _WIN32
    return "windows";
#else
    return "linux";
#endif
}

/* ===== 結果コードとイベントの表示 ===== */

static const char *potr_err_name(int err)
{
    switch (err)
    {
    case POTR_OK:
        return "POTR_OK";
    case POTR_ERR_INVALID_ARGUMENT:
        return "POTR_ERR_INVALID_ARGUMENT";
    case POTR_ERR_UNSUPPORTED:
        return "POTR_ERR_UNSUPPORTED";
    case POTR_ERR_NOT_FOUND:
        return "POTR_ERR_NOT_FOUND";
    case POTR_ERR_INVALID_HANDLE:
        return "POTR_ERR_INVALID_HANDLE";
    case POTR_ERR_OUT_OF_MEMORY:
        return "POTR_ERR_OUT_OF_MEMORY";
    case POTR_ERR_FULL:
        return "POTR_ERR_FULL";
    case POTR_ERR_DISCONNECTED:
        return "POTR_ERR_DISCONNECTED";
    case POTR_ERR_TIMEOUT:
        return "POTR_ERR_TIMEOUT";
    case POTR_ERR_EOF:
        return "POTR_ERR_EOF";
    case POTR_ERR_PROTOCOL:
        return "POTR_ERR_PROTOCOL";
    case POTR_ERR_IO:
        return "POTR_ERR_IO";
    case POTR_ERR_CANCELED:
        return "POTR_ERR_CANCELED";
    case POTR_ERR_UNKNOWN:
        return "POTR_ERR_UNKNOWN";
    default:
        return "POTR_ERR_OTHER";
    }
}

static const char *potr_event_name(potr_event ev)
{
    switch (ev)
    {
    case POTR_EVENT_DATA:
        return "POTR_EVENT_DATA";
    case POTR_EVENT_CONNECTED:
        return "POTR_EVENT_CONNECTED";
    case POTR_EVENT_DISCONNECTED:
        return "POTR_EVENT_DISCONNECTED";
    case POTR_EVENT_PATH_CONNECTED:
        return "POTR_EVENT_PATH_CONNECTED";
    case POTR_EVENT_PATH_DISCONNECTED:
        return "POTR_EVENT_PATH_DISCONNECTED";
    case POTR_EVENT_ERROR:
        return "POTR_EVENT_ERROR";
    default:
        return "POTR_EVENT_UNKNOWN";
    }
}

/* ===== 統計と CSV 出力 ===== */

static double percentile_sorted(const std::vector<double> &sorted, double p)
{
    if (sorted.empty())
    {
        return 0.0;
    }
    if (sorted.size() == 1)
    {
        return sorted[0];
    }
    const double idx = p * static_cast<double>(sorted.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(idx));
    const size_t hi = static_cast<size_t>(std::ceil(idx));
    if (lo == hi)
    {
        return sorted[lo];
    }
    const double frac = idx - static_cast<double>(lo);
    return sorted[lo] + frac * (sorted[hi] - sorted[lo]);
}

static double median_unsorted(std::vector<double> v)
{
    if (v.empty())
    {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    if ((n % 2U) != 0U)
    {
        return v[n / 2U];
    }
    return 0.5 * (v[n / 2U - 1U] + v[n / 2U]);
}

static double linear_slope(const std::vector<double> &xs, const std::vector<double> &ys)
{
    const size_t n = xs.size();
    if (n < 2U || ys.size() != n)
    {
        return 0.0;
    }
    double sx = 0.0;
    double sy = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        sx += xs[i];
        sy += ys[i];
    }
    const double mx = sx / static_cast<double>(n);
    const double my = sy / static_cast<double>(n);
    double num = 0.0;
    double den = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double dx = xs[i] - mx;
        const double dy = ys[i] - my;
        num += dx * dy;
        den += dx * dx;
    }
    if (den <= 0.0)
    {
        return 0.0;
    }
    return num / den;
}

struct MetricEntry
{
    std::string name;
    double value;
};

struct RunMetrics
{
    std::vector<MetricEntry> entries;

    void add(const std::string &name, double value)
    {
        entries.push_back(MetricEntry{name, value});
    }
};

static std::string format_value(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    return std::string(buf);
}

static void emit_csv(FILE *out, const std::string &label, const std::string &cond, const std::string &params,
                     const std::vector<RunMetrics> &runs, bool header)
{
    if (header)
    {
        std::fprintf(out, "os,version,cond,params,run,metric,value\n");
    }
    const char *os = os_name();
    const char *ver = label.c_str();
    for (size_t r = 0; r < runs.size(); ++r)
    {
        for (const auto &e : runs[r].entries)
        {
            std::fprintf(out, "%s,%s,%s,%s,%zu,%s,%s\n", os, ver, cond.c_str(), params.c_str(), r + 1U,
                         e.name.c_str(), format_value(e.value).c_str());
        }
    }
    if (!runs.empty())
    {
        /* 条件全体の min、median、max を、指標が最初に現れた順序のまま出力する。 */
        std::vector<std::string> metric_order;
        for (const auto &e : runs[0].entries)
        {
            metric_order.push_back(e.name);
        }
        for (const auto &mname : metric_order)
        {
            std::vector<double> vals;
            vals.reserve(runs.size());
            for (const auto &rm : runs)
            {
                for (const auto &e : rm.entries)
                {
                    if (e.name == mname)
                    {
                        vals.push_back(e.value);
                        break;
                    }
                }
            }
            if (vals.empty())
            {
                continue;
            }
            const double mn = *std::min_element(vals.begin(), vals.end());
            const double mx = *std::max_element(vals.begin(), vals.end());
            const double md = median_unsorted(vals);
            std::fprintf(out, "%s,%s,%s,%s,min,%s,%s\n", os, ver, cond.c_str(), params.c_str(), mname.c_str(),
                         format_value(mn).c_str());
            std::fprintf(out, "%s,%s,%s,%s,median,%s,%s\n", os, ver, cond.c_str(), params.c_str(), mname.c_str(),
                         format_value(md).c_str());
            std::fprintf(out, "%s,%s,%s,%s,max,%s,%s\n", os, ver, cond.c_str(), params.c_str(), mname.c_str(),
                         format_value(mx).c_str());
        }
    }
    std::fflush(out);
}

/* ===== ペイロードの符号化と復号 ===== */

static void write_u64_le(uint8_t *dst, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
    {
        dst[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xFFU);
    }
}

static uint64_t read_u64_le(const uint8_t *src)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
    {
        v |= static_cast<uint64_t>(src[i]) << (i * 8);
    }
    return v;
}

static void write_u32_le(uint8_t *dst, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
    {
        dst[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xFFU);
    }
}

static uint32_t read_u32_le(const uint8_t *src)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
    {
        v |= static_cast<uint32_t>(src[i]) << (i * 8);
    }
    return v;
}

static void fill_payload_pattern(std::vector<uint8_t> &buf)
{
    for (size_t i = 16U; i < buf.size(); ++i)
    {
        buf[i] = static_cast<uint8_t>('A' + (i % 26U));
    }
}

/* ===== porter の設定値の生成 ===== */

static potr_global_config make_default_global()
{
    potr_global_config g{};
    g.window_size = POTR_MAX_WINDOW_SIZE;
    g.max_payload = POTR_DEFAULT_MAX_PAYLOAD;
    g.reorder_timeout_ms = 50U;
    g.max_message_size = POTR_MAX_MESSAGE_SIZE;
    g.send_queue_depth = POTR_SEND_QUEUE_DEPTH;
    g.udp_health_interval_ms = POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS;
    g.udp_health_timeout_ms = POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS;
    g.tcp_health_interval_ms = POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS;
    g.tcp_health_timeout_ms = POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS;
    g.tcp_close_timeout_ms = POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS;
    /* フロー制御は既定で無効 (容量 0) とし、測定条件に影響させない。ほかの項目は既定値を明示する。 */
    g.flow_control_capacity = POTR_DEFAULT_FLOW_CONTROL_CAPACITY;
    g.flow_control_suppress_percent = POTR_DEFAULT_FLOW_CONTROL_SUPPRESS_PERCENT;
    g.flow_control_resume_percent = POTR_DEFAULT_FLOW_CONTROL_RESUME_PERCENT;
    g.flow_control_request_interval_ms = POTR_DEFAULT_FLOW_CONTROL_REQUEST_INTERVAL_MS;
    g.flow_control_request_lifetime_ms = POTR_DEFAULT_FLOW_CONTROL_REQUEST_LIFETIME_MS;
    g.flow_control_receiver_percent = POTR_DEFAULT_FLOW_CONTROL_RECEIVER_PERCENT;
    g.flow_control_receiver_count = POTR_DEFAULT_FLOW_CONTROL_RECEIVER_COUNT;
    return g;
}

static void set_addr(char dst[POTR_MAX_ADDR_LEN], const char *ip)
{
    std::memset(dst, 0, POTR_MAX_ADDR_LEN);
    if (ip != nullptr && ip[0] != '\0')
    {
        std::snprintf(dst, POTR_MAX_ADDR_LEN, "%s", ip);
    }
}

static potr_service_def make_service_def(int64_t service_id, potr_type type, uint16_t dst_port, uint16_t src_port,
                                         const char *src_ip, const char *dst_ip)
{
    potr_service_def d{};
    d.service_id = service_id;
    d.type = type;
    d.dst_port = dst_port;
    d.src_port = src_port;
    d.pack_wait_ms = 0U;
    set_addr(d.src_addr[0], src_ip);
    set_addr(d.dst_addr[0], dst_ip);
    d.max_peers = 16U;
    d.reconnect_interval_ms = POTR_DEFAULT_RECONNECT_INTERVAL_MS;
    d.connect_timeout_ms = POTR_DEFAULT_CONNECT_TIMEOUT_MS;
    return d;
}

/* ===== コマンド ライン オプション ===== */

struct Options
{
    std::string cond;
    std::string label = "current"; /* CSV の version 列に記録する名前 */
    std::string flags = "normal";
    std::string reply_mode = "ext";
    std::string proto = "udp";
    std::string tcp_type = "oneway";
    std::string out_path;
    std::string trace_level = "warning";
    size_t bytes = 0;         /* 0 means condition-specific default */
    uint32_t threads = 1U;
    uint32_t count = 0U;      /* 0 means condition-specific default */
    uint32_t runs = 5U;
    uint32_t warmup = UINT32_MAX; /* sentinel for condition-specific default */
    uint32_t inflight = 16U;
    uint32_t cb_sleep_ms = 1U;
    uint16_t base_port = 19000U;
    bool no_header = false;
    unsigned char pad[5] = {}; // 明示的アラインメントのためのパディング。
};

static std::string to_lower_str(std::string s)
{
    for (char &c : s)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

/* CSV の列を壊さないよう、英数字、'.'、'_'、'-' だけを許す。 */
static bool is_valid_label(const std::string &label)
{
    if (label.empty())
    {
        return false;
    }
    for (const char c : label)
    {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

static bool parse_u32(const char *s, uint32_t *out)
{
    if (s == nullptr || *s == '\0' || *s == '-')
    {
        return false;
    }
    char *endp = nullptr;
    const unsigned long v = std::strtoul(s, &endp, 10);
    if (endp == s || *endp != '\0')
    {
        return false;
    }
    *out = static_cast<uint32_t>(v);
    return true;
}

static void print_usage(const char *prog)
{
    std::fprintf(
        stderr,
        "Usage: %s --cond <p1|p2|p3|p4|p5> [options]\n"
        "Options:\n"
        "  --cond <p1|p2|p3|p4|p5>     Benchmark condition (required)\n"
        "  --label <name>              Name recorded in the CSV version column (default: current)\n"
        "  --bytes <N>                 Payload bytes (>=16; default: P1/P2/P4/P5=64, P3=5000)\n"
        "  --flags <normal|compress|blocking|compress_blocking>  Send flags for P1/P5 (default: normal)\n"
        "  --threads <N>               Sender thread count for P1/P5 (default: 1)\n"
        "  --count <N>                 Measured items/cycles per run (default: P1/P3/P5=10000, P2=5000, P4=1000)\n"
        "  --runs <R>                  Runs inside process (default: 5)\n"
        "  --warmup <W>                Warmup items/cycles excluded per run (default: P1/P2/P3/P5=1000, P4=10)\n"
        "  --inflight <K>              Max concurrent in-flight msgs per thread for P1/P2/P5 (default: 16)\n"
        "  --port <P>                  Base loopback port (default: 19000)\n"
        "  --reply-mode <cb|ext>       P2 reply mode (default: ext)\n"
        "  --proto <udp|tcp>           P4 protocol (default: udp)\n"
        "  --tcp-type <bidir|oneway>   TCP type for P5 / P4 tcp (default: oneway)\n"
        "  --cb-sleep-ms <ms>          P3 callback sleep duration in ms (default: 1)\n"
        "  --out <path>                Write CSV to file instead of stdout\n"
        "  --no-header                 Omit CSV header row\n"
        "  --trace-level <none|error|warning|info|debug>  Porter stderr trace level (default: warning)\n",
        prog);
}

/* ===== P1 / P5 の実装 ===== */

struct P1ThreadState
{
    std::mutex mtx;
    std::condition_variable cv;
    uint32_t completed = 0U;
    uint32_t next_seq = 0U;
};

struct P1Context
{
    std::vector<P1ThreadState *> tstates;
    std::vector<std::vector<double>> notify_lat_us; /* [thread][seq - warmup] */

    std::mutex conn_mtx;
    std::condition_variable conn_cv;
    std::atomic<uint64_t> unexpected_events{0ULL};
    std::atomic<uint64_t> full_count{0ULL}; /* 測定区間で POTR_ERR_FULL を受けた回数 */
    uint32_t threads = 1U;
    uint32_t warmup = 0U;
    uint32_t count = 0U;
    bool is_tcp = false;
    bool allow_errors_log = true;
    bool receiver_connected = false;
    bool sender_connected = false;
    std::atomic<bool> closing{false};
    unsigned char pad[7] = {}; // 明示的アラインメントのためのパディング。
};

static P1Context *s_p1_context = nullptr;

static void p1_receiver_cb(int64_t service_id, potr_peer_id peer_id, potr_event event, const void *data, size_t len)
{
    const uint64_t rx_ns = mono_now_ns();
    P1Context *ctx = s_p1_context;
    if (ctx == nullptr)
    {
        return;
    }
    if (event != POTR_EVENT_DATA)
    {
        if (event == POTR_EVENT_CONNECTED)
        {
            std::lock_guard<std::mutex> lk(ctx->conn_mtx);
            ctx->receiver_connected = true;
            ctx->conn_cv.notify_all();
        }
        else if (event == POTR_EVENT_DISCONNECTED)
        {
            if (!ctx->closing.load(std::memory_order_acquire))
            {
                ctx->unexpected_events.fetch_add(1ULL, std::memory_order_relaxed);
                std::fprintf(stderr,
                             "[%s] event=%s stage=recv service_id=%lld peer_id=%u\n",
                             ctx->is_tcp ? "P5" : "P1", potr_event_name(event), static_cast<long long>(service_id),
                             static_cast<unsigned>(peer_id));
            }
        }
        else if (event == POTR_EVENT_ERROR)
        {
            ctx->unexpected_events.fetch_add(1ULL, std::memory_order_relaxed);
            std::fprintf(stderr, "[%s] event=%s stage=recv service_id=%lld peer_id=%u\n",
                         ctx->is_tcp ? "P5" : "P1", potr_event_name(event), static_cast<long long>(service_id),
                         static_cast<unsigned>(peer_id));
        }
        return;
    }

    if (data == nullptr || len < 16U)
    {
        return;
    }
    const uint8_t *p = static_cast<const uint8_t *>(data);
    const uint64_t tx_ns = read_u64_le(p);
    const uint32_t tid = read_u32_le(p + 8U);
    const uint32_t seq = read_u32_le(p + 12U);
    if (tid >= ctx->threads)
    {
        return;
    }
    if (seq >= ctx->warmup && (seq - ctx->warmup) < ctx->count)
    {
        const uint64_t diff = (rx_ns > tx_ns) ? (rx_ns - tx_ns) : 0ULL;
        ctx->notify_lat_us[tid][seq - ctx->warmup] = static_cast<double>(diff) / 1000.0;
    }
    P1ThreadState *ts = ctx->tstates[tid];
    {
        std::lock_guard<std::mutex> lk(ts->mtx);
        ts->completed += 1U;
    }
    ts->cv.notify_one();
}

static void p1_sender_cb(int64_t service_id, potr_peer_id peer_id, potr_event event, const void * /*data*/,
                         size_t /*len*/)
{
    P1Context *ctx = s_p1_context;
    if (ctx == nullptr)
    {
        return;
    }
    if (event == POTR_EVENT_CONNECTED)
    {
        std::lock_guard<std::mutex> lk(ctx->conn_mtx);
        ctx->sender_connected = true;
        ctx->conn_cv.notify_all();
    }
    else if (event == POTR_EVENT_DISCONNECTED)
    {
        if (!ctx->closing.load(std::memory_order_acquire))
        {
            ctx->unexpected_events.fetch_add(1ULL, std::memory_order_relaxed);
            std::fprintf(stderr,
                         "[%s] event=POTR_EVENT_DISCONNECTED stage=sender_cb service_id=%lld peer_id=%u\n",
                         ctx->is_tcp ? "P5" : "P1", static_cast<long long>(service_id),
                         static_cast<unsigned>(peer_id));
        }
    }
    else if (event == POTR_EVENT_ERROR)
    {
        ctx->unexpected_events.fetch_add(1ULL, std::memory_order_relaxed);
        std::fprintf(stderr, "[%s] event=POTR_EVENT_ERROR stage=sender_cb service_id=%lld peer_id=%u\n",
                     ctx->is_tcp ? "P5" : "P1", static_cast<long long>(service_id),
                     static_cast<unsigned>(peer_id));
    }
}

static int parse_send_flags(const std::string &flags_str, int *out_flags)
{
    if (flags_str == "normal")
    {
        *out_flags = 0;
        return 0;
    }
    if (flags_str == "compress")
    {
        *out_flags = static_cast<int>(POTR_SEND_COMPRESS);
        return 0;
    }
    if (flags_str == "blocking")
    {
        *out_flags = static_cast<int>(POTR_SEND_BLOCKING);
        return 0;
    }
    if (flags_str == "compress_blocking")
    {
        *out_flags = static_cast<int>(POTR_SEND_COMPRESS | POTR_SEND_BLOCKING);
        return 0;
    }
    return -1;
}

static bool run_p1_or_p5(const Options &opt, std::vector<RunMetrics> *out_runs, std::string *out_params)
{
    const bool is_tcp = (opt.cond == "p5");
    const bool tcp_bidir = (opt.tcp_type == "bidir");
    int send_flags = 0;
    if (parse_send_flags(opt.flags, &send_flags) != 0)
    {
        std::fprintf(stderr, "Invalid --flags: %s\n", opt.flags.c_str());
        return false;
    }
    const size_t bytes = (opt.bytes > 0U) ? opt.bytes : 64U;
    if (bytes < 16U)
    {
        std::fprintf(stderr, "--bytes must be >= 16\n");
        return false;
    }
    const uint32_t count = (opt.count > 0U) ? opt.count : 10000U;
    const uint32_t warmup = (opt.warmup != UINT32_MAX) ? opt.warmup : 1000U;
    const uint32_t threads = (opt.threads > 0U) ? opt.threads : 1U;
    const uint32_t inflight = (opt.inflight > 0U) ? opt.inflight : 16U;

    {
        char pbuf[256];
        if (is_tcp)
        {
            std::snprintf(pbuf, sizeof(pbuf),
                          "tcp_type=%s;bytes=%zu;flags=%s;threads=%u;count=%u;warmup=%u",
                          opt.tcp_type.c_str(), bytes, opt.flags.c_str(), threads, count, warmup);
        }
        else
        {
            std::snprintf(pbuf, sizeof(pbuf),
                          "bytes=%zu;flags=%s;threads=%u;count=%u;warmup=%u",
                          bytes, opt.flags.c_str(), threads, count, warmup);
        }
        *out_params = pbuf;
    }

    const potr_type ptype = is_tcp ? (tcp_bidir ? POTR_TYPE_TCP_BIDIR : POTR_TYPE_TCP) : POTR_TYPE_UNICAST;
    const char *tag = is_tcp ? "P5" : "P1";

    for (uint32_t r = 0U; r < opt.runs; ++r)
    {
        const uint16_t port = static_cast<uint16_t>(opt.base_port + r * 10U);
        P1Context ctx;
        ctx.threads = threads;
        ctx.warmup = warmup;
        ctx.count = count;
        ctx.is_tcp = is_tcp;
        ctx.notify_lat_us.assign(threads, std::vector<double>(count, 0.0));
        ctx.tstates.reserve(threads);
        for (uint32_t t = 0U; t < threads; ++t)
        {
            ctx.tstates.push_back(new P1ThreadState());
        }
        s_p1_context = &ctx;

        auto cleanup_tstates = [&]() {
            s_p1_context = nullptr;
            for (P1ThreadState *ts : ctx.tstates)
            {
                delete ts;
            }
            ctx.tstates.clear();
        };

        potr_global_config global = make_default_global();
        if (is_tcp)
        {
            global.tcp_health_interval_ms = 100U;
            global.tcp_health_timeout_ms = 5000U;
            global.tcp_close_timeout_ms = 5000U;
        }
        potr_service_def def = make_service_def(1LL, ptype, port, 0U, "127.0.0.1", "127.0.0.1");

        potr_context *receiver = nullptr;
        potr_context *sender = nullptr;

        int rc = potr_service_open(&global, &def, POTR_ROLE_RECEIVER, p1_receiver_cb, &receiver);
        if (rc != POTR_OK)
        {
            std::fprintf(stderr, "[%s] run=%u stage=open_receiver err=%d (%s)\n", tag, r + 1U, rc,
                         potr_err_name(rc));
            cleanup_tstates();
            return false;
        }

        const potr_recv_fn sender_cb = (is_tcp && tcp_bidir) ? p1_sender_cb : nullptr;
        rc = potr_service_open(&global, &def, POTR_ROLE_SENDER, sender_cb, &sender);
        if (rc != POTR_OK)
        {
            std::fprintf(stderr, "[%s] run=%u stage=open_sender err=%d (%s)\n", tag, r + 1U, rc, potr_err_name(rc));
            potr_service_close(receiver);
            cleanup_tstates();
            return false;
        }

        if (is_tcp)
        {
            std::unique_lock<std::mutex> lk(ctx.conn_mtx);
            const bool ok = ctx.conn_cv.wait_for(lk, std::chrono::seconds(10), [&]() {
                return ctx.receiver_connected && (!tcp_bidir || ctx.sender_connected);
            });
            if (!ok)
            {
                std::fprintf(stderr,
                             "[%s] run=%u stage=connect timeout waiting for POTR_EVENT_CONNECTED "
                             "(receiver=%d sender=%d)\n",
                             tag, r + 1U, ctx.receiver_connected ? 1 : 0, ctx.sender_connected ? 1 : 0);
                ctx.closing.store(true, std::memory_order_release);
                potr_service_close(sender);
                potr_service_close(receiver);
                cleanup_tstates();
                return false;
            }
        }

        std::mutex bar_mtx;
        std::condition_variable bar_cv;
        uint32_t bar_arrived = 0U;
        uint64_t meas_start_ns = 0ULL;
        double cpu_start_ms = 0.0;

        std::atomic<bool> send_failed{false};
        std::vector<std::vector<double>> api_lat_us(threads, std::vector<double>(count, 0.0));
        std::vector<std::thread> workers;
        workers.reserve(threads);

        for (uint32_t spawn_index = 0U; spawn_index < threads; ++spawn_index)
        {
            /* 値捕捉の 4 [byte] がラムダのパディングになるため、添字は捕捉せず引数で渡す。 */
            workers.emplace_back([&](uint32_t tid) {
                std::vector<uint8_t> payload(bytes, 0U);
                fill_payload_pattern(payload);
                write_u32_le(payload.data() + 8U, tid);
                P1ThreadState *ts = ctx.tstates[tid];

                auto send_one = [&](uint32_t seq, bool record_api, const char *stage) -> bool {
                    write_u32_le(payload.data() + 12U, seq);
                    int retry = 0;
                    for (;;)
                    {
                        const uint64_t t0 = mono_now_ns();
                        write_u64_le(payload.data(), t0);
                        const int sret = potr_service_send(sender, POTR_PEER_NA, payload.data(), bytes, send_flags);
                        const uint64_t t1 = mono_now_ns();
                        if (sret == POTR_OK)
                        {
                            if (record_api)
                            {
                                api_lat_us[tid][seq - warmup] = static_cast<double>(t1 - t0) / 1000.0;
                            }
                            return true;
                        }
                        /*
                         * 待ち合わせを指定しない送信は、送信キューに空きがないと POTR_ERR_FULL を返す。
                         * porter はメッセージを保持しないため、少し待って同じメッセージを送り直す。
                         * 時刻 t0 は送り直しのたびに取り直すため、api_* は受け付けられた呼び出しの所要時間であり、
                         * 待った時間を含まない。待った回数は send_full_count として出力する。
                         */
                        if (sret == POTR_ERR_FULL ||
                            (sret == POTR_ERR_DISCONNECTED && is_tcp && !tcp_bidir && retry < 2000))
                        {
                            if (sret == POTR_ERR_FULL && record_api)
                            {
                                ctx.full_count.fetch_add(1ULL, std::memory_order_relaxed);
                            }
                            ++retry;
                            if (retry > 10000)
                            {
                                std::fprintf(stderr, "[%s] run=%u thread=%u stage=%s seq=%u timeout err=%d (%s)\n",
                                             tag, r + 1U, tid, stage, seq, sret, potr_err_name(sret));
                                return false;
                            }
                            if (retry < 32)
                            {
                                std::this_thread::yield();
                            }
                            else
                            {
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            }
                            continue;
                        }
                        std::fprintf(stderr, "[%s] run=%u thread=%u stage=%s seq=%u err=%d (%s)\n", tag, r + 1U,
                                     tid, stage, seq, sret, potr_err_name(sret));
                        return false;
                    }
                };

                /* 前半: ウォーム アップ */
                for (uint32_t seq = 0U; seq < warmup; ++seq)
                {
                    {
                        std::unique_lock<std::mutex> lk(ts->mtx);
                        const bool ok = ts->cv.wait_for(lk, std::chrono::seconds(15), [&]() {
                            return (seq - ts->completed) < inflight || send_failed.load(std::memory_order_acquire);
                        });
                        if (!ok)
                        {
                            std::fprintf(stderr, "[%s] run=%u thread=%u stage=warmup_inflight seq=%u timeout\n",
                                         tag, r + 1U, tid, seq);
                            send_failed.store(true, std::memory_order_release);
                            break;
                        }
                    }
                    if (send_failed.load(std::memory_order_acquire) || !send_one(seq, false, "warmup_send"))
                    {
                        send_failed.store(true, std::memory_order_release);
                        break;
                    }
                }
                if (!send_failed.load(std::memory_order_acquire))
                {
                    std::unique_lock<std::mutex> lk(ts->mtx);
                    const bool ok = ts->cv.wait_for(lk, std::chrono::seconds(15), [&]() {
                        return ts->completed >= warmup || send_failed.load(std::memory_order_acquire);
                    });
                    if (!ok)
                    {
                        std::fprintf(stderr, "[%s] run=%u thread=%u stage=warmup_drain timeout (got=%u/%u)\n", tag,
                                     r + 1U, tid, ts->completed, warmup);
                        send_failed.store(true, std::memory_order_release);
                    }
                }

                /* ウォーム アップ後に全スレッドをそろえる。 */
                {
                    std::unique_lock<std::mutex> lk(bar_mtx);
                    ++bar_arrived;
                    if (bar_arrived == threads)
                    {
                        cpu_start_ms = process_cpu_ms();
                        meas_start_ns = mono_now_ns();
                        bar_cv.notify_all();
                    }
                    else
                    {
                        bar_cv.wait(lk, [&]() { return bar_arrived == threads; });
                    }
                }

                if (send_failed.load(std::memory_order_acquire))
                {
                    return;
                }

                /* 後半: 測定 */
                for (uint32_t k = 0U; k < count; ++k)
                {
                    const uint32_t seq = warmup + k;
                    {
                        std::unique_lock<std::mutex> lk(ts->mtx);
                        const bool ok = ts->cv.wait_for(lk, std::chrono::seconds(15), [&]() {
                            return (seq - ts->completed) < inflight || send_failed.load(std::memory_order_acquire);
                        });
                        if (!ok)
                        {
                            std::fprintf(stderr, "[%s] run=%u thread=%u stage=send_inflight seq=%u timeout\n", tag,
                                         r + 1U, tid, seq);
                            send_failed.store(true, std::memory_order_release);
                            break;
                        }
                    }
                    if (send_failed.load(std::memory_order_acquire) || !send_one(seq, true, "send"))
                    {
                        send_failed.store(true, std::memory_order_release);
                        break;
                    }
                }

                if (!send_failed.load(std::memory_order_acquire))
                {
                    std::unique_lock<std::mutex> lk(ts->mtx);
                    const uint32_t target = warmup + count;
                    const bool ok = ts->cv.wait_for(lk, std::chrono::seconds(15), [&]() {
                        return ts->completed >= target || send_failed.load(std::memory_order_acquire);
                    });
                    if (!ok)
                    {
                        std::fprintf(stderr, "[%s] run=%u thread=%u stage=recv_drain timeout (got=%u/%u)\n", tag,
                                     r + 1U, tid, ts->completed, target);
                        send_failed.store(true, std::memory_order_release);
                    }
                }
            }, spawn_index);
        }

        for (auto &w : workers)
        {
            w.join();
        }
        const uint64_t meas_end_ns = mono_now_ns();
        const double cpu_end_ms = process_cpu_ms();

        ctx.closing.store(true, std::memory_order_release);
        const int close_s_rc = potr_service_close(sender);
        if (close_s_rc != POTR_OK)
        {
            std::fprintf(stderr, "[%s] run=%u stage=close_sender err=%d (%s)\n", tag, r + 1U, close_s_rc,
                         potr_err_name(close_s_rc));
        }
        const int close_r_rc = potr_service_close(receiver);
        if (close_r_rc != POTR_OK)
        {
            std::fprintf(stderr, "[%s] run=%u stage=close_receiver err=%d (%s)\n", tag, r + 1U, close_r_rc,
                         potr_err_name(close_r_rc));
        }

        if (send_failed.load(std::memory_order_acquire) || close_s_rc != POTR_OK || close_r_rc != POTR_OK ||
            ctx.unexpected_events.load(std::memory_order_acquire) > 0ULL)
        {
            cleanup_tstates();
            return false;
        }

        std::vector<double> all_api;
        std::vector<double> all_notify;
        all_api.reserve(static_cast<size_t>(threads) * count);
        all_notify.reserve(static_cast<size_t>(threads) * count);
        for (uint32_t tid = 0U; tid < threads; ++tid)
        {
            all_api.insert(all_api.end(), api_lat_us[tid].begin(), api_lat_us[tid].end());
            all_notify.insert(all_notify.end(), ctx.notify_lat_us[tid].begin(), ctx.notify_lat_us[tid].end());
        }
        std::sort(all_api.begin(), all_api.end());
        std::sort(all_notify.begin(), all_notify.end());

        const double elapsed_s = static_cast<double>(meas_end_ns - meas_start_ns) / 1e9;
        const double total_msgs = static_cast<double>(threads) * static_cast<double>(count);
        const double throughput = (elapsed_s > 0.0) ? (total_msgs / elapsed_s) : 0.0;

        RunMetrics rm;
        rm.add("throughput_msg_s", throughput);
        rm.add("api_p50_us", percentile_sorted(all_api, 0.50));
        rm.add("api_p95_us", percentile_sorted(all_api, 0.95));
        rm.add("api_p99_us", percentile_sorted(all_api, 0.99));
        rm.add("notify_p50_us", percentile_sorted(all_notify, 0.50));
        rm.add("notify_p95_us", percentile_sorted(all_notify, 0.95));
        rm.add("notify_p99_us", percentile_sorted(all_notify, 0.99));
        rm.add("cpu_time_ms", cpu_end_ms - cpu_start_ms);
        rm.add("send_full_count", static_cast<double>(ctx.full_count.load(std::memory_order_acquire)));
        out_runs->push_back(std::move(rm));

        cleanup_tstates();
    }
    return true;
}

/* ===== P2 の実装 (N:1 双方向の要求と返信) ===== */

struct P2ReplyItem
{
    std::vector<uint8_t> data;
    potr_peer_id peer_id = 0U;
    unsigned char pad[4] = {}; // 明示的アラインメントのためのパディング。
};

struct P2ClientState
{
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<double> rtt_us;
    uint32_t replies = 0U;
    bool connected = false;
    unsigned char pad[3] = {}; // 明示的アラインメントのためのパディング。
};

struct P2Context
{
    static constexpr uint32_t kNumClients = 4U;
    potr_context *server = nullptr;
    std::atomic<uint64_t> server_send_errors{0ULL};
    std::atomic<uint64_t> last_reply_ns{0ULL};
    std::atomic<uint64_t> full_count{0ULL}; /* 全区間 (ウォーム アップと返信を含む) で POTR_ERR_FULL を受けた回数 */

    std::mutex srv_conn_mtx;
    std::condition_variable srv_conn_cv;

    std::mutex q_mtx;
    std::condition_variable q_cv;
    std::deque<P2ReplyItem> q;

    P2ClientState *clients[kNumClients]{};

    uint32_t warmup = 0U;
    uint32_t count = 0U;
    uint32_t srv_connected_peers = 0U;
    uint32_t cli_connected_count = 0U;
    bool cb_reply_mode = false;
    std::atomic<bool> closing{false};
    std::atomic<bool> stop_worker{false};
    unsigned char pad[5] = {}; // 明示的アラインメントのためのパディング。
};

static P2Context *s_p2_context = nullptr;

static int p2_send_reply_retry(potr_context *srv, potr_peer_id peer_id, const void *data, size_t len)
{
    int retry = 0;
    for (;;)
    {
        const int rc = potr_service_send(srv, peer_id, data, len, 0);
        if (rc == POTR_OK)
        {
            return POTR_OK;
        }
        if (rc == POTR_ERR_FULL && retry < 1000)
        {
            /* 送信キューに空きがない場合は、少し待って送り直す (P1 の send_one を参照)。 */
            if (s_p2_context != nullptr)
            {
                s_p2_context->full_count.fetch_add(1ULL, std::memory_order_relaxed);
            }
            ++retry;
            if (retry < 32)
            {
                std::this_thread::yield();
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            continue;
        }
        return rc;
    }
}

static void p2_server_cb(int64_t /*service_id*/, potr_peer_id peer_id, potr_event event, const void *data, size_t len)
{
    P2Context *ctx = s_p2_context;
    if (ctx == nullptr)
    {
        return;
    }
    if (event != POTR_EVENT_DATA)
    {
        if (event == POTR_EVENT_CONNECTED)
        {
            std::lock_guard<std::mutex> lk(ctx->srv_conn_mtx);
            ++ctx->srv_connected_peers;
            ctx->srv_conn_cv.notify_all();
        }
        return;
    }
    if (data == nullptr || len == 0U || ctx->closing.load(std::memory_order_acquire))
    {
        return;
    }
    if (ctx->cb_reply_mode)
    {
        const int rc = p2_send_reply_retry(ctx->server, peer_id, data, len);
        if (rc != POTR_OK && !ctx->closing.load(std::memory_order_acquire))
        {
            ctx->server_send_errors.fetch_add(1ULL, std::memory_order_relaxed);
            std::fprintf(stderr, "[P2] stage=cb_reply peer_id=%u err=%d (%s)\n",
                         static_cast<unsigned>(peer_id), rc, potr_err_name(rc));
        }
    }
    else
    {
        P2ReplyItem item;
        item.peer_id = peer_id;
        const uint8_t *p = static_cast<const uint8_t *>(data);
        item.data.assign(p, p + len);
        {
            std::lock_guard<std::mutex> lk(ctx->q_mtx);
            ctx->q.push_back(std::move(item));
        }
        ctx->q_cv.notify_one();
    }
}

static void p2_client_cb(int64_t /*service_id*/, potr_peer_id /*peer_id*/, potr_event event, const void *data, size_t len)
{
    const uint64_t rx_ns = mono_now_ns();
    P2Context *ctx = s_p2_context;
    if (ctx == nullptr)
    {
        return;
    }
    if (event != POTR_EVENT_DATA)
    {
        if (event == POTR_EVENT_CONNECTED)
        {
            std::lock_guard<std::mutex> lk(ctx->srv_conn_mtx);
            ++ctx->cli_connected_count;
            ctx->srv_conn_cv.notify_all();
        }
        return;
    }
    if (data == nullptr || len < 16U)
    {
        return;
    }
    const uint8_t *p = static_cast<const uint8_t *>(data);
    const uint64_t tx_ns = read_u64_le(p);
    const uint32_t cid = read_u32_le(p + 8U);
    const uint32_t seq = read_u32_le(p + 12U);
    if (cid >= P2Context::kNumClients)
    {
        return;
    }
    P2ClientState *cs = ctx->clients[cid];
    ctx->last_reply_ns.store(rx_ns, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(cs->mtx);
        if (seq >= ctx->warmup && (seq - ctx->warmup) < ctx->count)
        {
            const uint64_t diff = (rx_ns > tx_ns) ? (rx_ns - tx_ns) : 0ULL;
            cs->rtt_us.push_back(static_cast<double>(diff) / 1000.0);
        }
        ++cs->replies;
    }
    cs->cv.notify_all();
}

static bool run_p2(const Options &opt, std::vector<RunMetrics> *out_runs, std::string *out_params)
{
    if (opt.reply_mode != "cb" && opt.reply_mode != "ext")
    {
        std::fprintf(stderr, "Invalid --reply-mode: %s (expected cb or ext)\n", opt.reply_mode.c_str());
        return false;
    }
    const size_t bytes = (opt.bytes > 0U) ? opt.bytes : 64U;
    if (bytes < 16U)
    {
        std::fprintf(stderr, "--bytes must be >= 16\n");
        return false;
    }
    const uint32_t count = (opt.count > 0U) ? opt.count : 5000U;
    const uint32_t warmup = (opt.warmup != UINT32_MAX) ? opt.warmup : 1000U;
    const uint32_t inflight = (opt.inflight > 0U) ? opt.inflight : 16U;
    const bool cb_mode = (opt.reply_mode == "cb");

    {
        char pbuf[256];
        std::snprintf(pbuf, sizeof(pbuf), "bytes=%zu;reply=%s;clients=%u;count=%u;warmup=%u", bytes,
                      opt.reply_mode.c_str(), P2Context::kNumClients, count, warmup);
        *out_params = pbuf;
    }

    for (uint32_t r = 0U; r < opt.runs; ++r)
    {
        const uint16_t port = static_cast<uint16_t>(opt.base_port + r * 10U);
        P2Context ctx;
        ctx.cb_reply_mode = cb_mode;
        ctx.warmup = warmup;
        ctx.count = count;
        for (uint32_t c = 0U; c < P2Context::kNumClients; ++c)
        {
            ctx.clients[c] = new P2ClientState();
            ctx.clients[c]->rtt_us.reserve(count);
        }
        s_p2_context = &ctx;

        auto cleanup_clients = [&]() {
            s_p2_context = nullptr;
            for (uint32_t c = 0U; c < P2Context::kNumClients; ++c)
            {
                delete ctx.clients[c];
                ctx.clients[c] = nullptr;
            }
        };

        potr_global_config global = make_default_global();
        global.udp_health_interval_ms = 50U;
        global.udp_health_timeout_ms = 10000U;

        potr_service_def srv_def = make_service_def(1LL, POTR_TYPE_UNICAST_BIDIR_N1, port, 0U, "", "127.0.0.1");
        int rc = potr_service_open(&global, &srv_def, POTR_ROLE_RECEIVER, p2_server_cb, &ctx.server);
        if (rc != POTR_OK)
        {
            std::fprintf(stderr, "[P2] run=%u stage=open_server err=%d (%s)\n", r + 1U, rc, potr_err_name(rc));
            cleanup_clients();
            return false;
        }

        std::thread ext_worker;
        if (!cb_mode)
        {
            ext_worker = std::thread([&]() {
                for (;;)
                {
                    P2ReplyItem item;
                    {
                        std::unique_lock<std::mutex> lk(ctx.q_mtx);
                        ctx.q_cv.wait(lk, [&]() { return !ctx.q.empty() || ctx.stop_worker.load(std::memory_order_acquire); });
                        if (ctx.q.empty())
                        {
                            if (ctx.stop_worker.load(std::memory_order_acquire))
                            {
                                break;
                            }
                            continue;
                        }
                        item = std::move(ctx.q.front());
                        ctx.q.pop_front();
                    }
                    const int sret = p2_send_reply_retry(ctx.server, item.peer_id, item.data.data(), item.data.size());
                    if (sret != POTR_OK && !ctx.closing.load(std::memory_order_acquire))
                    {
                        ctx.server_send_errors.fetch_add(1ULL, std::memory_order_relaxed);
                        std::fprintf(stderr, "[P2] stage=ext_reply peer_id=%u err=%d (%s)\n",
                                     static_cast<unsigned>(item.peer_id), sret, potr_err_name(sret));
                    }
                }
            });
        }

        potr_context *client_handles[P2Context::kNumClients]{};
        for (uint32_t c = 0U; c < P2Context::kNumClients; ++c)
        {
            potr_service_def cdef = make_service_def(1LL, POTR_TYPE_UNICAST_BIDIR, port, 0U, "127.0.0.1", "127.0.0.1");
            rc = potr_service_open(&global, &cdef, POTR_ROLE_SENDER, p2_client_cb, &client_handles[c]);
            if (rc != POTR_OK)
            {
                std::fprintf(stderr, "[P2] run=%u stage=open_client_%u err=%d (%s)\n", r + 1U, c, rc,
                             potr_err_name(rc));
                ctx.closing.store(true, std::memory_order_release);
                for (uint32_t j = 0U; j < c; ++j)
                {
                    potr_service_close(client_handles[j]);
                }
                ctx.stop_worker.store(true, std::memory_order_release);
                ctx.q_cv.notify_all();
                if (ext_worker.joinable())
                {
                    ext_worker.join();
                }
                potr_service_close(ctx.server);
                cleanup_clients();
                return false;
            }
        }

        /* サーバーと 4 クライアントの全員が POTR_EVENT_CONNECTED になるまで待つ。 */
        bool all_conn = false;
        {
            std::unique_lock<std::mutex> lk(ctx.srv_conn_mtx);
            all_conn = ctx.srv_conn_cv.wait_for(lk, std::chrono::seconds(10), [&]() {
                return ctx.srv_connected_peers >= P2Context::kNumClients &&
                       ctx.cli_connected_count >= P2Context::kNumClients;
            });
        }
        if (!all_conn)
        {
            std::fprintf(stderr, "[P2] run=%u stage=connect timeout (srv_peers=%u cli_conn=%u)\n", r + 1U,
                         ctx.srv_connected_peers, ctx.cli_connected_count);
            ctx.closing.store(true, std::memory_order_release);
            for (uint32_t c = 0U; c < P2Context::kNumClients; ++c)
            {
                potr_service_close(client_handles[c]);
            }
            ctx.stop_worker.store(true, std::memory_order_release);
            ctx.q_cv.notify_all();
            if (ext_worker.joinable())
            {
                ext_worker.join();
            }
            potr_service_close(ctx.server);
            cleanup_clients();
            return false;
        }

        std::mutex bar_mtx;
        std::condition_variable bar_cv;
        uint32_t bar_arrived = 0U;
        uint64_t meas_start_ns = 0ULL;

        std::vector<std::thread> cthreads;
        cthreads.reserve(P2Context::kNumClients);
        for (uint32_t spawn_index = 0U; spawn_index < P2Context::kNumClients; ++spawn_index)
        {
            /* 値捕捉の 4 [byte] がラムダのパディングになるため、添字は捕捉せず引数で渡す。 */
            cthreads.emplace_back([&](uint32_t c) {
                std::vector<uint8_t> payload(bytes, 0U);
                fill_payload_pattern(payload);
                write_u32_le(payload.data() + 8U, c);
                P2ClientState *cs = ctx.clients[c];

                auto send_req = [&](uint32_t seq) {
                    write_u32_le(payload.data() + 12U, seq);
                    int retry = 0;
                    for (;;)
                    {
                        write_u64_le(payload.data(), mono_now_ns());
                        const int sret = potr_service_send(client_handles[c], POTR_PEER_NA, payload.data(), bytes, 0);
                        if (sret == POTR_OK)
                        {
                            return;
                        }
                        if (sret == POTR_ERR_FULL && retry < 1000)
                        {
                            ctx.full_count.fetch_add(1ULL, std::memory_order_relaxed);
                            ++retry;
                            if (retry < 32)
                            {
                                std::this_thread::yield();
                            }
                            else
                            {
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            }
                            continue;
                        }
                        std::fprintf(stderr, "[P2] run=%u client=%u stage=send seq=%u err=%d (%s)\n", r + 1U, c, seq,
                                     sret, potr_err_name(sret));
                        return;
                    }
                };

                /* ウォーム アップ */
                for (uint32_t seq = 0U; seq < warmup; ++seq)
                {
                    {
                        std::unique_lock<std::mutex> lk(cs->mtx);
                        cs->cv.wait_for(lk, std::chrono::milliseconds(500),
                                        [&]() { return (seq - cs->replies) < inflight; });
                    }
                    send_req(seq);
                }
                {
                    std::unique_lock<std::mutex> lk(cs->mtx);
                    cs->cv.wait_for(lk, std::chrono::seconds(2), [&]() { return cs->replies >= warmup; });
                }

                /* 全クライアントをそろえる。 */
                {
                    std::unique_lock<std::mutex> lk(bar_mtx);
                    ++bar_arrived;
                    if (bar_arrived == P2Context::kNumClients)
                    {
                        meas_start_ns = mono_now_ns();
                        ctx.last_reply_ns.store(meas_start_ns, std::memory_order_release);
                        bar_cv.notify_all();
                    }
                    else
                    {
                        bar_cv.wait(lk, [&]() { return bar_arrived == P2Context::kNumClients; });
                    }
                }

                /* 測定 */
                for (uint32_t k = 0U; k < count; ++k)
                {
                    const uint32_t seq = warmup + k;
                    {
                        std::unique_lock<std::mutex> lk(cs->mtx);
                        cs->cv.wait_for(lk, std::chrono::milliseconds(500),
                                        [&]() { return (seq - cs->replies) < inflight; });
                    }
                    send_req(seq);
                }
            }, spawn_index);
        }

        for (auto &t : cthreads)
        {
            t.join();
        }
        const uint64_t send_done_ns = mono_now_ns();
        if (ctx.last_reply_ns.load(std::memory_order_acquire) < send_done_ns)
        {
            ctx.last_reply_ns.store(send_done_ns, std::memory_order_release);
        }

        /* 返信がそろうか、最後の返信または送信完了から 2 [s] 無通知になるまで待つ。 */
        for (;;)
        {
            size_t total_post = 0U;
            for (uint32_t c = 0U; c < P2Context::kNumClients; ++c)
            {
                std::lock_guard<std::mutex> lk(ctx.clients[c]->mtx);
                total_post += ctx.clients[c]->rtt_us.size();
            }
            if (total_post >= static_cast<size_t>(P2Context::kNumClients) * count)
            {
                break;
            }
            const uint64_t now = mono_now_ns();
            const uint64_t last = ctx.last_reply_ns.load(std::memory_order_acquire);
            if (now > last && (now - last) >= 2000000000ULL)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const uint64_t meas_end_ns = ctx.last_reply_ns.load(std::memory_order_acquire);

        ctx.closing.store(true, std::memory_order_release);
        for (uint32_t c = 0U; c < P2Context::kNumClients; ++c)
        {
            potr_service_close(client_handles[c]);
        }
        ctx.stop_worker.store(true, std::memory_order_release);
        ctx.q_cv.notify_all();
        if (ext_worker.joinable())
        {
            ext_worker.join();
        }
        potr_service_close(ctx.server);

        std::vector<double> all_rtts;
        for (uint32_t c = 0U; c < P2Context::kNumClients; ++c)
        {
            all_rtts.insert(all_rtts.end(), ctx.clients[c]->rtt_us.begin(), ctx.clients[c]->rtt_us.end());
        }
        const size_t expected = static_cast<size_t>(P2Context::kNumClients) * count;
        const size_t received = all_rtts.size();
        const size_t dropped = (expected > received) ? (expected - received) : 0U;
        std::sort(all_rtts.begin(), all_rtts.end());

        const double elapsed_s =
            (meas_end_ns > meas_start_ns) ? (static_cast<double>(meas_end_ns - meas_start_ns) / 1e9) : 0.0;
        const double throughput = (elapsed_s > 0.0) ? (static_cast<double>(received) / elapsed_s) : 0.0;

        RunMetrics rm;
        rm.add("rtt_p50_us", percentile_sorted(all_rtts, 0.50));
        rm.add("rtt_p95_us", percentile_sorted(all_rtts, 0.95));
        rm.add("rtt_p99_us", percentile_sorted(all_rtts, 0.99));
        rm.add("throughput_msg_s", throughput);
        rm.add("dropped_count", static_cast<double>(dropped));
        rm.add("send_full_count", static_cast<double>(ctx.full_count.load(std::memory_order_acquire)));
        out_runs->push_back(std::move(rm));

        cleanup_clients();
    }
    return true;
}

/* ===== P3 の実装 (受信コールバックが 1 [ms] 待機する場合) ===== */

struct P3Context
{
    uint32_t warmup = 0U;
    uint32_t count = 0U;
    uint32_t cb_sleep_ms = 1U;

    std::atomic<uint32_t> warmup_rx{0U};
    std::atomic<uint32_t> post_rx{0U};
    std::atomic<uint32_t> post_consumed{0U};
    std::atomic<uint64_t> last_rx_ns{0ULL};

    std::mutex lat_mtx;
    std::vector<double> notify_lat_us;
};

static P3Context *s_p3_context = nullptr;

static void p3_receiver_cb(int64_t /*service_id*/, potr_peer_id /*peer_id*/, potr_event event, const void *data,
                           size_t len)
{
    const uint64_t rx_ns = mono_now_ns();
    P3Context *ctx = s_p3_context;
    if (ctx == nullptr || event != POTR_EVENT_DATA || data == nullptr || len < 16U)
    {
        return;
    }
    ctx->last_rx_ns.store(rx_ns, std::memory_order_release);
    const uint8_t *p = static_cast<const uint8_t *>(data);
    const uint64_t tx_ns = read_u64_le(p);
    const uint32_t seq = read_u32_le(p + 12U);

    if (seq < ctx->warmup)
    {
        if (ctx->cb_sleep_ms > 0U)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(ctx->cb_sleep_ms));
        }
        ctx->warmup_rx.fetch_add(1U, std::memory_order_release);
        return;
    }

    if ((seq - ctx->warmup) < ctx->count)
    {
        const uint64_t diff = (rx_ns > tx_ns) ? (rx_ns - tx_ns) : 0ULL;
        {
            std::lock_guard<std::mutex> lk(ctx->lat_mtx);
            ctx->notify_lat_us.push_back(static_cast<double>(diff) / 1000.0);
        }
        ctx->post_rx.fetch_add(1U, std::memory_order_release);
    }

    if (ctx->cb_sleep_ms > 0U)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ctx->cb_sleep_ms));
    }
    ctx->post_consumed.fetch_add(1U, std::memory_order_release);
}

static bool run_p3(const Options &opt, std::vector<RunMetrics> *out_runs, std::string *out_params)
{
    const size_t bytes = (opt.bytes > 0U) ? opt.bytes : 5000U;
    if (bytes < 16U)
    {
        std::fprintf(stderr, "--bytes must be >= 16\n");
        return false;
    }
    const uint32_t count = (opt.count > 0U) ? opt.count : 10000U;
    const uint32_t warmup = (opt.warmup != UINT32_MAX) ? opt.warmup : 1000U;
    const uint32_t cb_sleep_ms = opt.cb_sleep_ms;

    {
        char pbuf[256];
        std::snprintf(pbuf, sizeof(pbuf), "bytes=%zu;count=%u;warmup=%u;cb_sleep_ms=%u", bytes, count, warmup,
                      cb_sleep_ms);
        *out_params = pbuf;
    }

#ifdef _WIN32
    timeBeginPeriod(1U);
    struct WinTimerReset
    {
        ~WinTimerReset()
        {
            timeEndPeriod(1U);
        }
    } timer_reset;
#endif

    for (uint32_t r = 0U; r < opt.runs; ++r)
    {
        const uint16_t port = static_cast<uint16_t>(opt.base_port + r * 10U);
        P3Context ctx;
        ctx.warmup = warmup;
        ctx.count = count;
        ctx.cb_sleep_ms = cb_sleep_ms;
        ctx.notify_lat_us.reserve(count);
        ctx.last_rx_ns.store(mono_now_ns(), std::memory_order_release);
        s_p3_context = &ctx;

        potr_global_config global = make_default_global();
        potr_service_def def = make_service_def(1LL, POTR_TYPE_UNICAST, port, 0U, "127.0.0.1", "127.0.0.1");

        potr_context *receiver = nullptr;
        potr_context *sender = nullptr;
        int rc = potr_service_open(&global, &def, POTR_ROLE_RECEIVER, p3_receiver_cb, &receiver);
        if (rc != POTR_OK)
        {
            std::fprintf(stderr, "[P3] run=%u stage=open_receiver err=%d (%s)\n", r + 1U, rc, potr_err_name(rc));
            s_p3_context = nullptr;
            return false;
        }
        rc = potr_service_open(&global, &def, POTR_ROLE_SENDER, nullptr, &sender);
        if (rc != POTR_OK)
        {
            std::fprintf(stderr, "[P3] run=%u stage=open_sender err=%d (%s)\n", r + 1U, rc, potr_err_name(rc));
            potr_service_close(receiver);
            s_p3_context = nullptr;
            return false;
        }

        std::vector<uint8_t> payload(bytes, 0U);
        fill_payload_pattern(payload);

        uint64_t full_count = 0ULL; /* 測定区間で POTR_ERR_FULL を受けた回数 */

        auto send_seq = [&](uint32_t seq, const char *stage) -> bool {
            write_u32_le(payload.data() + 12U, seq);
            int retry = 0;
            for (;;)
            {
                write_u64_le(payload.data(), mono_now_ns());
                const int sret = potr_service_send(sender, POTR_PEER_NA, payload.data(), bytes, 0);
                if (sret == POTR_OK)
                {
                    return true;
                }
                if (sret == POTR_ERR_FULL && retry < 60000)
                {
                    /* 送信キューに空きがない場合は、少し待って送り直す (P1 の send_one を参照)。 */
                    if (seq >= warmup)
                    {
                        ++full_count;
                    }
                    ++retry;
                    if (retry < 32)
                    {
                        std::this_thread::yield();
                    }
                    else
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    continue;
                }
                std::fprintf(stderr, "[P3] run=%u stage=%s seq=%u err=%d (%s)\n", r + 1U, stage, seq, sret,
                             potr_err_name(sret));
                return false;
            }
        };

        /* ウォーム アップの送信と受信の完了待ち */
        bool send_ok = true;
        for (uint32_t seq = 0U; seq < warmup; ++seq)
        {
            if (!send_seq(seq, "warmup_send"))
            {
                send_ok = false;
                break;
            }
        }
        if (warmup > 0U && send_ok)
        {
            const uint64_t w_send_done = mono_now_ns();
            if (ctx.last_rx_ns.load(std::memory_order_acquire) < w_send_done)
            {
                ctx.last_rx_ns.store(w_send_done, std::memory_order_release);
            }
            for (;;)
            {
                if (ctx.warmup_rx.load(std::memory_order_acquire) >= warmup)
                {
                    break;
                }
                const uint64_t now = mono_now_ns();
                const uint64_t last = ctx.last_rx_ns.load(std::memory_order_acquire);
                if (now > last && (now - last) >= 2000000000ULL)
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        /* ウォーム アップ後を基準とする。 */
        const MemSample base = sample_memory(false);
        std::atomic<bool> stop_sampler{false};
        std::atomic<uint64_t> peak_rss{base.rss_bytes};
        std::atomic<uint64_t> peak_priv{base.priv_bytes};

        std::thread sampler([&]() {
            while (!stop_sampler.load(std::memory_order_acquire))
            {
                const MemSample s = sample_memory(false);
                uint64_t pr = peak_rss.load(std::memory_order_relaxed);
                while (s.rss_bytes > pr && !peak_rss.compare_exchange_weak(pr, s.rss_bytes))
                {
                }
                uint64_t pp = peak_priv.load(std::memory_order_relaxed);
                while (s.priv_bytes > pp && !peak_priv.compare_exchange_weak(pp, s.priv_bytes))
                {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

        /* 間隔を空けずに測定対象を送信する。 */
        if (send_ok)
        {
            for (uint32_t k = 0U; k < count; ++k)
            {
                if (!send_seq(warmup + k, "send"))
                {
                    break;
                }
            }
        }
        const uint64_t send_done_ns = mono_now_ns();
        if (ctx.last_rx_ns.load(std::memory_order_acquire) < send_done_ns)
        {
            ctx.last_rx_ns.store(send_done_ns, std::memory_order_release);
        }

        /* 完了条件: 件数に到達するか、最後の受信から 5 [s] 無通知になる。 */
        for (;;)
        {
            const uint32_t rx = ctx.post_rx.load(std::memory_order_acquire);
            const uint32_t consumed = ctx.post_consumed.load(std::memory_order_acquire);
            if (rx >= count && consumed >= rx)
            {
                break;
            }
            const uint64_t now = mono_now_ns();
            const uint64_t last = ctx.last_rx_ns.load(std::memory_order_acquire);
            if (now > last && (now - last) >= 5000000000ULL && consumed >= rx)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        /* すべての通知を処理してから 1 [s] 待つ。 */
        std::this_thread::sleep_for(std::chrono::seconds(1));
        stop_sampler.store(true, std::memory_order_release);
        sampler.join();

        const MemSample after1s = sample_memory(false);

        potr_service_close(sender);
        potr_service_close(receiver);

        std::vector<double> lats;
        {
            std::lock_guard<std::mutex> lk(ctx.lat_mtx);
            lats = std::move(ctx.notify_lat_us);
        }
        const uint32_t rx_count = static_cast<uint32_t>(lats.size());
        const uint32_t dropped = (count > rx_count) ? (count - rx_count) : 0U;
        std::sort(lats.begin(), lats.end());
        const double max_lat = lats.empty() ? 0.0 : lats.back();

        const uint64_t pr = peak_rss.load(std::memory_order_acquire);
        const uint64_t pp = peak_priv.load(std::memory_order_acquire);
        const int64_t peak_rss_inc = static_cast<int64_t>(pr) - static_cast<int64_t>(base.rss_bytes);
        const int64_t peak_priv_inc = static_cast<int64_t>(pp) - static_cast<int64_t>(base.priv_bytes);
        const int64_t after1s_rss_inc = static_cast<int64_t>(after1s.rss_bytes) - static_cast<int64_t>(base.rss_bytes);
        const int64_t after1s_priv_inc = static_cast<int64_t>(after1s.priv_bytes) - static_cast<int64_t>(base.priv_bytes);

        RunMetrics rm;
        rm.add("notify_p50_us", percentile_sorted(lats, 0.50));
        rm.add("notify_p95_us", percentile_sorted(lats, 0.95));
        rm.add("notify_p99_us", percentile_sorted(lats, 0.99));
        rm.add("notify_max_us", max_lat);
        rm.add("peak_rss_inc_bytes", static_cast<double>(peak_rss_inc));
        rm.add("peak_priv_inc_bytes", static_cast<double>(peak_priv_inc));
        rm.add("after1s_rss_inc_bytes", static_cast<double>(after1s_rss_inc));
        rm.add("after1s_priv_inc_bytes", static_cast<double>(after1s_priv_inc));
        rm.add("dropped_count", static_cast<double>(dropped));
        rm.add("send_full_count", static_cast<double>(full_count));
        out_runs->push_back(std::move(rm));

        s_p3_context = nullptr;
    }
    return true;
}

/* ===== P4 の実装 (open、接続、送受信、close の反復) ===== */

struct P4Context
{
    std::mutex mtx;
    std::condition_variable cv;
    uint32_t expected_seq = 0U;
    bool recv_connected = false;
    bool send_connected = false;
    bool data_received = false;
    unsigned char pad[1] = {}; // 明示的アラインメントのためのパディング。
};

static P4Context *s_p4_context = nullptr;

static void p4_recv_cb(int64_t /*service_id*/, potr_peer_id /*peer_id*/, potr_event event, const void *data, size_t len)
{
    P4Context *ctx = s_p4_context;
    if (ctx == nullptr)
    {
        return;
    }
    if (event == POTR_EVENT_CONNECTED)
    {
        std::lock_guard<std::mutex> lk(ctx->mtx);
        ctx->recv_connected = true;
        ctx->cv.notify_all();
    }
    else if (event == POTR_EVENT_DATA && data != nullptr && len >= 16U)
    {
        const uint32_t seq = read_u32_le(static_cast<const uint8_t *>(data) + 12U);
        std::lock_guard<std::mutex> lk(ctx->mtx);
        if (seq == ctx->expected_seq)
        {
            ctx->data_received = true;
            ctx->cv.notify_all();
        }
    }
}

static void p4_send_cb(int64_t /*service_id*/, potr_peer_id /*peer_id*/, potr_event event, const void * /*data*/,
                       size_t /*len*/)
{
    P4Context *ctx = s_p4_context;
    if (ctx == nullptr)
    {
        return;
    }
    if (event == POTR_EVENT_CONNECTED)
    {
        std::lock_guard<std::mutex> lk(ctx->mtx);
        ctx->send_connected = true;
        ctx->cv.notify_all();
    }
}

static bool run_p4(const Options &opt, std::vector<RunMetrics> *out_runs, std::string *out_params)
{
    if (opt.proto != "udp" && opt.proto != "tcp")
    {
        std::fprintf(stderr, "Invalid --proto: %s (expected udp or tcp)\n", opt.proto.c_str());
        return false;
    }
    const bool is_tcp = (opt.proto == "tcp");
    const bool tcp_bidir = (opt.tcp_type == "bidir");
    const size_t bytes = (opt.bytes > 0U) ? opt.bytes : 64U;
    if (bytes < 16U)
    {
        std::fprintf(stderr, "--bytes must be >= 16\n");
        return false;
    }
    const uint32_t cycles = (opt.count > 0U) ? opt.count : 1000U;
    const uint32_t warmup = (opt.warmup != UINT32_MAX) ? opt.warmup : 10U;

    {
        char pbuf[256];
        if (is_tcp)
        {
            std::snprintf(pbuf, sizeof(pbuf), "proto=tcp;tcp_type=%s;bytes=%zu;cycles=%u;warmup=%u",
                          opt.tcp_type.c_str(), bytes, cycles, warmup);
        }
        else
        {
            std::snprintf(pbuf, sizeof(pbuf), "proto=udp;bytes=%zu;cycles=%u;warmup=%u", bytes, cycles, warmup);
        }
        *out_params = pbuf;
    }

    const potr_type ptype = is_tcp ? (tcp_bidir ? POTR_TYPE_TCP_BIDIR : POTR_TYPE_TCP) : POTR_TYPE_UNICAST;

    for (uint32_t r = 0U; r < opt.runs; ++r)
    {
        const uint16_t run_base_port = static_cast<uint16_t>(opt.base_port + r * 200U);
        std::vector<uint8_t> payload(bytes, 0U);
        fill_payload_pattern(payload);

        potr_global_config global = make_default_global();
        if (!is_tcp)
        {
            /* open と close の反復で、受信側が数 [ms] で POTR_EVENT_CONNECTED を観測できるよう、PING の間隔を短くする。 */
            global.udp_health_interval_ms = 5U;
            global.udp_health_timeout_ms = 5000U;
        }
        else
        {
            global.tcp_health_interval_ms = 50U;
            global.tcp_health_timeout_ms = 5000U;
            global.tcp_close_timeout_ms = 5000U;
        }

        std::vector<double> close_us;
        close_us.reserve(cycles);
        std::vector<double> xs;
        std::vector<double> rss_inc_series;
        std::vector<double> priv_inc_series;
        std::vector<double> handle_series;
        std::vector<double> thread_series;
        xs.reserve(cycles + 1U);
        rss_inc_series.reserve(cycles + 1U);
        priv_inc_series.reserve(cycles + 1U);
        handle_series.reserve(cycles + 1U);
        thread_series.reserve(cycles + 1U);

        struct Checkpoint
        {
            double rss_inc;
            double priv_inc;
            double handles;
            double threads;
            uint32_t cycle;
            uint32_t pad; // 明示的アラインメントのためのパディング。
        };
        std::vector<Checkpoint> checkpoints;

        MemSample base{};
        auto record_sample = [&](uint32_t meas_idx, bool is_checkpoint) {
            const MemSample s = sample_memory(true);
            if (meas_idx == 0U)
            {
                base = s;
            }
            const double ri = static_cast<double>(static_cast<int64_t>(s.rss_bytes) - static_cast<int64_t>(base.rss_bytes));
            const double pi =
                static_cast<double>(static_cast<int64_t>(s.priv_bytes) - static_cast<int64_t>(base.priv_bytes));
            const double hd = static_cast<double>(s.handles);
            const double th = static_cast<double>(s.threads);
            xs.push_back(static_cast<double>(meas_idx));
            rss_inc_series.push_back(ri);
            priv_inc_series.push_back(pi);
            handle_series.push_back(hd);
            thread_series.push_back(th);
            if (is_checkpoint)
            {
                checkpoints.push_back(Checkpoint{ri, pi, hd, th, meas_idx, 0U});
            }
        };

        if (warmup == 0U)
        {
            record_sample(0U, true);
        }

        for (uint32_t c = 0U; c < warmup + cycles; ++c)
        {
            const uint16_t port = static_cast<uint16_t>(run_base_port + (c % 200U));
            P4Context ctx;
            ctx.expected_seq = c;
            s_p4_context = &ctx;

            potr_service_def def = make_service_def(1LL, ptype, port, 0U, "127.0.0.1", "127.0.0.1");
            potr_context *receiver = nullptr;
            potr_context *sender = nullptr;

            int rc = potr_service_open(&global, &def, POTR_ROLE_RECEIVER, p4_recv_cb, &receiver);
            if (rc != POTR_OK)
            {
                std::fprintf(stderr, "[P4] run=%u cycle=%u stage=open_receiver err=%d (%s)\n", r + 1U, c, rc,
                             potr_err_name(rc));
                s_p4_context = nullptr;
                return false;
            }
            const potr_recv_fn scb = (is_tcp && tcp_bidir) ? p4_send_cb : nullptr;
            rc = potr_service_open(&global, &def, POTR_ROLE_SENDER, scb, &sender);
            if (rc != POTR_OK)
            {
                std::fprintf(stderr, "[P4] run=%u cycle=%u stage=open_sender err=%d (%s)\n", r + 1U, c, rc,
                             potr_err_name(rc));
                potr_service_close(receiver);
                s_p4_context = nullptr;
                return false;
            }

            {
                std::unique_lock<std::mutex> lk(ctx.mtx);
                const bool conn_ok = ctx.cv.wait_for(lk, std::chrono::seconds(10), [&]() {
                    return ctx.recv_connected && (!(is_tcp && tcp_bidir) || ctx.send_connected);
                });
                if (!conn_ok)
                {
                    std::fprintf(stderr, "[P4] run=%u cycle=%u stage=connect timeout\n", r + 1U, c);
                    potr_service_close(sender);
                    potr_service_close(receiver);
                    s_p4_context = nullptr;
                    return false;
                }
            }

            write_u64_le(payload.data(), mono_now_ns());
            write_u32_le(payload.data() + 12U, c);
            int retry = 0;
            for (;;)
            {
                rc = potr_service_send(sender, POTR_PEER_NA, payload.data(), bytes, 0);
                if (rc == POTR_OK)
                {
                    break;
                }
                /* 送信キューが満杯の場合と、TCP 片方向で接続前の場合は、少し待って送り直す。 */
                if ((rc == POTR_ERR_DISCONNECTED && is_tcp && !tcp_bidir && retry < 2000) ||
                    (rc == POTR_ERR_FULL && retry < 1000))
                {
                    ++retry;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                std::fprintf(stderr, "[P4] run=%u cycle=%u stage=send err=%d (%s)\n", r + 1U, c, rc, potr_err_name(rc));
                potr_service_close(sender);
                potr_service_close(receiver);
                s_p4_context = nullptr;
                return false;
            }

            {
                std::unique_lock<std::mutex> lk(ctx.mtx);
                const bool rx_ok = ctx.cv.wait_for(lk, std::chrono::seconds(10), [&]() { return ctx.data_received; });
                if (!rx_ok)
                {
                    std::fprintf(stderr, "[P4] run=%u cycle=%u stage=recv timeout\n", r + 1U, c);
                    potr_service_close(sender);
                    potr_service_close(receiver);
                    s_p4_context = nullptr;
                    return false;
                }
            }

            const uint64_t t0 = mono_now_ns();
            const int rcs = potr_service_close(sender);
            const int rcr = potr_service_close(receiver);
            const uint64_t t1 = mono_now_ns();
            s_p4_context = nullptr;

            if (rcs != POTR_OK || rcr != POTR_OK)
            {
                std::fprintf(stderr, "[P4] run=%u cycle=%u stage=close sender_err=%d receiver_err=%d\n", r + 1U, c,
                             rcs, rcr);
                return false;
            }

            if (c + 1U == warmup)
            {
                record_sample(0U, true);
            }
            else if (c >= warmup)
            {
                const uint32_t k = c - warmup + 1U;
                close_us.push_back(static_cast<double>(t1 - t0) / 1000.0);
                const bool is_cp = ((k % 100U) == 0U) || (k == cycles);
                /* Windows のスレッド数取得 (Toolhelp32) は重いため、傾きの標本は 10 サイクルごとに取る。 */
                if (is_cp || ((k % 10U) == 0U))
                {
                    record_sample(k, is_cp);
                }
            }
        }

        std::sort(close_us.begin(), close_us.end());
        RunMetrics rm;
        rm.add("close_p50_us", percentile_sorted(close_us, 0.50));
        rm.add("close_p95_us", percentile_sorted(close_us, 0.95));
        rm.add("close_p99_us", percentile_sorted(close_us, 0.99));
        rm.add("rss_slope_bytes_per_cycle", linear_slope(xs, rss_inc_series));
        rm.add("priv_slope_bytes_per_cycle", linear_slope(xs, priv_inc_series));
        rm.add("handles_slope_per_cycle", linear_slope(xs, handle_series));
        rm.add("threads_slope_per_cycle", linear_slope(xs, thread_series));

        for (const auto &cp : checkpoints)
        {
            char suffix[32];
            std::snprintf(suffix, sizeof(suffix), "_c%u", cp.cycle);
            rm.add(std::string("rss_inc_bytes") + suffix, cp.rss_inc);
            rm.add(std::string("priv_inc_bytes") + suffix, cp.priv_inc);
            rm.add(std::string("handles") + suffix, cp.handles);
            rm.add(std::string("threads") + suffix, cp.threads);
        }
        out_runs->push_back(std::move(rm));
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    Options opt;

    cplat_console_init();

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto need_val = [&](const char *name) -> const char * {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "Missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h")
        {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        else if (arg == "--cond")
        {
            const char *v = need_val("--cond");
            if (v == nullptr)
                return EXIT_FAILURE;
            opt.cond = to_lower_str(v);
        }
        else if (arg == "--label")
        {
            const char *v = need_val("--label");
            if (v == nullptr)
            {
                return EXIT_FAILURE;
            }
            opt.label = v;
            if (!is_valid_label(opt.label))
            {
                std::fprintf(stderr, "Invalid --label: %s (use letters, digits, '.', '_' and '-')\n", v);
                return EXIT_FAILURE;
            }
        }
        else if (arg == "--bytes")
        {
            const char *v = need_val("--bytes");
            uint32_t u = 0U;
            if (v == nullptr || !parse_u32(v, &u))
                return EXIT_FAILURE;
            opt.bytes = u;
        }
        else if (arg == "--flags")
        {
            const char *v = need_val("--flags");
            if (v == nullptr)
                return EXIT_FAILURE;
            opt.flags = to_lower_str(v);
        }
        else if (arg == "--threads")
        {
            const char *v = need_val("--threads");
            if (v == nullptr || !parse_u32(v, &opt.threads) || opt.threads == 0U)
                return EXIT_FAILURE;
        }
        else if (arg == "--count")
        {
            const char *v = need_val("--count");
            if (v == nullptr || !parse_u32(v, &opt.count) || opt.count == 0U)
                return EXIT_FAILURE;
        }
        else if (arg == "--runs")
        {
            const char *v = need_val("--runs");
            if (v == nullptr || !parse_u32(v, &opt.runs) || opt.runs == 0U)
                return EXIT_FAILURE;
        }
        else if (arg == "--warmup")
        {
            const char *v = need_val("--warmup");
            if (v == nullptr || !parse_u32(v, &opt.warmup))
                return EXIT_FAILURE;
        }
        else if (arg == "--inflight")
        {
            const char *v = need_val("--inflight");
            if (v == nullptr || !parse_u32(v, &opt.inflight))
                return EXIT_FAILURE;
        }
        else if (arg == "--port")
        {
            const char *v = need_val("--port");
            uint32_t u = 0U;
            if (v == nullptr || !parse_u32(v, &u) || u == 0U || u > 60000U)
                return EXIT_FAILURE;
            opt.base_port = static_cast<uint16_t>(u);
        }
        else if (arg == "--reply-mode")
        {
            const char *v = need_val("--reply-mode");
            if (v == nullptr)
                return EXIT_FAILURE;
            opt.reply_mode = to_lower_str(v);
        }
        else if (arg == "--proto")
        {
            const char *v = need_val("--proto");
            if (v == nullptr)
                return EXIT_FAILURE;
            opt.proto = to_lower_str(v);
        }
        else if (arg == "--tcp-type")
        {
            const char *v = need_val("--tcp-type");
            if (v == nullptr)
                return EXIT_FAILURE;
            opt.tcp_type = to_lower_str(v);
            if (opt.tcp_type != "bidir" && opt.tcp_type != "oneway")
            {
                std::fprintf(stderr, "Invalid --tcp-type: %s\n", opt.tcp_type.c_str());
                return EXIT_FAILURE;
            }
        }
        else if (arg == "--cb-sleep-ms")
        {
            const char *v = need_val("--cb-sleep-ms");
            if (v == nullptr || !parse_u32(v, &opt.cb_sleep_ms))
                return EXIT_FAILURE;
        }
        else if (arg == "--out")
        {
            const char *v = need_val("--out");
            if (v == nullptr)
                return EXIT_FAILURE;
            opt.out_path = v;
        }
        else if (arg == "--no-header")
        {
            opt.no_header = true;
        }
        else if (arg == "--trace-level")
        {
            const char *v = need_val("--trace-level");
            if (v == nullptr)
                return EXIT_FAILURE;
            opt.trace_level = to_lower_str(v);
        }
        else
        {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (opt.cond != "p1" && opt.cond != "p2" && opt.cond != "p3" && opt.cond != "p4" && opt.cond != "p5")
    {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    cplat_trace_level tlevel = CPLAT_TRACE_LEVEL_WARNING;
    if (opt.trace_level == "none")
        tlevel = CPLAT_TRACE_LEVEL_NONE;
    else if (opt.trace_level == "error")
        tlevel = CPLAT_TRACE_LEVEL_ERROR;
    else if (opt.trace_level == "warning")
        tlevel = CPLAT_TRACE_LEVEL_WARNING;
    else if (opt.trace_level == "info")
        tlevel = CPLAT_TRACE_LEVEL_INFO;
    else if (opt.trace_level == "debug")
        tlevel = CPLAT_TRACE_LEVEL_DEBUG;
    else
    {
        std::fprintf(stderr, "Invalid --trace-level: %s\n", opt.trace_level.c_str());
        return EXIT_FAILURE;
    }

    cplat_tracer *tracer = potr_tracer_get();
    if (tracer != nullptr)
    {
        if (cplat_tracer_set_stderr_level(tracer, tlevel) != CPLAT_OK || cplat_tracer_start(tracer) != CPLAT_OK)
        {
            std::fprintf(stderr, "Failed to start the porter tracer\n");
            return EXIT_FAILURE;
        }
    }

    FILE *out = stdout;
    if (!opt.out_path.empty())
    {
#ifdef _WIN32
        if (fopen_s(&out, opt.out_path.c_str(), "w") != 0)
        {
            out = nullptr;
        }
#else
        out = std::fopen(opt.out_path.c_str(), "w");
#endif
        if (out == nullptr)
        {
            std::fprintf(stderr, "Failed to open output file: %s\n", opt.out_path.c_str());
            return EXIT_FAILURE;
        }
    }

    std::vector<RunMetrics> runs;
    std::string params;
    std::string cond_upper;
    int exit_code = EXIT_SUCCESS;

    if (opt.cond == "p1" || opt.cond == "p5")
    {
        cond_upper = (opt.cond == "p5") ? "P5" : "P1";
        if (!run_p1_or_p5(opt, &runs, &params))
        {
            exit_code = EXIT_FAILURE;
        }
    }
    else if (opt.cond == "p2")
    {
        cond_upper = "P2";
        if (!run_p2(opt, &runs, &params))
        {
            exit_code = EXIT_FAILURE;
        }
    }
    else if (opt.cond == "p3")
    {
        cond_upper = "P3";
        if (!run_p3(opt, &runs, &params))
        {
            exit_code = EXIT_FAILURE;
        }
    }
    else if (opt.cond == "p4")
    {
        cond_upper = "P4";
        if (!run_p4(opt, &runs, &params))
        {
            exit_code = EXIT_FAILURE;
        }
    }

    if (exit_code == EXIT_SUCCESS)
    {
        emit_csv(out, opt.label, cond_upper, params, runs, !opt.no_header);
    }

    if (!opt.out_path.empty() && out != nullptr)
    {
        std::fclose(out);
    }
    return exit_code;
}
