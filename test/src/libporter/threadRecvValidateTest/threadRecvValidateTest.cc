#include <cplat/base/platform.h>
#if defined(PLATFORM_WINDOWS)
    #define _HAS_STD_BYTE 0
#endif
#include <testfw.h>
#include <mock_cplat.h>
#include <mock_porter.h>
#include <porter/porter_result.h>
#include <porter/porter_const.h>
#include "thread_recv_validate.h"

// 認証失敗とローカル障害を区別し、失敗した平文を公開しないことの確認
TEST(threadRecvValidateTest, classifies_crypto_errors_without_publishing_plaintext)
{
    // Arrange
    NiceMock<Mock_cplat> mock;
    potr_context ctx = {};
    uint8_t ciphertext[POTR_CRYPTO_TAG_SIZE] = {};
    uint8_t plaintext[32] = {};
    uint8_t header[POTR_PACKET_HEADER_SIZE] = {};
    ctx.recv_crypto_buf = plaintext;
    ctx.crypto_buf_size = sizeof(plaintext);
    ctx.service.encrypt_enabled = 1;
    int actual[6] = {};
    potr_packet packets[6] = {};
    for (int i = 0; i < 6; ++i)
    {
        packets[i].flags = static_cast<uint16_t>(POTR_FLAG_ENCRYPTED | (i < 3 ? POTR_FLAG_DATA : POTR_FLAG_FIN));
        packets[i].payload = ciphertext;
        packets[i].payload_len = sizeof(ciphertext);
    } // [状態] - DATA と制御パケットをそれぞれ 3 件用意する。

    // Pre-Assert
    EXPECT_CALL(mock, cplat_crypto_decrypt(_, _, _, _, _, _, _, _))
        .WillOnce(Return(CPLAT_ERR_AUTHENTICATION_FAILED))
        .WillOnce(Return(CPLAT_ERR_OUT_OF_MEMORY))
        .WillOnce(Return(CPLAT_ERR_UNKNOWN))
        .WillOnce(Return(CPLAT_ERR_AUTHENTICATION_FAILED))
        .WillOnce(Return(CPLAT_ERR_OUT_OF_MEMORY))
        .WillOnce(Return(CPLAT_ERR_UNKNOWN)); // [Pre-Assert確認_異常系] - 復号が合計 6 回呼ばれること。
    // [Pre-Assert手順] - 認証失敗、メモリ不足、内部失敗を順番に返す。

    // Act
    for (int i = 0; i < 6; ++i)
    {
        actual[i] = thread_recv_authenticate_packet(&ctx, &packets[i], header, "test",
                                                    -1); // [手順] - 両形式のエラーを処理する。
    }

    // Assert
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual[0]); // [確認_異常系] - 認証失敗を通信エラーへ変換すること。
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, actual[1]);
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual[2]);
    EXPECT_EQ(POTR_ERR_PROTOCOL, actual[3]);
    EXPECT_EQ(POTR_ERR_OUT_OF_MEMORY, actual[4]);
    EXPECT_EQ(POTR_ERR_UNKNOWN, actual[5]);
    for (const auto &packet : packets)
    {
        EXPECT_EQ(ciphertext, packet.payload);             // [確認_異常系 回数=6] - 失敗した復号出力を公開しないこと。
        EXPECT_EQ(sizeof(ciphertext), packet.payload_len); // [確認_異常系 回数=6] - 元の長さを保持すること。
    }
}

// ローカルの契約不整合を受信データの認証失敗と区別することの確認
TEST(threadRecvValidateTest, classifies_local_argument_capacity_and_range_errors)
{
    // Arrange
    NiceMock<Mock_cplat> mock;
    potr_context ctx = {};
    uint8_t payload[POTR_CRYPTO_TAG_SIZE] = {};
    uint8_t plaintext[32] = {};
    uint8_t header[POTR_PACKET_HEADER_SIZE] = {};
    ctx.recv_crypto_buf = plaintext;
    ctx.crypto_buf_size = sizeof(plaintext);
    const int failures[] = {CPLAT_ERR_INVALID_ARGUMENT, CPLAT_ERR_BUFFER_TOO_SMALL, CPLAT_ERR_OUT_OF_RANGE};
    int actual[6] = {}; // [状態] - DATA と制御パケットに対して各エラーを試す。

    // Pre-Assert
    EXPECT_CALL(mock, cplat_crypto_decrypt(_, _, _, _, _, _, _, _))
        .WillOnce(Return(failures[0]))
        .WillOnce(Return(failures[1]))
        .WillOnce(Return(failures[2]))
        .WillOnce(Return(failures[0]))
        .WillOnce(Return(failures[1]))
        .WillOnce(Return(failures[2]));
    // [Pre-Assert確認_異常系] - 復号を合計 6 回試すこと。
    // [Pre-Assert手順] - 引数不正、容量不足、範囲外を返す。

    // Act
    for (int i = 0; i < 6; ++i)
    {
        potr_packet packet = {};
        packet.flags = static_cast<uint16_t>(POTR_FLAG_ENCRYPTED | (i < 3 ? POTR_FLAG_DATA : POTR_FLAG_FIN));
        packet.payload = payload;
        packet.payload_len = sizeof(payload);
        actual[i] = thread_recv_authenticate_packet(&ctx, &packet, header, "test", -1); // [手順] - 各不整合を処理する。
    }

    // Assert
    for (int result : actual)
    {
        EXPECT_EQ(POTR_ERR_INVALID_ARGUMENT, result); // [確認_異常系 回数=6] - ローカルの契約不整合として分類すること。
    }
}

// タグ長が不足する受信データを backend 呼び出し前に拒否することの確認
TEST(threadRecvValidateTest, rejects_malformed_payload_lengths_before_decryption)
{
    // Arrange
    NiceMock<Mock_cplat> mock;
    potr_context ctx = {};
    uint8_t payload[POTR_CRYPTO_TAG_SIZE - 1] = {};
    uint8_t header[POTR_PACKET_HEADER_SIZE] = {};
    potr_packet data = {};
    data.flags = POTR_FLAG_ENCRYPTED | POTR_FLAG_DATA;
    data.payload = payload;
    data.payload_len = sizeof(payload);
    potr_packet control = data;
    control.flags = POTR_FLAG_ENCRYPTED | POTR_FLAG_FIN; // [状態] - 両形式のタグを 1 バイト不足させる。

    // Pre-Assert
    EXPECT_CALL(mock, cplat_crypto_decrypt(_, _, _, _, _, _, _, _)).Times(0);
    // [Pre-Assert確認_異常系] - 不正な受信長で復号を呼ばないこと。

    // Act
    int data_ret = thread_recv_authenticate_packet(&ctx, &data, header, "test", -1);
    int control_ret =
        thread_recv_authenticate_packet(&ctx, &control, header, "test", -1); // [手順] - 受信形式を検査する。

    // Assert
    EXPECT_EQ(POTR_ERR_PROTOCOL, data_ret);    // [確認_異常系] - DATA の形式不正を通知すること。
    EXPECT_EQ(POTR_ERR_PROTOCOL, control_ret); // [確認_異常系] - 制御パケットの形式不正を通知すること。
}
