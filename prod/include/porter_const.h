/**
 *******************************************************************************
 *  @file           porter_const.h
 *  @brief          通信ライブラリの定数ファイル。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef PORTER_CONST_H
#define PORTER_CONST_H

/** @defgroup POTR_SEND_FLAG 送信オプションフラグ (potrSend の flags 引数)
 *  @{
 *  @details
 *  `potrSend()` の `flags` 引数に論理和で組み合わせて指定するビットフラグです。\n
 *  0 を指定すると非圧縮・ノンブロッキング送信になります。
 */
#define POTR_SEND_COMPRESS                                                                                             \
    0x0001U /**< メッセージを圧縮して送信します。圧縮後のサイズが元のサイズ以上の場合は自動的に非圧縮で送信します。 */
#define POTR_SEND_BLOCKING 0x0002U /**< ブロッキング送信を行います。0 を指定するとノンブロッキング送信を行います。 */
/** @} */

/** @defgroup POTR_RESULT 戻り値
 *  @{
 */
#define POTR_SUCCESS            0  /**< 成功の戻り値を表す定数。 */
#define POTR_ERROR              -1 /**< 失敗の戻り値を表す定数。 */
#define POTR_ERROR_DISCONNECTED 1  /**< 論理 CONNECTED 前または切断中に potrSend() を呼んだ場合の戻り値。 */
/** @} */

/** @defgroup POTR_OUTER_FLAG 外側パケットフラグ (PotrPacket.flags)
 *  @{
 *  @details
 *  UDP で送受信される外側パケット (PotrPacket) の flags フィールドに設定するフラグです。\n
 *  ペイロードエレメントのフラグ (@ref POTR_ELEM_FLAG) とは独立したビット空間で管理します。
 */
#define POTR_FLAG_DATA 0x0001U /**< データパケット (パックコンテナ) であることを示すフラグ。常に設定される。 */
#define POTR_FLAG_NACK 0x0002U /**< 再送要求パケットであることを示すフラグ。ack_num に要求通番を格納する。 */
#define POTR_FLAG_PING                                                                                                 \
    0x0004U /**< ヘルスチェックパケットであることを示すフラグ。ペイロードには POTR_MAX_PATH バイトのパス受信状態 (POTR_PING_STATE_*) を格納する。 */
#define POTR_FLAG_REJECT                                                                                               \
    0x0008U /**< 再送不能通知パケットであることを示すフラグ。ack_num に再送不能な通番を格納する。 */
#define POTR_FLAG_FIN                                                                                                  \
    0x0010U /**< 正常終了通知パケットであることを示すフラグ。送信者が potrCloseService 時に送出し、受信者は即座に DISCONNECTED へ遷移する。ペイロードなし。 */
#define POTR_FLAG_ENCRYPTED                                                                                            \
    0x0020U /**< AES-256-GCM 認証タグが付与されていることを示す外側パケットフラグ。\n
                                       *   POTR_FLAG_DATA と組み合わせる場合: [ヘッダー][暗号文: packed_len B][GCM 認証タグ: 16B]。\n
                                       *   POTR_FLAG_PING と組み合わせる場合: [ヘッダー][暗号文: POTR_MAX_PATH B][GCM 認証タグ: 16B]。\n
                                       *   その他 (NACK/REJECT/FIN/FIN_ACK): ペイロードなし (平文 0B) の GCM 認証タグのみ (16B)。\n
                                       *   いずれの場合もヘッダー全体が AAD として認証される。\n
                                       *   Nonce (12B) = session_id (4B NBO) + flags (2B NBO) + seq_or_ack_num (4B NBO) + padding (2B 0x00)。\n
                                       *   flags には本フラグを含んだ実際の送信フラグ値を使用する。\n
                                       *   seq_or_ack_num は DATA/PING/FIN の場合 seq_num、NACK/REJECT/FIN_ACK の場合 ack_num。 */
#define POTR_FLAG_FIN_TARGET_VALID                                                                                     \
    0x0040U /**< FIN の ack_num が有効な受信完了目標 next_seq を表すことを示すフラグ。未設定時の FIN は no-data FIN として扱い、ack_num は無視する。 */
#define POTR_FLAG_FIN_ACK                                                                                              \
    0x0080U /**< FIN 処理完了応答パケットであることを示すフラグ。ack_num に完了した fin_target_seq を格納する。 */
/** @} */

/** @defgroup POTR_ELEM_FLAG ペイロードエレメントフラグ (パックコンテナ内エレメントヘッダー.flags)
 *  @{
 *  @details
 *  POTR_FLAG_DATA パケットのペイロード内に格納されるペイロードエレメントのヘッダー flags フィールドに設定するフラグです。\n
 *  圧縮・フラグメント化はメッセージ単位で行われ、ペイロードエレメント単位で管理します。\n
 *  外側パケットのフラグ (@ref POTR_OUTER_FLAG) には設定しません。
 */
#define POTR_FLAG_MORE_FRAG                                                                                            \
    0x0001U /**< 後続フラグメントが存在することを示すペイロードエレメントフラグ。メッセージが複数ペイロードエレメントに分割された場合、最終フラグメント以外に設定する。 */
#define POTR_FLAG_COMPRESSED                                                                                           \
    0x0002U /**< ペイロードが圧縮されていることを示すペイロードエレメントフラグ。圧縮はメッセージ単位で行い、全フラグメントのペイロードエレメントに設定する。先頭 4 バイトが元サイズ (NBO)、続くデータが raw DEFLATE。 */
/** @} */

/** @defgroup POTR_DEFAULT デフォルト値
 *  @{
 */
