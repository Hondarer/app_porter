/**
 *******************************************************************************
 *  @file           porter_const.h
 *  @brief          porter ライブラリで使用する定数を定義します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/*
 * このヘッダーは多数のソース ファイルから参照されるため、
 * @hideincludedbygraph によって "Included by" グラフを無効にする。
 */

#ifndef PORTER_CONST_H
#define PORTER_CONST_H

/** @defgroup POTR_SEND_FLAG 送信オプション フラグ (potr_service_send の flags 引数)
 *  @ingroup        PORTER_PUBLIC_API
 *  `potr_service_send()` の `flags` 引数に論理和で組み合わせて指定するビット フラグです。\n
 *  0 を指定すると非圧縮・非ブロッキング送信になります。
 */

/**
 *  @ingroup        POTR_SEND_FLAG
 *  @{
 */
#define POTR_SEND_COMPRESS \
    0x0001U /**< メッセージを圧縮して送信します。圧縮後のサイズが元のサイズ以上の場合は自動的に非圧縮で送信します。 */
#define POTR_SEND_BLOCKING 0x0002U /**< ブロッキング送信を行います。0 を指定すると非ブロッキング送信を行います。 */
/** @} */

/** @defgroup POTR_OUTER_FLAG 外側パケット フラグ (potr_packet.flags)
 *  @ingroup        PORTER_PUBLIC_API
 *  UDP で送受信される外側パケット (potr_packet) の flags フィールドに設定するフラグです。\n
 *  ペイロード エレメントのフラグ (@ref POTR_ELEM_FLAG) とは独立したビット空間で管理します。
 */

/**
 *  @ingroup        POTR_OUTER_FLAG
 *  @{
 */
#define POTR_FLAG_DATA 0x0001U /**< データ パケット (パック コンテナー) であることを示すフラグ。常に設定される。 */
#define POTR_FLAG_NACK 0x0002U /**< 再送要求パケットであることを示すフラグ。ack_num に要求通番を格納します。 */
#define POTR_FLAG_PING \
    0x0004U /**< ヘルス チェック パケットであることを示すフラグ。ペイロードには POTR_MAX_PATH バイトのパス受信状態 (POTR_PING_STATE_*) を格納します。 */
#define POTR_FLAG_REJECT \
    0x0008U /**< 再送不能通知パケットであることを示すフラグ。ack_num に再送不能な通番を格納します。 */
#define POTR_FLAG_FIN \
    0x0010U /**< 正常終了通知パケットであることを示すフラグ。送信者が potr_service_close 時に送出し、受信者は即座に DISCONNECTED へ遷移します。ペイロードなし。 */
#define POTR_FLAG_ENCRYPTED \
    0x0020U /**< AES-256-GCM 認証タグが付与されていることを示す外側パケット フラグ。\n
                                       *   POTR_FLAG_DATA と組み合わせる場合: [ヘッダー][暗号文: packed_len B][GCM 認証タグ: 16B]。\n
                                       *   POTR_FLAG_PING と組み合わせる場合: [ヘッダー][暗号文: POTR_MAX_PATH B][GCM 認証タグ: 16B]。\n
                                       *   その他 (NACK/REJECT/FIN/FIN_ACK): ペイロードなし (平文 0B) の GCM 認証タグのみ (16B)。\n
                                       *   いずれの場合も 72 [byte]のヘッダー全体が AAD として認証されます。\n
                                       *   Nonce (12B) = session_id (4B NBO) + tx_counter (8B NBO)。\n
                                       *   経路ごとの複製、再送と制御パケットも送出ごとに tx_counter を採番します。 */
#define POTR_FLAG_FIN_TARGET_VALID \
    0x0040U /**< FIN の ack_num が有効な受信完了目標 next_seq を表すことを示すフラグ。未設定時の FIN は no-data FIN として扱い、ack_num は無視します。 */
