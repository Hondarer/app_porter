/**
 *******************************************************************************
 *  @file           porter_type.h
 *  @brief          通信ライブラリの型定義ファイル。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef PORTER_TYPE_H
#define PORTER_TYPE_H

#include <stddef.h>
#include <stdint.h>

#include <porter_const.h>

/**
 *******************************************************************************
 *  @brief          ピア識別子。
 *
 *  @details
 *  N:1 モードで各クライアントを識別する ID です。\n
 *  N:1 モードでは有効なピア ID は常に POTR_PEER_NA および POTR_PEER_ALL 以外の値です。\n
 *  1:1 モードおよびその他の通信種別では peer_id は常に POTR_PEER_NA です。
 *******************************************************************************
 */
typedef uint32_t PotrPeerId;

/**
 *******************************************************************************
 *  @brief          通信種別。
 *
 *  @details
 *  サービス定義の通信方式を表します。
 * 
 *  - POTR_TYPE_UNICAST_RAW, POTR_TYPE_MULTICAST_RAW, POTR_TYPE_BROADCAST_RAW\n
 *    RAW モード (再送なし、ベストエフォート)。\n
 *    受信ウィンドウによる順序整列とセッション管理は有効。\n
 *    ギャップ検出時は NACK の代わりに POTR_EVENT_DISCONNECTED を発行する。\n
 *    通番は AES ノンス用にインクリメントするが、再送制御には使用しない。\n
 *    potrSend は常にブロッキング送信。マルチパス対応。\n
 *    PING ヘルスチェックは health_interval_ms / health_timeout_ms の設定に従う。
 * 
 *  - POTR_TYPE_UNICAST, POTR_TYPE_MULTICAST, POTR_TYPE_BROADCAST\n
 *    再送制御あり UDP モード。\n
 *    NACK / 再送・スライディングウィンドウによる信頼性・順序保証。\n
 *    ギャップ検出時は RECEIVER が SENDER に NACK を送信し再送を要求する。\n
 *    再送不可能な場合は REJECT を送出し POTR_EVENT_DISCONNECTED を発行する。\n
 *    potrSend は送信ウィンドウに空きがある場合は非同期、満杯の場合はブロッキング。\n
 * 
 *  - POTR_TYPE_UNICAST_BIDIR\n
 *    双方向 1:1 通信 (UDP ユニキャスト)。\n
 *    SENDER / RECEIVER ともに送受信・NACK・ヘルスチェックを独立して行う。\n
 *    両端それぞれが src_addr:src_port で bind し、相手の dst_addr:dst_port へ送信する。\n
 *    src_port = 0 を指定すると SENDER はエフェメラルポートで bind し、\n
 *    RECEIVER は最初のパケット受信時に SENDER のポートを動的学習して返信する。\n
 *    src_addr を省略した場合:\n
 *      SENDER  : INADDR_ANY で bind し、OS がルーティングに基づきアダプタを自動選択する。\n
 *      RECEIVER: 最初の受信パケットから SENDER のアドレスを動的学習する (1:1 通信を維持)。\n
 *    SENDER も callback が必須。
 * 
 *  - POTR_TYPE_UNICAST_BIDIR_N1\n
 *    N:1 双方向通信 (UDP ユニキャスト)。\n
 *    サーバ側は dst_addr:dst_port で bind し、複数クライアントを同時に受け入れる。\n
 *    クライアントは POTR_TYPE_UNICAST_BIDIR (1:1) として接続する。\n
 *    max_peers で最大同時接続数を制御 (省略時: 1024)。\n
 *    src_addr は不要。src_port 指定でポートフィルタ付き N:1 になる。
 * 
 *  - POTR_TYPE_TCP, POTR_TYPE_TCP_BIDIR\n
 *    TCP ユニキャスト通信。\n
 *    TCP 3way handshake による接続確立。SENDER が connect()、RECEIVER が accept()。\n
 *    NACK / 再送なし (TCP 自体が信頼性・順序を保証)。\n
 *    スライディングウィンドウなし (TCP のフロー制御に委ねる)。\n
 *    SENDER 側は reconnect_interval_ms / connect_timeout_ms で再接続挙動を制御。\n
 *    接続切断時は POTR_EVENT_DISCONNECTED を発行する。
 * 
 *******************************************************************************
 */
