/**
 *******************************************************************************
 *  @file           porter.h
 *  @brief          通信ライブラリ (動的リンク用) のヘッダーファイル。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  このライブラリは UDP/IP を用いたプラットフォーム間通信を提供します。\n
 *  Linux / Windows クロスプラットフォーム対応の DLL として提供されます。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef PORTER_H
#define PORTER_H

#include <porter_type.h>
#include <com_util/base/platform.h>
#include <com_util/trace/tracer.h>

#ifdef DOXYGEN

    /**
     *  @def            POTR_EXPORT
     *  @brief          DLL エクスポート/インポート制御マクロ。
     *  @details        ビルド条件に応じて以下の値を取ります。
     *
     *  | 条件                                                  | 値                       |
     *  | ----------------------------------------------------- | ------------------------ |
     *  | Linux (非 Windows)                                    | (空)                     |
     *  | Windows / `__INTELLISENSE__` 定義時                   | (空)                     |
     *  | Windows / `POTR_STATIC` 定義時 (静的リンク)           | (空)                     |
     *  | Windows / `POTR_EXPORTS` 定義時 (DLL ビルド)          | `__declspec(dllexport)`  |
     *  | Windows / `POTR_EXPORTS` 未定義時 (DLL 利用側)        | `__declspec(dllimport)`  |
     */
    #define POTR_EXPORT

    /**
     *  @def            POTR_API
     *  @brief          呼び出し規約マクロ。
     *  @details        Windows 環境では `__stdcall` 呼び出し規約を指定します。\n
     *                  Linux (非 Windows) 環境では空に展開されます。\n
     *                  すでに定義済みの場合は再定義されません。
     */
    #define POTR_API

#else /* !DOXYGEN */

    #ifndef POTR_STATIC
        #define POTR_STATIC 0
    #endif /* POTR_STATIC */
    #ifndef POTR_EXPORTS
        #define POTR_EXPORTS 0
    #endif /* POTR_EXPORTS */
    #include <com_util/base/dll_exports.h>
    #define POTR_EXPORT COM_UTIL_DLL_EXPORT(POTR)
    #define POTR_API    COM_UTIL_DLL_API(POTR)

