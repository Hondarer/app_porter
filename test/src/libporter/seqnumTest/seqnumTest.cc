#include <cplat/base/platform.h>

#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif /* PLATFORM_WINDOWS */
#include <testfw.h>

#include <porter/protocol/seqnum.h>

#include <stdint.h>

using namespace testing;

// ウィンドウ先頭と末尾を範囲内と判定することの確認
TEST(seqnumTest, includesFirstAndLastSequenceNumbers)
{
    // Arrange
    const uint32_t base = 100U;
    const uint16_t window_size = 4U;

    // Pre-Assert

    // Act
    int first_result = potr_internal_seqnum_in_window(base, base, window_size);     // [手順] - ウィンドウ先頭を判定する。
    int last_result = potr_internal_seqnum_in_window(base + 3U, base, window_size); // [手順] - ウィンドウ末尾を判定する。

    // Assert
    EXPECT_EQ(1, first_result); // [確認_正常系] - 先頭に対する potr_internal_seqnum_in_window の戻り値が 1 であること。
    EXPECT_EQ(1, last_result);  // [確認_正常系] - 末尾に対する potr_internal_seqnum_in_window の戻り値が 1 であること。
}

// ウィンドウ直後と直前を範囲外と判定することの確認
TEST(seqnumTest, excludesSequenceNumbersImmediatelyOutsideWindow)
{
    // Arrange
    const uint32_t base = 100U;
    const uint16_t window_size = 4U;

    // Pre-Assert

    // Act
    int after_result = potr_internal_seqnum_in_window(base + 4U, base, window_size);  // [手順] - ウィンドウ直後を判定する。
    int before_result = potr_internal_seqnum_in_window(base - 1U, base, window_size); // [手順] - ウィンドウ直前を判定する。

    // Assert
    EXPECT_EQ(0, after_result);  // [確認_正常系] - 直後に対する potr_internal_seqnum_in_window の戻り値が 0 であること。
    EXPECT_EQ(0, before_result); // [確認_正常系] - 直前に対する potr_internal_seqnum_in_window の戻り値が 0 であること。
}

// サイズ 0 のウィンドウが先頭を範囲外と判定することの確認
TEST(seqnumTest, excludesBaseFromEmptyWindow)
{
    // Arrange
    const uint32_t base = 100U;

    // Pre-Assert

    // Act
    int result = potr_internal_seqnum_in_window(base, base, 0U); // [手順] - サイズ 0 のウィンドウ先頭を判定する。

    // Assert
    EXPECT_EQ(0, result); // [確認_正常系] - サイズ 0 に対する potr_internal_seqnum_in_window の戻り値が 0 であること。
}

// uint32_t の上限をまたぐウィンドウを正しく判定することの確認
TEST(seqnumTest, handlesWindowAcrossUint32Wraparound)
{
    // Arrange
    const uint32_t base = UINT32_MAX - 1U;
    const uint16_t window_size = 4U;

    // Pre-Assert

    // Act
    int max_minus_one_result =
        potr_internal_seqnum_in_window(UINT32_MAX - 1U, base, window_size);           // [手順] - 折り返し前の先頭を判定する。
    int max_result = potr_internal_seqnum_in_window(UINT32_MAX, base, window_size);   // [手順] - uint32_t の上限を判定する。
    int zero_result = potr_internal_seqnum_in_window(0U, base, window_size);          // [手順] - 折り返し後の 0 を判定する。
    int one_result = potr_internal_seqnum_in_window(1U, base, window_size);           // [手順] - 折り返し後の末尾を判定する。
    int two_result = potr_internal_seqnum_in_window(2U, base, window_size);           // [手順] - 折り返し後の直後を判定する。
    int before_result = potr_internal_seqnum_in_window(base - 1U, base, window_size); // [手順] - 折り返し前の直前を判定する。

    // Assert
    EXPECT_EQ(1, max_minus_one_result); // [確認_正常系] - 折り返し前の先頭に対する戻り値が 1 であること。
    EXPECT_EQ(1, max_result);           // [確認_正常系] - uint32_t の上限に対する戻り値が 1 であること。
    EXPECT_EQ(1, zero_result);          // [確認_正常系] - 折り返し後の 0 に対する戻り値が 1 であること。
    EXPECT_EQ(1, one_result);           // [確認_正常系] - 折り返し後の末尾に対する戻り値が 1 であること。
    EXPECT_EQ(0, two_result);           // [確認_正常系] - 折り返し後の直後に対する戻り値が 0 であること。
    EXPECT_EQ(0, before_result);        // [確認_正常系] - 折り返し前の直前に対する戻り値が 0 であること。
}