typedef enum
{
    POTR_TYPE_UNICAST_RAW   = 1, /**< 1:1 通信 RAW モード (UDP ユニキャスト)。 */
    POTR_TYPE_MULTICAST_RAW = 2, /**< 1:N 通信 RAW モード (UDP マルチキャスト)。 */
    POTR_TYPE_BROADCAST_RAW = 3, /**< 1:N 通信 RAW モード (UDP ブロードキャスト)。 */

    POTR_TYPE_UNICAST   = 4, /**< 1:1 通信 (UDP ユニキャスト)。 */
    POTR_TYPE_MULTICAST = 5, /**< 1:N 通信 (UDP マルチキャスト)。 */
    POTR_TYPE_BROADCAST = 6, /**< 1:N 通信 (UDP ブロードキャスト)。 */

    POTR_TYPE_UNICAST_BIDIR    = 7, /**< 双方向 1:1 通信 (UDP ユニキャスト)。 */

    POTR_TYPE_UNICAST_BIDIR_N1 = 8, /**< N:1 双方向通信 (UDP ユニキャスト)。 */

    POTR_TYPE_TCP             = 9,  /**< TCP ユニキャスト通信 (単方向: SENDER のみ potrSend 可)。 */

    POTR_TYPE_TCP_BIDIR       = 10, /**< TCP 双方向通信 (両端が potrSend 可)。 */

    /* POTR_TYPE_TCP_BIDIR_N1 = 11, */ /**< TCP 双方向 N:1 通信 (将来)。 */
} PotrType;

/**
 *******************************************************************************
 *  @brief          役割種別。
 *
 *  @details
 *  potrOpenService() の呼び出し元がデータを送信する役割か受信する役割かを明示します。
 *******************************************************************************
 */
typedef enum
{
    POTR_ROLE_SENDER   = 1, /**< 送信者。 */
    POTR_ROLE_RECEIVER = 2, /**< 受信者。 */
} PotrRole;

/**
 *******************************************************************************
 *  @brief          サービス定義。
 *
 *  @details
 *  設定ファイルの [service.N] セクションから読み込まれるサービス設定です。
 *
 *  通信種別によって有効なフィールドが異なります。
 *******************************************************************************
 */
typedef struct
{
    int64_t  service_id; /**< サービス ID。 */
    PotrType type;       /**< 通信種別。 */

    /* POTR_TYPE_UNICAST */
    uint16_t dst_port; /**< 宛先ポート番号。サービスの識別子。受信者の bind ポート / 送信者の送信先ポート。(全通信種別で必須) */

    /* POTR_TYPE_MULTICAST / POTR_TYPE_BROADCAST */
    uint16_t src_port; /**< 送信者の送信元 bind ポート番号。0 = OS 自動選定。(全通信種別で省略可) */

    /* POTR_TYPE_MULTICAST */
    uint8_t  ttl;           /**< マルチキャスト TTL。(multicast のみ) */
    uint8_t  _pad[3];       /**< パディング (pack_wait_ms を 4 バイト境界に揃える)。 */
    uint32_t pack_wait_ms;  /**< パッキング待ち時間 (ミリ秒)。最初の送信要求からこの時間だけ待ち合わせ、複数メッセージを 1 パケットにまとめる。0 = 即時送信 (パッキング待ちなし)。パケット容量が満杯になった場合はタイマーを無視して即時送信する。 */

    char multicast_group[POTR_MAX_ADDR_LEN]; /**< マルチキャストグループアドレス。(multicast のみ) */
    char broadcast_addr[POTR_MAX_ADDR_LEN];  /**< ブロードキャスト宛先アドレス。省略時は 255.255.255.255。(broadcast のみ) */

    /* マルチパス対応 (src_addr は全通信種別、dst_addr は unicast のみ有効) */
    char src_addr[POTR_MAX_PATH][POTR_MAX_ADDR_LEN]; /**< 送信元アドレス [0]=src_addr1 〜 [3]=src_addr4。送信者は bind / 送信インターフェース、受信者は送信元フィルタ。(全通信種別で必須) */
    char dst_addr[POTR_MAX_PATH][POTR_MAX_ADDR_LEN]; /**< 宛先アドレス [0]=dst_addr1 〜 [3]=dst_addr4。送信者は送信先、受信者は bind アドレス。(unicast のみ) */

    /* 暗号化設定 (AES-256-GCM) */
    uint8_t  encrypt_key[POTR_CRYPTO_KEY_SIZE]; /**< AES-256-GCM 事前共有鍵 (32 バイト)。encrypt_enabled が 0 の場合は未使用。 */
    int      encrypt_enabled;                   /**< 非 0 のとき暗号化有効。設定ファイルに有効な encrypt_key が存在するときに 1 に設定される。 */

    /* N:1 モード設定 */
    uint32_t max_peers; /**< N:1 モード時の最大同時接続ピア数。省略時: 1024。1:1 モードでは無視される。 */

    /* サービス単位のヘルスチェック上書き (0 = グローバル値を使用) */
    uint32_t health_interval_ms; /**< グローバルの udp/tcp_health_interval_ms をサービス単位で上書きする。0 = グローバル値を使用。 */
    uint32_t health_timeout_ms;  /**< グローバルの udp/tcp_health_timeout_ms をサービス単位で上書きする。0 = グローバル値を使用。 */

    /* TCP 固有フィールド (POTR_TYPE_TCP / POTR_TYPE_TCP_BIDIR 以外では無視) */
    uint32_t reconnect_interval_ms; /**< SENDER 自動再接続間隔 (ms)。0 = 再接続なし。デフォルト: POTR_DEFAULT_RECONNECT_INTERVAL_MS。 */
    uint32_t connect_timeout_ms;    /**< SENDER TCP 接続タイムアウト (ms)。0 = OS デフォルト。デフォルト: POTR_DEFAULT_CONNECT_TIMEOUT_MS。 */
} PotrServiceDef;