#define POTR_FLAG_FIN_ACK \
    0x0080U /**< FIN 処理完了応答パケットであることを示すフラグ。ack_num に完了した fin_target_seq を格納します。 */
#define POTR_FLAG_REPORT \
    0x0100U /**< 受信報告パケット。受信者自身のセッション識別子を使い、seq_num は送出上限の有無 (1 = ack_num に送出上限がある、0 = ない)、ack_num は送出上限 (seq_num が 0 の場合は 0)。ペイロードは POTR_MAX_PATH バイトの経路受信状態 (POTR_PING_STATE_*)。暗号化有効時は PING と同じ方式で保護します。 */
#define POTR_FLAG_HANDSHAKE \
    0x0200U /**< 相互認証と鍵確立のための TLS / DTLS ハンドシェイクのバイト列をペイロードに格納するパケット。封緘は鍵なしの平文です。 */
#define POTR_FLAG_KEY \
    0x0400U /**< 一対多通信のグループ鍵を配布するパケット。受信者の証明書を検証した後、受信者用の鍵でグループ鍵を暗号化して送ります。 */
#define POTR_FLAG_FLOW_CONTROL \
    0x0800U /**< フロー制御パケット。受信側が送信側へ、抑制の要求または解除と、送出上限 (送信側がこの通番以上の新しい DATA を送出しない通番) を伝えます。送出する側自身のセッション識別子を使い、seq_num と ack_num は 0。状態が抑制の要求と解除の場合、ペイロードは 5 [byte] で、1 [byte] の状態 (POTR_FLOW_CONTROL_STATE_*) と 4 [byte] の送出上限 (ネットワーク バイト オーダー)。状態が監視対象の一覧 (POTR_FLOW_CONTROL_STATE_MONITOR) の場合は、multicast と broadcast の送信者が自身のセッション識別子で送り、ペイロードは 1 [byte] の状態、1 [byte] の台数、台数分の 16 [byte] の受信者のセッション識別子。暗号化有効時は、同じ向きの受信報告または PING と同じ方式で保護します。 */

/** @} */

/** @defgroup POTR_ELEM_FLAG ペイロード エレメント フラグ (パック コンテナー内エレメント ヘッダー.flags)
 *  @ingroup        PORTER_PUBLIC_API
 *  POTR_FLAG_DATA パケットのペイロード内に格納されるペイロード エレメントのヘッダー flags フィールドに設定するフラグです。\n
 *  圧縮・フラグメント化はメッセージ単位で行われ、ペイロード エレメント単位で管理します。\n
 *  外側パケットのフラグ (@ref POTR_OUTER_FLAG) には設定しません。
 */

/**
 *  @ingroup        POTR_ELEM_FLAG
 *  @{
 */
#define POTR_FLAG_MORE_FRAG \
    0x0001U /**< 後続フラグメントが存在することを示すペイロード エレメント フラグ。メッセージが複数ペイロード エレメントに分割された場合、最終フラグメント以外に設定します。 */
#define POTR_FLAG_COMPRESSED \
    0x0002U /**< ペイロードが圧縮されていることを示すペイロード エレメント フラグ。圧縮はメッセージ単位で行い、全フラグメントのペイロード エレメントに設定します。先頭 8 [byte] が元サイズ (NBO)、続くデータが raw DEFLATE。 */
/** @} */

/** @defgroup POTR_PROTOCOL プロトコル定数
 *  @ingroup        PORTER_PUBLIC_API
 */

/**
 *  @ingroup        POTR_PROTOCOL
 *  @{
 */
#define POTR_PROTOCOL_VERSION 3U /**< 現行の porter ワイヤー プロトコル バージョン。 */
/** @} */

/** @defgroup POTR_DEFAULT デフォルト値
 *  @ingroup        PORTER_PUBLIC_API
 */

/**
 *  @ingroup        POTR_DEFAULT
 *  @{
 */
