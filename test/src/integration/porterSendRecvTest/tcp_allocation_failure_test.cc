#include <testfw.h>
#include <porter.h>
#include <porter/porter_result.h>
#include <cplat/compress/compress.h>
#include <cplat/crt/stdlib.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#if defined(PLATFORM_LINUX)
    #include <dlfcn.h>

namespace
{
constexpr size_t payload_size = 512;
constexpr size_t message_size = 8192;
constexpr size_t wire_size = offsetof(potr_packet, payload) + payload_size;
constexpr size_t compress_size = CPLAT_COMPRESS_HEADER_SIZE + message_size + 64;
constexpr size_t crypto_size = payload_size + POTR_CRYPTO_TAG_SIZE;

struct allocation_probe
{
    std::thread::id owner = std::this_thread::get_id();
    size_t fail_size = 0;
    size_t fail_occurrence = 0;
    size_t matching_calls = 0;
    size_t failures = 0;
    size_t allocated = 0;
    size_t freed = 0;
    size_t overflow = 0;
    std::array<void *, 128> live = {};
};

std::mutex s_probe_mutex;
allocation_probe *s_probe = nullptr;

using malloc_fn = void *(*)(size_t);
using free_fn = void (*)(void *);

/* リンク必須の libcplat が公開する実関数を解決できない状態は、試験基盤の不変条件違反です。
 * NULL で続行すると偽の確保失敗となり、free を省略すると解放検証が成立しないため停止します。 */
malloc_fn real_malloc()
{
    static auto fn = reinterpret_cast<malloc_fn>(dlsym(RTLD_NEXT, "cplat_malloc"));
    if (fn == nullptr)
    {
        std::abort();
    }
    return fn;
}

free_fn real_free()
{
    static auto fn = reinterpret_cast<free_fn>(dlsym(RTLD_NEXT, "cplat_free"));
    if (fn == nullptr)
    {
        std::abort();
    }
    return fn;
}

void receive(int64_t, potr_peer_id, potr_event, const void *, size_t) {}
} // namespace

/* porter からの動的参照だけを差し替え、通常時は実 cplat へ委譲します。
 * libcplat 内でローカルに束縛された確保や calloc は追跡対象外です。
 * see: https://man7.org/linux/man-pages/man3/dlsym.3.html
 * 注入は open を呼ぶスレッドに限定し、別スレッドからの解放も照合します。 */
extern "C" void *CPLAT_API cplat_malloc(size_t size)
{
    auto fn = real_malloc();
    std::lock_guard<std::mutex> lock(s_probe_mutex);
    bool tracked = s_probe != nullptr && s_probe->owner == std::this_thread::get_id();
    if (tracked && size == s_probe->fail_size && ++s_probe->matching_calls == s_probe->fail_occurrence)
    {
        ++s_probe->failures;
        return nullptr;
    }
    void *ptr = fn(size);
    if (tracked && ptr != nullptr)
    {
        ++s_probe->allocated;
        for (auto &slot : s_probe->live)
        {
            if (slot == nullptr)
            {
                slot = ptr;
                return ptr;
            }
        }
        s_probe->overflow = 1;
    }
    return ptr;
}

extern "C" void CPLAT_API cplat_free(void *ptr)
{
    auto fn = real_free();
    std::lock_guard<std::mutex> lock(s_probe_mutex);
    if (s_probe != nullptr && ptr != nullptr)
    {
        for (auto &slot : s_probe->live)
        {
            if (slot == ptr)
            {
                slot = nullptr;
                ++s_probe->freed;
                break;
            }
        }
    }
    fn(ptr);
}
#endif

struct allocation_case
{
    const char *name;
    size_t size;
    size_t occurrence;
};

class tcp_allocation_failure_test : public TestWithParam<allocation_case>
{
#if defined(PLATFORM_LINUX)
  protected:
    potr_global_config global = {};
    uint32_t reserved = 0;
    potr_service_def service = {};
    potr_context *handle = nullptr;
    allocation_probe probe;

    void SetUp() override
    {
        // プロセス共通トレーサーの寿命は service_close を超えるため、追跡開始前に初期化します。
        ASSERT_NE(nullptr, potr_tracer_get());
        global.window_size = 16;
        global.max_payload = payload_size;
        global.max_message_size = message_size;
        global.send_queue_depth = 16;
        service.service_id = 7183;
        service.type = POTR_TYPE_TCP;
        service.dst_port = 19873;
        service.encrypt_enabled = 1;
        for (size_t i = 0; i < POTR_MAX_PATH; ++i)
        {
            strcpy(service.src_addr[i], "127.0.0.1");
            strcpy(service.dst_addr[i], "127.0.0.1");
            service.dst_addr[i][8] = static_cast<char>('1' + i);
        }
        std::lock_guard<std::mutex> lock(s_probe_mutex);
        s_probe = &probe;
    }

    void arm(size_t size, size_t occurrence)
    {
        std::lock_guard<std::mutex> lock(s_probe_mutex);
        probe.fail_size = size;
        probe.fail_occurrence = occurrence;
        probe.matching_calls = 0;
        probe.failures = 0;
    }

    allocation_probe snapshot()
    {
        std::lock_guard<std::mutex> lock(s_probe_mutex);
        return probe;
    }

    int open()
    {
        return potr_service_open(&global, &service, POTR_ROLE_RECEIVER, receive, &handle);
    }

    int close()
    {
        int ret = potr_service_close(handle);
        handle = nullptr;
        return ret;
    }