/**
 *******************************************************************************
 *  @brief          グローバル設定。
 *
 *  @details
 *  設定ファイルの [global] セクションから読み込まれる共通プロトコル設定です。
 *******************************************************************************
 */
typedef struct
{
    uint16_t window_size;             /**< スライディングウィンドウサイズ (パケット数)。 */
    uint16_t max_payload;             /**< 最大ペイロード長 (バイト)。 */
    uint32_t reorder_timeout_ms;      /**< 受信ウィンドウ欠番検出後、NACK または切断を遅延する時間 (ミリ秒)。マルチパスや近距離 WAN での追い越し吸収用。0 = 即時 (デフォルト)。推奨値: LAN/マルチパス=10〜30 ms、遠距離 WAN=30〜100 ms。 */
    uint32_t max_message_size;        /**< 1 回の potrSend で送信できる最大メッセージ長 (バイト)。デフォルト: POTR_MAX_MESSAGE_SIZE。 */
    uint32_t send_queue_depth;        /**< 非同期送信キューの最大エントリ数。デフォルト: POTR_SEND_QUEUE_DEPTH。 */
    uint32_t udp_health_interval_ms;  /**< UDP 通信種別の既定 PING 送信間隔 (ミリ秒)。設定周期ごとに PING を送信する。0 = 無効。設定ファイルキー: udp_health_interval_ms。 */
    uint32_t udp_health_timeout_ms;   /**< UDP 通信種別の既定 PING 応答待機タイムアウト (ミリ秒)。0 = 無効。設定ファイルキー: udp_health_timeout_ms。 */
    uint32_t tcp_health_interval_ms;  /**< TCP 通信種別の既定 PING 送信間隔 (ミリ秒)。設定周期ごとに PING を送信する。0 = 無効。設定ファイルキー: tcp_health_interval_ms。 */
    uint32_t tcp_health_timeout_ms;   /**< TCP 通信種別の既定 PING 応答待機タイムアウト (ミリ秒)。0 = 無効。設定ファイルキー: tcp_health_timeout_ms。 */
    uint32_t tcp_close_timeout_ms;    /**< TCP 通信種別の close 完了待機タイムアウト (ミリ秒)。設定ファイルキー: tcp_close_timeout_ms。0 = 待機なし。 */
} PotrGlobalConfig;

