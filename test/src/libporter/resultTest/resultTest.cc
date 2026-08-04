#include <testfw.h>
#include <porter/porter_result.h>
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
    return std::vector<int>{POTR_ERR_UNKNOWN,       POTR_ERR_INVALID_ARGUMENT,
                            POTR_ERR_UNSUPPORTED,   POTR_ERR_NOT_FOUND,
                            POTR_ERR_OUT_OF_MEMORY, POTR_ERR_FULL,
                            POTR_ERR_EMPTY,         POTR_ERR_OUT_OF_WINDOW,
                            POTR_ERR_DISCONNECTED,  POTR_ERR_TIMEOUT,
                            POTR_ERR_EOF,           POTR_ERR_IO,
                            POTR_ERR_PROTOCOL,      POTR_ERR_CANCELED};
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
    unique_codes.insert(codes.begin(), codes.end()); // [手順] - 全コードを集合へ挿入して重複を排除する。

    // Assert
    EXPECT_EQ(codes.size(),
              unique_codes.size()); // [確認_正常系] - 重複がなく、集合の要素数が列挙したコード数と一致すること。
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
    EXPECT_EQ(0, POTR_OK);             // [確認_正常系] - POTR_OK の値が 0 であること。
    EXPECT_EQ(0U, non_negative_count); // [確認_正常系] - 0 以上の値を持つエラー コードが存在しないこと。
    EXPECT_FALSE(error_codes.empty()); // [確認_正常系] - 検証対象のエラー コードが 1 つ以上列挙されていること。
}