#define POTR_DEFAULT_WINDOW_SIZE  16U   /**< デフォルトウィンドウサイズ (パケット数)。 */
#define POTR_DEFAULT_MAX_PAYLOAD  1400U /**< デフォルト最大ペイロード長 (バイト)。 */
#define POTR_DEFAULT_TTL          1U    /**< デフォルトマルチキャスト TTL。 */
#define POTR_DEFAULT_PACK_WAIT_MS 0U    /**< デフォルトパッキング待ち時間 (ミリ秒)。0 = 即時送信。 */
#define POTR_DEFAULT_RECONNECT_INTERVAL_MS                                                                             \
    5000U /**< SENDER 自動再接続間隔のデフォルト (ミリ秒)。TCP 通信種別 (POTR_TYPE_TCP / POTR_TYPE_TCP_BIDIR) のみ有効。 */
#define POTR_DEFAULT_CONNECT_TIMEOUT_MS                                                                                \
    10000U /**< SENDER TCP 接続タイムアウトのデフォルト (ミリ秒)。0 = OS デフォルト。TCP 通信種別のみ有効。 */
#define POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS                                                                            \
    3000U /**< UDP 通信種別に適用する既定ヘルスチェック送信間隔 (ミリ秒)。0 = 無効。 */
#define POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS                                                                             \
    10000U /**< UDP 通信種別に適用する既定ヘルスチェックタイムアウト (ミリ秒)。0 = 無効。 */
#define POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS                                                                            \
    10000U /**< TCP 通信種別に適用する既定ヘルスチェック送信間隔 (ミリ秒)。0 = 無効。 */
#define POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS                                                                             \
    31000U /**< TCP 通信種別に適用する既定ヘルスチェックタイムアウト (ミリ秒)。0 = 無効。 */
#define POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS                                                                              \
    5000U /**< TCP 通信種別に適用する close 完了待機タイムアウト (ミリ秒)。0 = 待機なし。 */
/** @} */

/** @defgroup POTR_CRYPTO 暗号化定数 (AES-256-GCM)
 *  @{
 *  @details
 *  POTR_FLAG_ENCRYPTED が設定されたパケットの暗号化・復号に使用する定数です。
 */
#define POTR_CRYPTO_KEY_SIZE                                                                                           \
    32U /**< AES-256-GCM 鍵サイズ (バイト)。設定ファイルの encrypt_key に 64 文字 hex で指定する。 */
#define POTR_CRYPTO_NONCE_SIZE                                                                                         \
    12U /**< AES-256-GCM ノンスサイズ (バイト)。session_id (4B NBO) + flags (2B NBO) + seq_or_ack_num (4B NBO) + padding (2B 0x00) で構成する。 */
#define POTR_CRYPTO_TAG_SIZE 16U /**< AES-256-GCM 認証タグサイズ (バイト)。暗号文の直後に付加する。 */
/** @} */

/** @defgroup POTR_LIMIT 上限値・デフォルト値
 *  @{
 */
#define POTR_MAX_ADDR_LEN 64U /**< アドレス文字列の最大長 (バイト、終端 NUL を含む)。 */
#define POTR_MAX_PAYLOAD                                                                                               \
    65507U /**< ペイロードの最大長 (バイト)。UDP 最大ペイロード (65535 - IP20 - UDP8)。max_payload 設定値のバリデーション上限として使用する。 */
#define POTR_MAX_WINDOW_SIZE                                                                                           \
    256U /**< ウィンドウサイズの最大値 (パケット数)。window_size 設定値のバリデーション上限として使用する。 */
#define POTR_MAX_SERVICES                                                                                              \
    64U /**< config_list_service_ids() の初期バッファ容量。サービス数がこれを超えた場合は realloc で自動拡張する。 */
#define POTR_MAX_MESSAGE_SIZE                                                                                          \
    65535U /**< 1 回の potrSend で送信できる最大メッセージ長 (バイト) のデフォルト値。設定ファイルの max_message_size で変更可能。フラグメント化により max_payload を超えるメッセージも送受信できる。 */
#define POTR_SEND_QUEUE_DEPTH                                                                                          \
    1024U /**< 非同期送信キューの最大エントリ数のデフォルト値。設定ファイルの send_queue_depth で変更可能。メッセージがフラグメント化される場合、1 メッセージが複数エントリを占有する。 */
#define POTR_PAYLOAD_ELEM_HDR_SIZE                                                                                     \
    6U /**< パックコンテナ内ペイロードエレメントのヘッダーサイズ (バイト)。flags (2): POTR_FLAG_MORE_FRAG / POTR_FLAG_COMPRESSED を格納 + payload_len (4): uint32_t (NBO)。通番は外側パケットで管理するためペイロードエレメントには含まない。 */
#define POTR_MAX_PATH 4U /**< マルチパスの最大パス数。 */
/** @} */

/** @defgroup POTR_PING_STATE PING 受信状態 (PING ペイロードの各パスフィールド値)
 *  @{
 *  @details
 *  PING パケットのペイロードに格納するパスごとの PING 受信状態です。\n
 *  双方向 PING 形態 (UNICAST_BIDIR / UNICAST_BIDIR_N1 / TCP / TCP_BIDIR) では実際の状態を格納します。\n
 *  片方向 PING 形態 (UNICAST / MULTICAST / BROADCAST / RAW 系) では常に UNDEFINED を格納します。
 */
#define POTR_PING_STATE_UNDEFINED                                                                                      \
    0U /**< 不定: 片方向のため PING を受信しない、またはまだ一度も PING を受信していない。 */
#define POTR_PING_STATE_NORMAL   1U /**< 正常: 定周期で PING を受信中。 */
#define POTR_PING_STATE_ABNORMAL 2U /**< 異常: PING タイムアウト (途絶)。 */
/** @} */

#endif /* PORTER_CONST_H */