/**
 *******************************************************************************
 *  @brief          ネットワーク送受信用パケット構造体。
 *
 *  @details
 *  UDP で送受信される物理パケットのレイアウトです。\n
 *  各フィールドはネットワークバイトオーダー (ビッグエンディアン) で格納します。\n
 *  ヘッダー固定長: offsetof(PotrPacket, payload) = 40 バイト (64 ビット環境)。\n
 *  payload フィールドはポインタであり、wire データとして直接 sendto に渡せません。\n
 *  送信時は PotrContext_ の send_wire_buf / recv_buf に wire データを組み立ててください。
 *
 *  ワイヤーフォーマット (バイトオフセット):
 *  @code
     0: service_id      (int64_t,  8 bytes)
     8: session_tv_sec  (int64_t,  8 bytes)
    16: session_id      (uint32_t, 4 bytes)
    20: session_tv_nsec (int32_t,  4 bytes)
    24: seq_num         (uint32_t, 4 bytes)
    28: ack_num         (uint32_t, 4 bytes)
    32: flags           (uint16_t, 2 bytes)
    34: payload_len     (uint16_t, 2 bytes)
    36: _reserved       (uint32_t, 4 bytes, padding)
    40: payload         (pointer)
 *  @endcode
 *******************************************************************************
 */
typedef struct
{
    int64_t  service_id;      /**< サービス識別子 (NBO)。受信時に照合する。 */
    int64_t  session_tv_sec;  /**< セッション開始時刻 秒部 (NBO)。struct timespec の tv_sec 相当。 */
    uint32_t session_id;      /**< セッション識別子 (NBO)。potrOpenService 時に決定する乱数。 */
    int32_t  session_tv_nsec; /**< セッション開始時刻 ナノ秒部 (NBO)。struct timespec の tv_nsec 相当。 */
    uint32_t seq_num;         /**< 通番。送信側が付与する連番 (NBO)。 */
    uint32_t ack_num;         /**< 再送要求番号 / 再送不能通番 (NBO)。NACK では要求通番、REJECT では再送不能通番を格納する。 */
    uint16_t flags;           /**< パケット種別フラグ (POTR_FLAG_*) (NBO)。 */
    uint16_t payload_len;     /**< ペイロード長 (バイト) (NBO)。 */
    uint32_t _reserved;       /**< パディング (payload ポインタを 8 バイト境界に揃える)。 */
    const uint8_t *payload;   /**< ペイロードデータへのポインタ (読み取り専用)。ウィンドウプールまたは受信バッファ内を指す。 */
} PotrPacket;

/**
 *******************************************************************************
 *  @brief          セッションハンドル。
 *
 *  @details
 *  potrOpenService() が返す不透明ポインタです。\n
 *  内部実装の詳細はライブラリ利用者からは隠蔽されます。
 *******************************************************************************
 */
typedef struct PotrContext_ *PotrHandle;

/**
 *******************************************************************************
 *  @brief          ピア識別子。
 *
 *  @details
 *  N:1 通信モード (`POTR_TYPE_UNICAST_BIDIR` src 情報なし) で各接続ピアを識別する ID です。\n
 *  1:1 通信モードおよびその他の通信種別では常に **`POTR_PEER_NA`** を使用します。\n
 *  有効な N:1 ピア ID は常に `POTR_PEER_NA` および `POTR_PEER_ALL` 以外の値となります（ピア ID 生成ロジックにより保証）。\n
 *  予約値については @ref POTR_PEER を参照してください。
 *******************************************************************************
 */
typedef uint32_t PotrPeerId;

/** @defgroup POTR_PEER ピア ID 予約値
 *  @{
 *  @details
 *  `potrSend()` の `peer_id` 引数および `PotrRecvCallback` の `peer_id` 引数で使用する予約値です。
 */
#define POTR_PEER_NA  ((PotrPeerId)0U)        /**< ピア ID 未割当を示す予約値。
                                                *   1:1 モードのコールバックで渡される (ピアの概念がない)。
                                                *   `potrSend()` に N:1 モードで指定した場合はエラーを返す。 */
#define POTR_PEER_ALL ((PotrPeerId)UINT32_MAX) /**< 全接続ピアへの一斉送信を指示する予約ピア ID。
                                                *   N:1 モードでは全アクティブピアへユニキャスト送信する。
                                                *   1:1 モードでは唯一のピアへの送信として動作する。 */