#define POTR_DEFAULT_WINDOW_SIZE  16U   /**< デフォルト ウィンドウ サイズ (パケット数)。 */
#define POTR_DEFAULT_MAX_PAYLOAD  1400U /**< デフォルト最大ペイロード長 [byte]。 */
#define POTR_DEFAULT_TTL          1U    /**< デフォルト マルチキャスト TTL。 */
#define POTR_DEFAULT_PACK_WAIT_MS 0U    /**< デフォルト パッキング待ち時間 [ms]。0 = 即時送信。 */
#define POTR_DEFAULT_RECONNECT_INTERVAL_MS \
    5000U /**< SENDER 自動再接続間隔のデフォルト [ms]。TCP 通信種別 (POTR_TYPE_TCP / POTR_TYPE_TCP_BIDIR) のみ有効。 */
#define POTR_DEFAULT_CONNECT_TIMEOUT_MS \
    10000U /**< SENDER TCP 接続タイムアウトのデフォルト [ms]。0 = OS デフォルト。TCP 通信種別のみ有効。 */
#define POTR_DEFAULT_UDP_HEALTH_INTERVAL_MS \
    3000U /**< UDP 通信種別に適用する既定ヘルス チェック送信間隔 [ms]。0 = 無効。 */
#define POTR_DEFAULT_UDP_HEALTH_TIMEOUT_MS \
    10000U /**< UDP 通信種別に適用する既定ヘルス チェック タイムアウト [ms]。0 = 無効。 */
#define POTR_DEFAULT_TCP_HEALTH_INTERVAL_MS \
    10000U /**< TCP 通信種別に適用する既定ヘルス チェック送信間隔 [ms]。0 = 無効。 */
#define POTR_DEFAULT_TCP_HEALTH_TIMEOUT_MS \
    31000U /**< TCP 通信種別に適用する既定ヘルス チェック タイムアウト [ms]。0 = 無効。 */
#define POTR_DEFAULT_TCP_CLOSE_TIMEOUT_MS \
    5000U /**< TCP 通信種別に適用する close 完了待機タイムアウト [ms]。0 = 待機なし。 */
#define POTR_TCP_FIRST_PKT_TIMEOUT_SCALE \
    3U /**< TCP 先読みパケット タイムアウトの係数。tcp_health_timeout_ms にこの値を乗じて先読みタイムアウトを算出します。 */
#define POTR_DEFAULT_TCP_FIRST_PKT_TIMEOUT_MS \
    30000U /**< TCP 先読みパケット タイムアウトの既定値 [ms]。tcp_health_timeout_ms が 0 (無効) の場合に使用します。 */
/** @} */

/** @defgroup POTR_CRYPTO 暗号化定数 (AES-256-GCM)
 *  @ingroup        PORTER_PUBLIC_API
 *  POTR_FLAG_ENCRYPTED が設定されたパケットの暗号化・復号に使用する定数です。
 */

/**
 *  @ingroup        POTR_CRYPTO
 *  @{
 */
#define POTR_CRYPTO_KEY_SIZE \
    32U /**< AES-256-GCM 鍵サイズ [byte]。設定ファイルの encrypt_key に 64 文字 hex で指定します。 */
#define POTR_CRYPTO_NONCE_SIZE \
    12U /**< AES-256-GCM ノンス サイズ [byte]。session_id (4B NBO) + tx_counter (8B NBO) で構成し、72 [byte]のヘッダー全体を AAD に使用します。 */
#define POTR_CRYPTO_TAG_SIZE 16U /**< AES-256-GCM 認証タグ サイズ [byte]。暗号文の直後に付加します。 */
/** @} */

/** @defgroup POTR_LIMIT 上限値・デフォルト値
 *  @ingroup        PORTER_PUBLIC_API
 */

/**
 *  @ingroup        POTR_LIMIT
 *  @{
 */
