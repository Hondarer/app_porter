#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
    #include <winsock2.h>
#elif defined(PLATFORM_LINUX)
    #include <errno.h>
#endif
#include <testfw.h>
#include <porter/porter_result.h>
extern "C"
{
#include <porter/infra/potr_result.h>
}
#include <cplat/base/error.h>
#include <errno.h>
#include <set>
#include <vector>

/* 値を変更する場合は、porter の全利用箇所への影響を調査すること。 */
static_assert(POTR_OK == 0, "POTR_OK の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_UNKNOWN == -1, "POTR_ERR_UNKNOWN の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_INVALID_ARGUMENT == -2,
              "POTR_ERR_INVALID_ARGUMENT の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_UNSUPPORTED == -3,
              "POTR_ERR_UNSUPPORTED の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_NOT_FOUND == -4,
              "POTR_ERR_NOT_FOUND の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_INVALID_CONFIG == -6,
              "POTR_ERR_INVALID_CONFIG の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_OUT_OF_MEMORY == -10,
              "POTR_ERR_OUT_OF_MEMORY の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_FULL == -11, "POTR_ERR_FULL の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_EMPTY == -12, "POTR_ERR_EMPTY の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_OUT_OF_WINDOW == -13,
              "POTR_ERR_OUT_OF_WINDOW の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_DISCONNECTED == -20,
              "POTR_ERR_DISCONNECTED の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_TIMEOUT == -21, "POTR_ERR_TIMEOUT の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_EOF == -22, "POTR_ERR_EOF の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_IO == -23, "POTR_ERR_IO の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_PROTOCOL == -24, "POTR_ERR_PROTOCOL の値を変更する場合は全利用箇所への影響を調査してください。");
static_assert(POTR_ERR_CANCELED == -40, "POTR_ERR_CANCELED の値を変更する場合は全利用箇所への影響を調査してください。");

static std::vector<int> all_error_codes()
{
    return std::vector<int>{POTR_ERR_UNKNOWN,        POTR_ERR_INVALID_ARGUMENT,
                            POTR_ERR_UNSUPPORTED,    POTR_ERR_NOT_FOUND,
                            POTR_ERR_INVALID_CONFIG, POTR_ERR_OUT_OF_MEMORY,
                            POTR_ERR_FULL,           POTR_ERR_EMPTY,
                            POTR_ERR_OUT_OF_WINDOW,  POTR_ERR_DISCONNECTED,
                            POTR_ERR_TIMEOUT,        POTR_ERR_EOF,
                            POTR_ERR_IO,             POTR_ERR_PROTOCOL,
                            POTR_ERR_CANCELED};
}

class resultTest : public Test
{
};

// すべての結果コードが相異なる値であることの確認
TEST_F(resultTest, all_codes_are_distinct)
{
    // Arrange
    std::vector<int> codes = all_error_codes(); // [状態] - porter_result.h が定義する全エラー コードを列挙する。
    std::set<int> unique_codes;

    codes.push_back(POTR_OK); // [状態] - 比較対象に POTR_OK を加える。

    // Pre-Assert

    // Act
    unique_codes.insert(
        codes.begin(),
        codes.end()); // [手順] - 重複を排除するため、全コードを指定して unique_codes.insert() を呼び出す。

    // Assert
    EXPECT_EQ(
        codes.size(),
        unique_codes
            .size()); // [確認_正常系] - unique_codes.size() の戻り値が codes.size() の戻り値と一致すること。(列挙した結果コードに重複がないことの確認)
}

// POTR_OK のみが 0 で、エラー コードがすべて負値であることの確認
TEST_F(resultTest, only_ok_is_zero_and_all_errors_are_negative)
{
    // Arrange
    const std::vector<int> error_codes = all_error_codes(); // [状態] - POTR_OK を除く全エラー コードを列挙する。
    size_t non_negative_count = 0U;

    // Pre-Assert

    // Act
    for (int code : error_codes)
    {
        if (code >= 0)
        {
            non_negative_count++; // [手順] - 0 以上の値を持つエラー コードを数える。
        }
    }

    // Assert
    EXPECT_EQ(0, POTR_OK); // [確認_正常系] - POTR_OK の値が 0 であること。
    EXPECT_EQ(
        0U,
        non_negative_count); // [確認_正常系] - non_negative_count が 0U であること。(0 以上の値を持つエラー コードが存在しないことの確認)
    EXPECT_FALSE(error_codes.empty()); // [確認_正常系] - error_codes.empty() の戻り値が false であること。
}