/** @} */

/**
 *******************************************************************************
 *  @brief          受信イベント種別。
 *
 *  @details
 *  PotrRecvCallback の第 2 引数に渡されるイベント種別です。
 *
 *  @note
 *  POTR_EVENT_CONNECTED / POTR_EVENT_DISCONNECTED は、data=NULL, len=0 で呼び出されます。\n
 *  POTR_EVENT_PATH_CONNECTED / POTR_EVENT_PATH_DISCONNECTED は、data に `const int*` の
 *  path 状態配列、len に対象 path index を渡します。\n
 *  すべてのイベントは内部で直列化されるため、順序性が保証されます。\n
 *  `CONNECTED` / `DISCONNECTED` は path 論理接続状態の OR が 0->1 / 1->0 に変化した時点で
 *  発火します。\n
 *  `PATH_CONNECTED` / `PATH_DISCONNECTED` は対象 path の論理接続状態が
 *  0->1 / 1->0 に変化した時点で発火します。\n
 *  path 論理接続状態の意味:\n
 *  - type 1-6: 有効な `PING` または `DATA` をその path で受理済み\n
 *  - type 7-10: ローカル path が有効、かつ相手 `PING` ペイロード内で当該 path が
 *    `POTR_PING_STATE_NORMAL`\n
 *  未接続中に受信した `DATA` は破棄され、`POTR_EVENT_DATA` は発火しません。\n
 *  DISCONNECTED の発火条件:\n
 *  - ヘルスチェック有効時 (health_timeout_ms > 0): タイムアウト / FIN 受信 / REJECT 受信\n
 *  - ヘルスチェック無効時 (health_timeout_ms = 0): FIN 受信 / REJECT 受信\n
 *  REJECT 受信後は DISCONNECTED の直後に、次のパケット受信で CONNECTED が発火します。
 *******************************************************************************
 */
typedef enum
{
    POTR_EVENT_DATA              = 0, /**< データ受信。data/len に内容が格納される。 */
    POTR_EVENT_CONNECTED         = 1, /**< 論理接続中の path が 1 本以上になった。data=NULL, len=0。 */
    POTR_EVENT_DISCONNECTED      = 2, /**< 論理接続中の path が 0 本になった。data=NULL, len=0。 */
    POTR_EVENT_PATH_CONNECTED    = 3, /**< path 論理接続が 0->1。data=path_states, len=path_idx。 */
    POTR_EVENT_PATH_DISCONNECTED = 4, /**< path 論理接続が 1->0。data=path_states, len=path_idx。 */
} PotrEvent;

/**
 *******************************************************************************
 *  @brief          受信コールバック関数型 (全通信種別共通)。
 *
 *  @details
 *  データ受信またはヘルスチェック状態変化時に、受信スレッドから呼び出されます。\n
 *  コールバック内でブロッキング処理を行わないようにしてください。\n
 *  すべてのイベントは受信スレッドから直列に呼び出されるため、順序性が保証されます。
 *
 *  @param[in]      service_id  サービスの ID。
 *  @param[in]      peer_id     ピア識別子。N:1 モード時は接続ピアの ID (`POTR_PEER_NA` / `POTR_PEER_ALL` 以外)。\n
 *                              1:1 モードおよびその他の通信種別では常に POTR_PEER_NA。
 *  @param[in]      event       イベント種別 (PotrEvent)。
 *  @param[in]      data        `POTR_EVENT_DATA` 時は受信データ、`POTR_EVENT_PATH_*` 時は
 *                              path 論理接続状態配列 (`const int[POTR_MAX_PATH]`) を指します。
 *                              それ以外のイベントでは NULL です。コールバック復帰後は無効になります。
 *  @param[in]      len         `POTR_EVENT_DATA` 時は受信データ長、`POTR_EVENT_PATH_*` 時は
 *                              対象 path index です。それ以外のイベントでは 0 です。
 *******************************************************************************
 */
typedef void (*PotrRecvCallback)(int64_t service_id, PotrPeerId peer_id,
                                 PotrEvent event,
                                 const void *data, size_t len);

#endif /* PORTER_TYPE_H */