#define POTR_MAX_ADDR_LEN 64U /**< アドレス文字列の最大長 (バイト、終端 NUL を含む)。 */
#define POTR_UDP_MAX_DATAGRAM_PAYLOAD 65507U /**< IPv4 UDP データグラムの最大ペイロード長 [byte]。65535 - IP20 - UDP8。 */
#define POTR_PACKET_HEADER_WIRE_SIZE 72U /**< 外側パケットのワイヤー ヘッダー長 [byte]。 */
#define POTR_MAX_PAYLOAD \
    (POTR_UDP_MAX_DATAGRAM_PAYLOAD - POTR_PACKET_HEADER_WIRE_SIZE) /**< ペイロードの最大長 [byte]。max_payload 設定値の検査上限。 */
#define POTR_MAX_WINDOW_SIZE \
    256U /**< ウィンドウ サイズの最大値 (パケット数)。window_size 設定値のバリデーション上限として使用します。 */
#define POTR_MAX_SERVICES \
    64U /**< potr_internal_config_list_service_ids() の初期バッファー容量。サービス数がこれを超えた場合は realloc で自動拡張します。 */
#define POTR_MAX_MESSAGE_SIZE \
    65535U /**< 1 回の potr_service_send で送信できる最大メッセージ長 [byte] のデフォルト値。設定ファイルの max_message_size で変更可能。フラグメント化により max_payload を超えるメッセージも送受信できます。 */
#define POTR_SEND_QUEUE_DEPTH \
    1024U /**< 非同期送信キューの最大エントリ数のデフォルト値。設定ファイルの send_queue_depth で変更可能。メッセージがフラグメント化される場合、1 メッセージが複数エントリを占有します。 */
#define POTR_PAYLOAD_ELEM_HDR_SIZE \
    6U /**< パック コンテナー内ペイロード エレメントのヘッダー サイズ [byte]。flags (2): POTR_FLAG_MORE_FRAG / POTR_FLAG_COMPRESSED を格納 + payload_len (4): uint32_t (NBO)。通番は外側パケットで管理するためペイロード エレメントには含まない。 */
#define POTR_MAX_PATH 8U /**< マルチパスの最大パス数。 */
#define POTR_MAX_PATH_GROUP 4U /**< 経路の優先グループ番号の最大値。番号が小さいほど優先度が高い。 */
/** @} */

/** @defgroup POTR_PING_STATE PING 受信状態 (PING ペイロードの各パス フィールド値)
 *  @ingroup        PORTER_PUBLIC_API
 *  PING パケットのペイロードに格納するパスごとの PING 受信状態です。\n
 *  双方向 PING 形態 (UNICAST_BIDIR / UNICAST_BIDIR_N1 / TCP / TCP_BIDIR) では実際の状態を格納します。\n
 *  片方向 PING 形態 (UNICAST / MULTICAST / BROADCAST / RAW 系) では常に UNDEFINED を格納します。
 */

/**
 *  @ingroup        POTR_PING_STATE
 *  @{
 */
/**
 *  @brief          PING 受信状態です。
 *
 *  PING と受信報告のペイロードでは 1 [byte] で表します。
 *  配列、引数、構造体のメンバーの型は、uint8_t のままとします。
 */
typedef enum potr_ping_state
{
    POTR_PING_STATE_UNDEFINED =
        0,                       /**< 不定: 片方向のため PING を受信しない、またはまだ一度も PING を受信していない。 */
    POTR_PING_STATE_NORMAL = 1,  /**< 正常: 定周期で PING を受信中。 */
    POTR_PING_STATE_ABNORMAL = 2 /**< 異常: PING タイムアウト (途絶)。 */
} potr_ping_state;
/** @} */

/** @defgroup POTR_FLOW_CONTROL フロー制御
 *  @ingroup        PORTER_PUBLIC_API
 *  受信側の通知キューの占有量に基づいて、送信側が新しい DATA の送出を止める仕組みの定数です。\n
 *  対象は UNICAST / MULTICAST / BROADCAST / UNICAST_BIDIR / UNICAST_BIDIR_N1 / TCP / TCP_BIDIR です。\n
 *  OS の受信バッファーに基づく送出上限は、UNICAST / MULTICAST / BROADCAST / UNICAST_BIDIR / UNICAST_BIDIR_N1 の
 *  受信側で、容量の設定によらず働きます。
 *  RAW 系の通信種別では、フロー制御の設定を無視します。
 */