// 詳細がない場合にも結果コードから porter 結果コードへ変換することの確認
TEST_F(resultTest, socket_result_without_detail)
{
    // Arrange
    cplat_error empty_detail = {}; // [状態] - 要因のない空の詳細エラーを用意する。

    // Pre-Assert

    // Act
    int ret_ok = potr_internal_result_from_socket_result(CPLAT_OK, nullptr);
    int ret_timeout = potr_internal_result_from_socket_result(CPLAT_ERR_TIMEOUT, nullptr);
    int ret_canceled = potr_internal_result_from_socket_result(CPLAT_ERR_CANCELED, nullptr);
    int ret_eof = potr_internal_result_from_socket_result(CPLAT_ERR_EOF, nullptr);
    int ret_would_block = potr_internal_result_from_socket_result(CPLAT_ERR_WOULD_BLOCK, nullptr);
    int ret_busy = potr_internal_result_from_socket_result(CPLAT_ERR_BUSY, &empty_detail);
    int ret_oom = potr_internal_result_from_socket_result(CPLAT_ERR_OUT_OF_MEMORY, nullptr);
    int ret_invalid_arg = potr_internal_result_from_socket_result(CPLAT_ERR_INVALID_ARGUMENT, nullptr);
    int ret_unknown = potr_internal_result_from_socket_result(CPLAT_ERR_UNKNOWN, &empty_detail);
    // [手順] - 各 cplat 結果コードを詳細なし (または空の詳細) で変換する。

    // Assert
    EXPECT_EQ(POTR_OK, ret_ok);                                 // [確認_正常系] - CPLAT_OK が POTR_OK であること。
    EXPECT_EQ(POTR_ERR_TIMEOUT, ret_timeout);                   // [確認_正常系] - CPLAT_ERR_TIMEOUT が POTR_ERR_TIMEOUT であること。
    EXPECT_EQ(POTR_ERR_CANCELED, ret_canceled);                 // [確認_正常系] - CPLAT_ERR_CANCELED が POTR_ERR_CANCELED であること。
    EXPECT_EQ(POTR_ERR_EOF, ret_eof);                           // [確認_正常系] - CPLAT_ERR_EOF が POTR_ERR_EOF であること。
    EXPECT_EQ(POTR_ERR_IO, ret_would_block);                    // [確認_正常系] - 未処理の CPLAT_ERR_WOULD_BLOCK が防御として POTR_ERR_IO であること。
    EXPECT_EQ(POTR_ERR_IO, ret_busy);                           // [確認_正常系] - CPLAT_ERR_BUSY が POTR_ERR_IO であること。
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, ret_oom);                 // [確認_正常系] - CPLAT_ERR_OUT_OF_MEMORY が POTR_ERR_OUT_OF_MEMORY であること。
    EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, ret_invalid_arg);      // [確認_正常系] - CPLAT_ERR_INVALID_ARGUMENT が POTR_ERR_INVALID_ARGUMENT であること。
    EXPECT_EQ(POTR_ERR_IO, ret_unknown);                        // [確認_正常系] - 詳細のない CPLAT_ERR_UNKNOWN が POTR_ERR_IO であること。
}