#endif /* DOXYGEN */

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *******************************************************************************
     *  @brief          設定構造体から指定サービスを開きます。
     *  @param[in]      global      グローバル設定構造体へのポインタ。
     *  @param[in]      service     サービス定義構造体へのポインタ。
     *  @param[in]      role        役割種別。POTR_ROLE_SENDER または POTR_ROLE_RECEIVER。
     *  @param[in]      callback    イベント発生時に呼び出されるコールバック関数 (PotrRecvCallback)。
     *                              POTR_ROLE_RECEIVER の場合は必須。データ受信・接続検知・切断検知を受け取る。
     *                              POTR_ROLE_SENDER の場合は通常 NULL を指定すること。\n
     *                              ただし POTR_TYPE_TCP_BIDIR および POTR_TYPE_UNICAST_BIDIR では
     *                              SENDER にもコールバックが必須。これらの種別では POTR_ROLE_SENDER でも
     *                              callback が NULL の場合は失敗を返します。
     *  @param[out]     handle      成功時にセッションハンドルを格納するポインタ。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  @details
     *  設定構造体からサービス定義を取得し、UDP ソケットを初期化します。\n
     *  role と callback の組み合わせが不正な場合は POTR_ERROR を返します。\n
     *  role と設定の IP アドレスが不整合 (bind 失敗など) の場合も POTR_ERROR を返します。\n
     *  通信種別・役割に応じて以下のソケット設定を行います。
     *
     *  | 通信種別              | 役割     | bind アドレス     | bind ポート   |
     *  | --------------------- | -------- | ----------------- | ------------- |
     *  | POTR_TYPE_UNICAST     | 送信者   | src_addr          | src_port      |
     *  | POTR_TYPE_UNICAST     | 受信者   | dst_addr          | dst_port      |
     *  | POTR_TYPE_MULTICAST   | 送信者   | INADDR_ANY        | src_port      |
     *  | POTR_TYPE_MULTICAST   | 受信者   | INADDR_ANY        | dst_port      |
     *  | POTR_TYPE_BROADCAST   | 送信者   | src_addr          | src_port      |
     *  | POTR_TYPE_BROADCAST   | 受信者   | INADDR_ANY        | dst_port      |
     *
     *  POTR_ROLE_RECEIVER の場合、内部で受信スレッドを起動します。
     *
     *  @par            使用例 (受信者)
     *  @code{.c}
        void on_recv(int64_t service_id, PotrPeerId peer_id,
                     PotrEvent event, const void *data, size_t len) {
            (void)peer_id;  // 1:1 モードでは常に POTR_PEER_NA
            if (event == POTR_EVENT_CONNECTED)
                printf("service %" PRId64 ": connected\n", service_id);
            else if (event == POTR_EVENT_DISCONNECTED)
                printf("service %" PRId64 ": disconnected\n", service_id);
            else
                printf("service %" PRId64 ": received %zu bytes\n", service_id, len);
        }

        PotrGlobalConfig global = {0};
        global.window_size        = 16;
        global.max_payload        = 1400;
        global.udp_health_interval_ms = 3000;
        global.udp_health_timeout_ms  = 10000;
        global.tcp_health_interval_ms = 10000;
        global.tcp_health_timeout_ms  = 31000;
        global.max_message_size   = 65535;
        global.send_queue_depth   = 64;

        PotrServiceDef service = {0};
        service.service_id = 1001;
        service.type       = POTR_TYPE_UNICAST;
        service.dst_port   = 49001;
        strncpy(service.src_addr[0], "127.0.0.1", POTR_MAX_ADDR_LEN - 1);
        strncpy(service.dst_addr[0], "127.0.0.1", POTR_MAX_ADDR_LEN - 1);

        PotrHandle handle;
        if (potrOpenService(&global, &service,
                            POTR_ROLE_RECEIVER, on_recv, &handle) == POTR_SUCCESS) {
            // 受信待機中 (受信スレッドが動作)
            potrCloseService(handle);
        }
     *  @endcode
     *
     *  @par            使用例 (送信者)
     *  @code{.c}
        PotrGlobalConfig global = {0};
        global.window_size        = 16;
        global.max_payload        = 1400;
        global.udp_health_interval_ms = 3000;
        global.udp_health_timeout_ms  = 10000;
        global.tcp_health_interval_ms = 10000;
        global.tcp_health_timeout_ms  = 31000;
        global.max_message_size   = 65535;
        global.send_queue_depth   = 64;

        PotrServiceDef service = {0};
        service.service_id = 1001;
        service.type       = POTR_TYPE_UNICAST;
        service.dst_port   = 49001;
        strncpy(service.src_addr[0], "127.0.0.1", POTR_MAX_ADDR_LEN - 1);
        strncpy(service.dst_addr[0], "127.0.0.1", POTR_MAX_ADDR_LEN - 1);

        PotrHandle handle;
        if (potrOpenService(&global, &service,
                            POTR_ROLE_SENDER, NULL, &handle) == POTR_SUCCESS) {
            potrSend(handle, POTR_PEER_NA, "hello", 5, 0);
            potrCloseService(handle);
        }
     *  @endcode
     *
     *  @par            スレッド セーフティ
     *  本関数はスレッドセーフです。\n
     *  異なるスレッドから独立したハンドルを取得するために並行して呼び出すことができます。\n
     *  ただし取得したハンドルはスレッドセーフではありません。\n
     *  同一ハンドルに対する操作は 1 スレッドから行ってください。
     *
     *  @warning        global が NULL の場合は失敗を返します。\n
     *                  service が NULL の場合は失敗を返します。\n
     *                  handle が NULL の場合は失敗を返します。\n
     *                  POTR_ROLE_RECEIVER かつ callback が NULL の場合は失敗を返します。\n
     *                  POTR_ROLE_SENDER かつ callback が NULL でない場合は失敗を返します。\n
     *                  ただし POTR_TYPE_TCP_BIDIR および POTR_TYPE_UNICAST_BIDIR では SENDER にも\n
     *                  コールバックが必須であり、この場合 callback が NULL の場合は失敗を返します。
     *******************************************************************************
     */
    POTR_EXPORT extern int POTR_API potrOpenService(const PotrGlobalConfig *global, const PotrServiceDef *service,
                                                    PotrRole role, PotrRecvCallback callback, PotrHandle *handle);

    /**
     *******************************************************************************
     *  @brief          設定ファイルから指定サービスを開きます。
     *  @param[in]      config_path 設定ファイルのパス。
     *  @param[in]      service_id  開くサービスの ID。
     *  @param[in]      role        役割種別。POTR_ROLE_SENDER または POTR_ROLE_RECEIVER。
     *  @param[in]      callback    イベント発生時に呼び出されるコールバック関数 (PotrRecvCallback)。
     *                              POTR_ROLE_RECEIVER の場合は必須。データ受信・接続検知・切断検知を受け取る。
     *                              POTR_ROLE_SENDER の場合は通常 NULL を指定すること。\n
     *                              ただし POTR_TYPE_TCP_BIDIR および POTR_TYPE_UNICAST_BIDIR では
     *                              SENDER にもコールバックが必須。これらの種別では POTR_ROLE_SENDER でも
     *                              callback が NULL の場合は失敗を返します。
     *  @param[out]     handle      成功時にセッションハンドルを格納するポインタ。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  @details
     *  設定ファイルを解析してサービス定義を取得し、potrOpenService() を呼び出します。\n
     *  role と callback の組み合わせが不正な場合は POTR_ERROR を返します。\n
     *  role と設定ファイルの IP アドレスが不整合 (bind 失敗など) の場合も POTR_ERROR を返します。\n
     *  通信種別・役割に応じて以下のソケット設定を行います。
     *
     *  | 通信種別              | 役割     | bind アドレス     | bind ポート   |
     *  | --------------------- | -------- | ----------------- | ------------- |
     *  | POTR_TYPE_UNICAST     | 送信者   | src_addr          | src_port      |
     *  | POTR_TYPE_UNICAST     | 受信者   | dst_addr          | dst_port      |
     *  | POTR_TYPE_MULTICAST   | 送信者   | INADDR_ANY        | src_port      |
     *  | POTR_TYPE_MULTICAST   | 受信者   | INADDR_ANY        | dst_port      |
     *  | POTR_TYPE_BROADCAST   | 送信者   | src_addr          | src_port      |
     *  | POTR_TYPE_BROADCAST   | 受信者   | INADDR_ANY        | dst_port      |
     *
     *  POTR_ROLE_RECEIVER の場合、内部で受信スレッドを起動します。
     *
     *  @par            使用例 (受信者)
     *  @code{.c}
        void on_recv(int64_t service_id, PotrPeerId peer_id,
                     PotrEvent event, const void *data, size_t len) {
            (void)peer_id;  // 1:1 モードでは常に POTR_PEER_NA
            if (event == POTR_EVENT_CONNECTED)
                printf("service %" PRId64 ": connected\n", service_id);
            else if (event == POTR_EVENT_DISCONNECTED)
                printf("service %" PRId64 ": disconnected\n", service_id);
            else
                printf("service %" PRId64 ": received %zu bytes\n", service_id, len);
        }

        PotrHandle handle;
        if (potrOpenServiceFromConfig("porter-services.conf", 1001,
                                      POTR_ROLE_RECEIVER, on_recv, &handle) == POTR_SUCCESS) {
            // 受信待機中 (受信スレッドが動作)
            potrCloseService(handle);
        }
     *  @endcode
     *
     *  @par            使用例 (送信者)
     *  @code{.c}
        PotrHandle handle;
        if (potrOpenServiceFromConfig("porter-services.conf", 1001,
                                      POTR_ROLE_SENDER, NULL, &handle) == POTR_SUCCESS) {
            potrSend(handle, POTR_PEER_NA, "hello", 5, 0);
            potrCloseService(handle);
        }
     *  @endcode
     *
     *  @par            スレッド セーフティ
     *  本関数はスレッドセーフです。\n
     *  異なるスレッドから独立したハンドルを取得するために並行して呼び出すことができます。\n
     *  ただし取得したハンドルはスレッドセーフではありません。\n
     *  同一ハンドルに対する操作は 1 スレッドから行ってください。
     *
     *  @warning        handle が NULL の場合は失敗を返します。\n
     *                  config_path が NULL または存在しない場合は失敗を返します。\n
     *                  指定した service_id が設定ファイルに存在しない場合は失敗を返します。\n
     *                  POTR_ROLE_RECEIVER かつ callback が NULL の場合は失敗を返します。\n
     *                  POTR_ROLE_SENDER かつ callback が NULL でない場合は失敗を返します。\n
     *                  ただし POTR_TYPE_TCP_BIDIR および POTR_TYPE_UNICAST_BIDIR では SENDER にも\n
     *                  コールバックが必須であり、この場合 callback が NULL の場合は失敗を返します。
     *******************************************************************************
     */
    POTR_EXPORT extern int POTR_API potrOpenServiceFromConfig(const char *config_path, int64_t service_id,
                                                              PotrRole role, PotrRecvCallback callback,
                                                              PotrHandle *handle);

    /**
     *******************************************************************************
     *  @brief          メッセージを送信します。
     *  @param[in]      handle      potrOpenService() で取得したセッションハンドル。
     *  @param[in]      peer_id     送信先ピア識別子。\n
     *                              N:1 モード: 有効なピア ID (`POTR_PEER_NA` / `POTR_PEER_ALL` 以外) を指定します。\n
     *                              N:1 モード: POTR_PEER_ALL を指定すると全接続ピアへ一斉送信します。\n
     *                              N:1 モード: POTR_PEER_NA を指定すると POTR_ERROR を返します。\n
     *                              1:1 モードおよびその他の通信種別: POTR_PEER_NA または POTR_PEER_ALL を指定します (通常は POTR_PEER_NA を使用)。
     *  @param[in]      data        送信するメッセージへのポインタ。
     *  @param[in]      len         送信するメッセージのバイト数。
     *  @param[in]      flags       送信オプションフラグ。以下のフラグを論理和で組み合わせて指定します。
     *                              0 を指定すると非圧縮・ノンブロッキング送信になります。
     *
     *                              | フラグ                | 説明                                     |
     *                              | --------------------- | ---------------------------------------- |
     *                              | `POTR_SEND_COMPRESS`  | メッセージを圧縮して送信します。         |
     *                              | `POTR_SEND_BLOCKING`  | ブロッキング送信を行います。             |
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  @details
     *  通信種別に応じて以下の宛先へ UDP パケットを送信します。
     *
     *  | 通信種別              | 送信先                                     |
     *  | --------------------- | ------------------------------------------ |
     *  | POTR_TYPE_UNICAST     | 接続相手の dst_port 宛にユニキャスト送信   |
     *  | POTR_TYPE_MULTICAST   | multicast_group:dst_port へ送信            |
     *  | POTR_TYPE_BROADCAST   | broadcast_addr:dst_port へ送信             |
     *  | POTR_TYPE_UNICAST_BIDIR (N:1) | peer_id で指定したピアへ送信       |
     *
     *  compress に `POTR_SEND_COMPRESS` を指定した場合、内部で圧縮処理を行ってから送信します。\n
     *  圧縮後のサイズが元のサイズ以上になった場合は、自動的に非圧縮で送信します。\n
     *  受信側の PotrRecvCallback には、解凍済みの元メッセージが渡されます。\n
     *  送受信ともにフラグメント化と組み合わせて使用できます。
     *
     *  @par            ノンブロッキング送信 (flags に POTR_SEND_BLOCKING を指定しない場合)
     *  メッセージを内部送信キューに登録して即座に返ります。\n
     *  実際の sendto はバックグラウンド送信スレッドが非同期に実行します。\n
     *  キューが満杯の場合は空きが生じるまで待機してからメッセージを登録し、登録後に返ります。
     *
     *  @par            ブロッキング送信 (flags に POTR_SEND_BLOCKING を指定した場合)
     *  呼び出し前に滞留しているノンブロッキング送信のメッセージが
     *  すべて sendto 完了するまで待機します。\n
     *  その後、本呼び出しのメッセージをキューを通じて sendto して返ります。\n
     *  本関数が返った時点で、自身のメッセージの sendto は完了しています。
     *
     *  @note
     *  圧縮フォーマットには raw DEFLATE (RFC 1951) を使用します。\n
     *  Linux (zlib) と Windows (Compression API MSZIP|COMPRESS_RAW) は
     *  同一フォーマットを出力するため、クロスプラットフォーム通信に対応します。\n
     *  圧縮効果がない場合 (圧縮後サイズ >= 元サイズ) は、アプリケーションへの通知なしに
     *  内部で非圧縮に切り替えて送信します。送受信のデータ内容に影響はありません。
     *
     *  @par            スレッド セーフティ
     *  本関数はスレッドセーフではありません。\n
     *  同一ハンドルへの並行呼び出しは未定義動作です。\n
     *  送信は 1 スレッドから行ってください。
     *
     *  @warning        handle が NULL の場合は失敗を返します。\n
     *                  data が NULL の場合は失敗を返します。\n
     *                  len が 0 の場合は失敗を返します。\n
     *                  len が POTR_MAX_MESSAGE_SIZE を超える場合は失敗を返します。\n
     *                  送信スレッドが停止している場合 (potrCloseService 呼び出し後など) は失敗を返します。\n
     *                  N:1 モードで peer_id = POTR_PEER_NA (0) を指定した場合は失敗を返します。\n
     *                  `unicast_bidir` で CONNECTED 前に呼び出した場合は\n
     *                  POTR_ERROR_DISCONNECTED (1) を返します。\n
     *                  N:1 モードでは未接続 peer への送信、および `POTR_PEER_ALL` 指定時に\n
     *                  接続済み peer が 0 件のとき POTR_ERROR_DISCONNECTED (1) を返します。\n
     *                  TCP 通信種別 (`POTR_TYPE_TCP` / `POTR_TYPE_TCP_BIDIR`) では、\n
     *                  物理 TCP 接続済みでも CONNECTED 前または全 path 切断中は\n
     *                  POTR_ERROR_DISCONNECTED (1) を返します。
     *******************************************************************************
     */
    POTR_EXPORT extern int POTR_API potrSend(PotrHandle handle, PotrPeerId peer_id, const void *data, size_t len,
                                             int flags);

    /**
     *******************************************************************************
     *  @brief          指定ピアを切断します (N:1 モード専用)。
     *  @param[in]      handle      potrOpenService() で取得したセッションハンドル。
     *  @param[in]      peer_id     切断するピアの識別子 (POTR_PEER_NA および POTR_PEER_ALL 以外)。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  @details
     *  指定したピアへ FIN パケットを送信し、ピアのリソースを解放します。\n
     *  切断完了後に POTR_EVENT_DISCONNECTED コールバックが発火します。\n
     *  N:1 モード (unicast_bidir かつ src 情報省略) 専用です。\n
     *  1:1 モードおよびその他の通信種別では POTR_ERROR を返します。
     *
     *  @par            スレッド セーフティ
     *  本関数はスレッドセーフです。\n
     *  内部で peers_mutex により排他制御されるため、複数スレッドから並行して呼び出せます。\n
     *  ただし PotrRecvCallback の内部から本関数を呼び出すとデッドロックが発生します。\n
     *  コールバック内からの呼び出しは避けてください。
     *
     *  @warning        handle が NULL の場合は失敗を返します。\n
     *                  peer_id = POTR_PEER_NA または POTR_PEER_ALL の場合は失敗を返します。\n
     *                  指定した peer_id が存在しない場合は失敗を返します。\n
     *                  1:1 モードまたは N:1 モード以外で呼び出した場合は失敗を返します。
     *******************************************************************************
     */
    POTR_EXPORT extern int POTR_API potrDisconnectPeer(PotrHandle handle, PotrPeerId peer_id);

    /**
     *******************************************************************************
     *  @brief          サービスを閉じます。
     *  @param[in]      handle  potrOpenService() で取得したセッションハンドル。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  @details
     *  受信スレッドを停止し、ソケットをクローズしてリソースを解放します。\n
     *  マルチキャストの場合はグループから離脱します。\n
     *  本関数呼び出し後、handle は無効となります。
     *
     *  @note
     *  本関数呼び出し時、POTR_EVENT_DISCONNECTED コールバックは発火しません。\n
     *  アプリケーション自身が明示的にサービスを閉じる操作は、接続状態変化イベントとして通知しない設計です。\n
     *  UDP 通信種別の送信者 (POTR_ROLE_SENDER) が本関数を呼び出した場合は、POTR_FLAG_FIN パケットが送信されます。\n
     *  TCP 通信種別 (POTR_TYPE_TCP / POTR_TYPE_TCP_BIDIR) では、送信キューの drain 完了後に
     *  protocol-level の POTR_FLAG_FIN / POTR_FLAG_FIN_ACK を交換してからソケットを閉じます。\n
     *  TCP close 待機は global 設定 tcp_close_timeout_ms の範囲で行い、タイムアウト時は強制 close して
     *  POTR_ERROR を返します。\n
     *  いずれの場合も、相手側では POTR_EVENT_DISCONNECTED が発火します。
     *
     *  @par            スレッド セーフティ
     *  本関数はスレッドセーフではありません。\n
     *  同一ハンドルに対して他の porter API と並行して呼び出さないでください。\n
     *  本関数を呼び出す前に、同一ハンドルへのすべての potrSend() が完了していることを確認してください。
     *
     *  @warning        handle が NULL の場合は失敗を返します。
     *******************************************************************************
     */
    POTR_EXPORT extern int POTR_API potrCloseService(PotrHandle handle);

    /**
     *******************************************************************************
     *  @brief          porter 内部ロガーハンドルを返します。
     *  @return         com_util_tracer_t ハンドル。
     *
     *  @details
     *  porter ライブラリが内部で使用する com_util_tracer_t ハンドルを返します。\n
     *  本関数は potrOpenService() の前に呼び出すことができます。\n
     *  取得したハンドルに対して com_util_tracer_set_stderr_level() と
     *  com_util_tracer_start() を呼び出すことで、stderr へのトレース出力を有効化できます。
     *
     *  @par            stderr 出力を有効にする例
     *  @code{.c}
        com_util_tracer_t *tracer = potrGetTracer();
        com_util_tracer_set_stderr_level(tracer, COM_UTIL_TRACE_LEVEL_INFO);
        com_util_tracer_start(tracer);
     *  @endcode
     *
     *  @par            ログフォーマット (stderr)
     *  @code
        YYYY-MM-DD HH:MM:SS.mmm L [file.c:line] message
     *  @endcode
     *  タイムスタンプは UTC。L はレベル文字 (C/E/W/I/V)。
     *
     *  @par            スレッド セーフティ
     *  本関数はスレッドセーフです。
     *******************************************************************************
     */
    POTR_EXPORT com_util_tracer_t * POTR_API potrGetTracer(void);

    /**
     *******************************************************************************
     *  @brief          設定ファイルから指定サービスの通信種別を取得します。
     *  @param[in]      config_path 設定ファイルのパス。
     *  @param[in]      service_id  照会するサービスの ID。
     *  @param[out]     type        成功時に通信種別 (PotrType) を格納するポインタ。
     *  @return         成功時は POTR_SUCCESS、失敗時は POTR_ERROR を返します。
     *
     *  @details
     *  設定ファイルを解析して指定サービスの通信種別を返します。\n
     *  potrOpenService() の前に呼び出すことで、ロール・コールバックの要否を
     *  アプリケーション側で判断できます。\n
     *  本関数はソケットの作成や通信スレッドの起動を行いません。
     *
     *  @par            使用例
     *  @code{.c}
        PotrType type;
        if (potrGetServiceType("porter-services.conf", 1031, &type) == POTR_SUCCESS) {
            if (type == POTR_TYPE_UNICAST_BIDIR) {
                // unicast_bidir: コールバックが必須
                potrOpenService("porter-services.conf", 1031,
                                POTR_ROLE_SENDER, on_recv, &handle);
            }
        }
     *  @endcode
     *
     *  @par            スレッド セーフティ
     *  本関数はスレッドセーフです。\n
     *  グローバルな共有状態にアクセスしないため、複数スレッドから並行して呼び出せます。
     *
     *  @warning        config_path または type が NULL の場合は失敗を返します。\n
     *                  指定した service_id が設定ファイルに存在しない場合は失敗を返します。
     *******************************************************************************
     */
    POTR_EXPORT extern int POTR_API potrGetServiceType(const char *config_path, int64_t service_id, PotrType *type);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PORTER_H */