/**
 *  @ingroup        POTR_FLOW_CONTROL
 *  @{
 */
/**
 *  @brief          フロー制御パケットの状態です。
 *
 *  フロー制御パケットのペイロードでは 1 [byte] で表します。
 *  引数と構造体のメンバーの型は、uint8_t のままとします。
 */
typedef enum potr_flow_control_state
{
    POTR_FLOW_CONTROL_STATE_SUPPRESS = 1, /**< フロー制御パケットの状態: 抑制の要求。 */
    POTR_FLOW_CONTROL_STATE_RESUME = 2,   /**< フロー制御パケットの状態: 解除。 */
    POTR_FLOW_CONTROL_STATE_MONITOR =
        3 /**< フロー制御パケットの状態: 監視対象の一覧。multicast と broadcast の送信者が受信者へ送ります。 */
} potr_flow_control_state;

#define POTR_FLOW_CONTROL_MONITORED_RECEIVER_MAX \
    64U /**< multicast と broadcast の送信者が監視する受信者の台数の上限。flow_control_monitored_receiver_count の検査上限です。 */
#define POTR_FLOW_CONTROL_DISABLED \
    0xFFFFFFFFU /**< 「無効」を表す専用の値。サービス定義の flow_control_capacity、flow_control_receiver_percent、flow_control_receiver_count、flow_control_monitored_receiver_percent、flow_control_monitored_receiver_count と、グローバル設定とサービス定義の flow_control_receive_buffer_percent に指定できます。設定ファイルでは -1 と記載します。 */
#define POTR_DEFAULT_FLOW_CONTROL_CAPACITY 0U /**< 通知キューの容量のデフォルト [byte]。0 = フロー制御は無効。 */
#define POTR_DEFAULT_FLOW_CONTROL_SUPPRESS_PERCENT 80U /**< 抑制を要求するしきい値のデフォルト [%]。 */
#define POTR_DEFAULT_FLOW_CONTROL_RESUME_PERCENT   50U /**< 解除するしきい値のデフォルト [%]。 */
#define POTR_DEFAULT_FLOW_CONTROL_REQUEST_INTERVAL_MS \
    200U /**< 抑制の要求の再送周期のデフォルト [ms]。 */
#define POTR_DEFAULT_FLOW_CONTROL_REQUEST_LIFETIME_MS \
    1000U /**< 抑制の要求の有効期限のデフォルト [ms]。 */
#define POTR_DEFAULT_FLOW_CONTROL_RECEIVER_PERCENT \
    0U /**< 一対多の送信側が抑制する、要求中の受信者の割合のデフォルト [%]。0 = 割合の条件は無効。 */
#define POTR_DEFAULT_FLOW_CONTROL_RECEIVER_COUNT \
    2U /**< 一対多の送信側が抑制する、要求中の受信者の台数のデフォルト。 */
#define POTR_DEFAULT_FLOW_CONTROL_MONITORED_RECEIVER_PERCENT \
    0U /**< multicast と broadcast の送信側が監視する受信者の割合のデフォルト [%]。0 = 割合は使わない。 */
#define POTR_DEFAULT_FLOW_CONTROL_MONITORED_RECEIVER_COUNT \
    4U /**< multicast と broadcast の送信側が監視する受信者の台数のデフォルト。 */
#define POTR_DEFAULT_FLOW_CONTROL_RECEIVE_BUFFER_PERCENT \
    100U /**< OS の受信バッファーの大きさのうち、受信バッファーの幅の計算に使う割合のデフォルト [%]。 */
/** @} */

#endif /* PORTER_CONST_H */