// 詳細がある場合に結果コードと詳細要因の双方に従って変換することの確認
TEST_F(resultTest, socket_result_with_detail)
{
    // Arrange
    cplat_error detail_timeout = {};
    detail_timeout.result = CPLAT_ERR_TIMEOUT;
#if defined(PLATFORM_WINDOWS)
    detail_timeout.domain = CPLAT_ERROR_DOMAIN_WINSOCK;
    detail_timeout.code = (unsigned long)WSAETIMEDOUT;
#else
    detail_timeout.domain = CPLAT_ERROR_DOMAIN_SOCKET_ERRNO;
    detail_timeout.code = (unsigned long)ETIMEDOUT;
#endif

    cplat_error detail_would_block = {};
    detail_would_block.result = CPLAT_ERR_WOULD_BLOCK;
#if defined(PLATFORM_WINDOWS)
    detail_would_block.domain = CPLAT_ERROR_DOMAIN_WINSOCK;
    detail_would_block.code = (unsigned long)WSAEWOULDBLOCK;
#else
    detail_would_block.domain = CPLAT_ERROR_DOMAIN_SOCKET_ERRNO;
    detail_would_block.code = (unsigned long)EAGAIN;
#endif

    cplat_error detail_busy = {};
    detail_busy.result = CPLAT_ERR_BUSY;
    detail_busy.domain = CPLAT_ERROR_DOMAIN_ERRNO;
    detail_busy.code = (unsigned long)EBUSY;

    cplat_error detail_oom = {};
    detail_oom.result = CPLAT_ERR_OUT_OF_MEMORY;
    detail_oom.domain = CPLAT_ERROR_DOMAIN_ERRNO;
    detail_oom.code = (unsigned long)ENOMEM;

    cplat_error detail_reset = {};
    detail_reset.result = CPLAT_ERR_UNKNOWN;
#if defined(PLATFORM_WINDOWS)
    detail_reset.domain = CPLAT_ERROR_DOMAIN_WINSOCK;
    detail_reset.code = (unsigned long)WSAECONNRESET;
#else
    detail_reset.domain = CPLAT_ERROR_DOMAIN_SOCKET_ERRNO;
    detail_reset.code = (unsigned long)ECONNRESET;
#endif
    // [状態] - 各要因を設定した cplat_error を用意する。

    // Pre-Assert

    // Act
    int ret_timeout = potr_internal_result_from_socket_result(CPLAT_ERR_TIMEOUT, &detail_timeout);
    int ret_canceled = potr_internal_result_from_socket_result(CPLAT_ERR_CANCELED, &detail_reset);
    int ret_eof = potr_internal_result_from_socket_result(CPLAT_ERR_EOF, &detail_reset);
    int ret_would_block = potr_internal_result_from_socket_result(CPLAT_ERR_WOULD_BLOCK, &detail_would_block);
    int ret_busy = potr_internal_result_from_socket_result(CPLAT_ERR_BUSY, &detail_busy);
    int ret_oom = potr_internal_result_from_socket_result(CPLAT_ERR_OUT_OF_MEMORY, &detail_oom);
    int ret_reset = potr_internal_result_from_socket_result(CPLAT_ERR_UNKNOWN, &detail_reset);
    // [手順] - 各 cplat 結果コードを詳細付きで変換する。

    // Assert
    EXPECT_EQ(POTR_ERR_TIMEOUT, ret_timeout);        // [確認_正常系] - TIMEOUT が POTR_ERR_TIMEOUT であること。
    EXPECT_EQ(POTR_ERR_CANCELED, ret_canceled);      // [確認_正常系] - CANCELED が詳細より優先して POTR_ERR_CANCELED であること。
    EXPECT_EQ(POTR_ERR_EOF, ret_eof);                // [確認_正常系] - EOF が詳細より優先して POTR_ERR_EOF であること。
    EXPECT_EQ(POTR_ERR_IO, ret_would_block);         // [確認_正常系] - WOULD_BLOCK が POTR_ERR_IO であること。
    EXPECT_EQ(POTR_ERR_IO, ret_busy);                // [確認_正常系] - BUSY が POTR_ERR_IO であること。
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, ret_oom);      // [確認_正常系] - OUT_OF_MEMORY が POTR_ERR_OUT_OF_MEMORY であること。
    EXPECT_EQ(POTR_ERR_DISCONNECTED, ret_reset);     // [確認_正常系] - CONNECTION_RESET が POTR_ERR_DISCONNECTED であること。
}