    void TearDown() override
    {
        arm(0, 0);
        if (handle != nullptr)
        {
            close();
        }
        std::lock_guard<std::mutex> lock(s_probe_mutex);
        s_probe = nullptr;
    }
#endif
};

// 受信バッファーの確保失敗後に、確保済み領域と全経路の listen ソケットを解放することを確認します。
TEST_P(tcp_allocation_failure_test, releases_buffers_and_reopens_endpoints)
{
    // Arrange
#if defined(PLATFORM_LINUX)
    const auto target = GetParam();
    size_t expected_calls = 2;
    if (target.size == wire_size)
    {
        // recv_buf/send_wire_buf と経路別受信バッファー、既存の経路別先読みバッファーです。
        expected_calls += 2 * POTR_MAX_PATH;
    }
    arm(target.size, 0); // [状態] - 全 4 経路の TCP 受信者で、対象サイズの確保回数を記録する。
    // Pre-Assert
    // Act
    int baseline_ret = open(); // [手順] - 注入せず potr_service_open を呼び出す。
    auto baseline = snapshot();
    // Assert
    ASSERT_EQ(POTR_OK, baseline_ret); // [確認_正常系] - 注入前の potr_service_open が成功すること。
    ASSERT_NE(nullptr, handle);       // [確認_正常系] - 注入前のハンドルが有効であること。
    ASSERT_EQ(
        expected_calls,
        baseline.matching_calls); // [確認_正常系] - 対象サイズの全バッファー確保を実 cplat_malloc で捕捉すること。

    // Act_2
    int baseline_close_ret = close(); // [手順] - 注入前のサービスを potr_service_close で終了する。
    auto baseline_closed = snapshot();
    // Assert_2
    ASSERT_EQ(POTR_OK, baseline_close_ret); // [確認_正常系] - 注入前の potr_service_close が成功すること。
    ASSERT_FALSE(baseline_closed.overflow); // [確認_正常系] - 確保追跡の容量を超えないこと。
    ASSERT_EQ(baseline_closed.allocated,
              baseline_closed.freed); // [確認_正常系] - 正常終了でも追跡した全領域を解放すること。

    // Arrange_3
    arm(target.size, target.occurrence);
    // Pre-Assert_3
    // Act_3
    int failure_ret =
        open(); // [手順] - 指定受信バッファーの cplat_malloc だけを失敗させて potr_service_open を呼び出す。
    auto failed = snapshot();
    arm(0, 0);
    // Assert_3
    EXPECT_EQ(1U, failed.failures); // [確認_異常系] - 指定した確保で 1 回だけ失敗を注入すること。
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY,
              failure_ret);     // [確認_異常系] - 注入時の potr_service_open がメモリ不足を返すこと。
    EXPECT_EQ(nullptr, handle); // [確認_異常系] - 失敗時にハンドルを公開しないこと。
    EXPECT_GT(failed.allocated,
              baseline_closed.allocated); // [確認_異常系] - 失敗より前に実際の確保が成功していること。
    EXPECT_FALSE(failed.overflow);        // [確認_異常系] - 失敗経路でも全確保を追跡できること。
    EXPECT_EQ(failed.allocated,
              failed.freed); // [確認_異常系] - 失敗時に成功した全 cplat_malloc のポインターを解放すること。
    if (handle != nullptr)
    {
        close();
    }

    // Act_4
    int reopen_ret = open(); // [手順] - 注入を解除して同じ 4 エンドポイントで potr_service_open を呼び出す。
    // Assert_4
    ASSERT_EQ(POTR_OK,
              reopen_ret);      // [確認_正常系] - 失敗後の potr_service_open が全 listen ソケットを再利用できること。
    ASSERT_NE(nullptr, handle); // [確認_正常系] - 再オープンしたハンドルが有効であること。

    // Act_5
    int close_ret = close(); // [手順] - 再オープンしたサービスを potr_service_close で終了する。
    auto closed = snapshot();
    // Assert_5
    EXPECT_EQ(POTR_OK, close_ret);             // [確認_正常系] - 再オープン後の potr_service_close が成功すること。
    EXPECT_FALSE(closed.overflow);             // [確認_正常系] - 再オープン後も全確保を追跡できること。
    EXPECT_EQ(closed.allocated, closed.freed); // [確認_正常系] - 再オープン後も追跡した全領域を解放すること。
#else
    // Pre-Assert
    // Act
    // Assert
    GTEST_SKIP() << "Linux の動的 cplat_malloc/cplat_free 差し替えを使用するため Windows では対象外";
#endif
}

#if defined(PLATFORM_LINUX)
/* 送信用 compress/crypto の次が受信用。wire は recv_buf/send_wire_buf の次が経路別です。
 * 総 malloc 回数には依存せず、公開設定から決まるサイズの出現順で指定します。 */
INSTANTIATE_TEST_SUITE_P(
    receive_buffers, tcp_allocation_failure_test,
    Values(allocation_case{"recv_compress_buf", compress_size, 2}, allocation_case{"recv_crypto_buf", crypto_size, 2},
           allocation_case{"tcp_recv_buf_0", wire_size, 3}, allocation_case{"tcp_recv_buf_1", wire_size, 4},
           allocation_case{"tcp_recv_buf_2", wire_size, 5}, allocation_case{"tcp_recv_buf_3", wire_size, 6}),
    [](const TestParamInfo<allocation_case> &case_info) { return case_info.param.name; });
#else
INSTANTIATE_TEST_SUITE_P(receive_buffers, tcp_allocation_failure_test, Values(allocation_case{"linux_only", 0, 0}));
#endif
