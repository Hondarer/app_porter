/**
 *******************************************************************************
 *  @file           porter_spec.h
 *  @brief          動的リンク用 porter ライブラリの API を公開します。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  このライブラリは UDP/IP を用いたプラットフォーム間通信を提供します。\n
 *  Linux / Windows クロスプラットフォーム対応の DLL として提供されます。
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

#ifndef PORTER_SPEC_H
#define PORTER_SPEC_H

#include <porter/porter_result.h>
#include <porter/porter_type.h>
#include <porter/porter_export.h>
#include <cplat/trace/tracer.h>

/**
 *  @ingroup        PORTER_PUBLIC_API
 *  @{
 */

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

    /**
     *  @brief          設定構造体から指定サービスを開きます。
     *  @param[in]      global      グローバル設定構造体へのポインター。
     *  @param[in]      service     サービス定義構造体へのポインター。
     *  @param[in]      role        役割種別。POTR_ROLE_SENDER または POTR_ROLE_RECEIVER。
     *  @param[in]      callback    イベント発生時に呼び出されるコールバック関数 (potr_recv_fn)。
     *                              POTR_ROLE_RECEIVER の場合は必須。データ受信・接続検知・切断検知を受け取る。
     *                              POTR_ROLE_SENDER の場合は通常 NULL を指定すること。\n
     *                              ただし POTR_TYPE_TCP_BIDIR および POTR_TYPE_UNICAST_BIDIR では
     *                              SENDER にもコールバックが必須。これらの種別では POTR_ROLE_SENDER でも
     *                              callback が NULL の場合は失敗を返します。
     *  @param[out]     handle      成功時にセッション ハンドルを格納するポインター。
     *  @retval         POTR_OK                    サービスを開きました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  引数 (NULL、未定義の役割、または役割とコールバックの組み合わせ) が不正です。
     *  @retval         POTR_ERR_INVALID_CONFIG    設定構造体の値、または証明書と鍵の形式・組み合わせが不正です。誤りは ERROR のトレースへ出力します。
     *  @retval         POTR_ERR_UNSUPPORTED       通信種別が未対応です。
     *  @retval         POTR_ERR_OUT_OF_MEMORY     内部バッファーを確保できません。
     *  @retval         POTR_ERR_IO                アドレス解決、ソケット操作、または認証ファイルの読み取りに失敗しました。
     *  @retval         POTR_ERR_UNKNOWN           スレッド生成などの分類不能な内部処理に失敗しました。
     *
     *  設定構造体からサービス定義を取得し、UDP ソケットを初期化します。\n
     *  role と callback の組み合わせが不正な場合は POTR_ERR_INVALID_ARGUMENT を返します。\n
     *  role と設定の IP アドレスが不整合で bind に失敗した場合は POTR_ERR_IO を返します。\n
     *  通信種別・役割に応じて以下のソケット設定を行います。
     *
     *  Table: 通信種別と役割ごとのソケット設定
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
        @code{.c}
        void on_recv(int64_t service_id, potr_peer_id peer_id,
                     potr_event event, const void *data, size_t len) {
            (void)peer_id;  // 1:1 モードでは常に POTR_PEER_NA
            if (event == POTR_EVENT_CONNECTED)
                printf("service %" PRId64 ": connected\n", service_id);
            else if (event == POTR_EVENT_DISCONNECTED)
                printf("service %" PRId64 ": disconnected\n", service_id);
            else
                printf("service %" PRId64 ": received %zu bytes\n", service_id, len);
        }

        potr_global_config global = {0};
        global.window_size        = 16;
        global.max_payload        = 1400;
        global.udp_health_interval_ms = 3000;
        global.udp_health_timeout_ms  = 10000;
        global.tcp_health_interval_ms = 10000;
        global.tcp_health_timeout_ms  = 31000;
        global.max_message_size   = 65535;
        global.send_queue_depth   = 64;

        potr_service_def service = {0};
        service.service_id = 1001;
        service.type       = POTR_TYPE_UNICAST;
        service.dst_port   = 49001;
        cplat_strcpy(service.src_addr[0], POTR_MAX_ADDR_LEN, "127.0.0.1");
        cplat_strcpy(service.dst_addr[0], POTR_MAX_ADDR_LEN, "127.0.0.1");

        potr_context * handle;
        if (potr_service_open(&global, &service,
                            POTR_ROLE_RECEIVER, on_recv, &handle) == POTR_OK) {
            // 受信待機中 (受信スレッドが動作)
            potr_service_close(handle);
        }
        @endcode
     *
     *  @par            使用例 (送信者)
        @code{.c}
        potr_global_config global = {0};
        global.window_size        = 16;
        global.max_payload        = 1400;
        global.udp_health_interval_ms = 3000;
        global.udp_health_timeout_ms  = 10000;
        global.tcp_health_interval_ms = 10000;
        global.tcp_health_timeout_ms  = 31000;
        global.max_message_size   = 65535;
        global.send_queue_depth   = 64;

        potr_service_def service = {0};
        service.service_id = 1001;
        service.type       = POTR_TYPE_UNICAST;
        service.dst_port   = 49001;
        cplat_strcpy(service.src_addr[0], POTR_MAX_ADDR_LEN, "127.0.0.1");
        cplat_strcpy(service.dst_addr[0], POTR_MAX_ADDR_LEN, "127.0.0.1");

        potr_context * handle;
        if (potr_service_open(&global, &service,
                            POTR_ROLE_SENDER, NULL, &handle) == POTR_OK) {
            potr_service_send(handle, POTR_PEER_NA, "hello", 5, 0);
            potr_service_close(handle);
        }
        @endcode
     *
     *  @par            スレッド セーフ
     *  本関数はスレッド セーフです。
     */
    POTR_EXPORT extern int POTR_API potr_service_open(const potr_global_config *global, const potr_service_def *service,
                                                      potr_role role, potr_recv_fn callback, potr_context **handle);

    /**
     *  @brief          設定ファイルから指定サービスを開きます。
     *  @param[in]      config_path 設定ファイルのパス。
     *  @param[in]      service_id  開くサービスの ID。
     *  @param[in]      role        役割種別。POTR_ROLE_SENDER または POTR_ROLE_RECEIVER。
     *  @param[in]      callback    イベント発生時に呼び出されるコールバック関数 (potr_recv_fn)。
     *                              POTR_ROLE_RECEIVER の場合は必須。データ受信・接続検知・切断検知を受け取る。
     *                              POTR_ROLE_SENDER の場合は通常 NULL を指定すること。\n
     *                              ただし POTR_TYPE_TCP_BIDIR および POTR_TYPE_UNICAST_BIDIR では
     *                              SENDER にもコールバックが必須。これらの種別では POTR_ROLE_SENDER でも
     *                              callback が NULL の場合は失敗を返します。
     *  @param[out]     handle      成功時にセッション ハンドルを格納するポインター。
     *  @retval         POTR_OK                    サービスを開きました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  引数 (NULL、未定義の役割、または役割とコールバックの組み合わせ) が不正です。
     *  @retval         POTR_ERR_INVALID_CONFIG    設定ファイルの内容、または証明書と鍵の形式・組み合わせが不正です。誤りは ERROR のトレースへ出力します。
     *  @retval         POTR_ERR_UNSUPPORTED       通信種別が未対応です。
     *  @retval         POTR_ERR_NOT_FOUND         指定したサービス ID がありません。
     *  @retval         POTR_ERR_OUT_OF_MEMORY     メモリまたは内部バッファーを確保できません。
     *  @retval         POTR_ERR_IO                設定ファイル、認証ファイル、アドレス解決、またはソケット操作に失敗しました。
     *  @retval         POTR_ERR_UNKNOWN           スレッド生成などの分類不能な内部処理に失敗しました。
     *
     *  設定ファイルを解析してサービス定義を取得し、potr_service_open() を呼び出します。\n
     *  role と callback の組み合わせが不正な場合は POTR_ERR_INVALID_ARGUMENT を返します。\n
     *  role と設定ファイルの IP アドレスが不整合で bind に失敗した場合は POTR_ERR_IO を返します。\n
     *  通信種別・役割に応じて以下のソケット設定を行います。
     *
     *  Table: 設定ファイル指定時の通信種別と役割ごとのソケット設定
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
        @code{.c}
        void on_recv(int64_t service_id, potr_peer_id peer_id,
                     potr_event event, const void *data, size_t len) {
            (void)peer_id;  // 1:1 モードでは常に POTR_PEER_NA
            if (event == POTR_EVENT_CONNECTED)
                printf("service %" PRId64 ": connected\n", service_id);
            else if (event == POTR_EVENT_DISCONNECTED)
                printf("service %" PRId64 ": disconnected\n", service_id);
            else
                printf("service %" PRId64 ": received %zu bytes\n", service_id, len);
        }

        potr_context * handle;
        if (potr_service_open_from_config("porter-services.jsonc", 1001,
                                      POTR_ROLE_RECEIVER, on_recv, &handle) == POTR_OK) {
            // 受信待機中 (受信スレッドが動作)
            potr_service_close(handle);
        }
        @endcode
     *
     *  @par            使用例 (送信者)
        @code{.c}
        potr_context * handle;
        if (potr_service_open_from_config("porter-services.jsonc", 1001,
                                      POTR_ROLE_SENDER, NULL, &handle) == POTR_OK) {
            potr_service_send(handle, POTR_PEER_NA, "hello", 5, 0);
            potr_service_close(handle);
        }
        @endcode
     *
     *  @par            スレッド セーフ
     *  本関数はスレッド セーフです。
     */
    POTR_EXPORT extern int POTR_API potr_service_open_from_config(const char *config_path, int64_t service_id,
                                                                  potr_role role, potr_recv_fn callback,
                                                                  potr_context **handle);

    /**
     *  @brief          メッセージを送信します。
     *  @param[in]      handle      potr_service_open() で取得したセッション ハンドル。
     *  @param[in]      peer_id     送信先ピア識別子。\n
     *                              N:1 モード: 有効なピア ID (`POTR_PEER_NA` / `POTR_PEER_ALL` 以外) を指定します。\n
     *                              N:1 モード: POTR_PEER_ALL を指定すると全接続ピアへ一斉送信します。\n
     *                              N:1 モード: POTR_PEER_NA を指定すると POTR_ERR_INVALID_ARGUMENT を返します。\n
     *                              1:1 モードおよびその他の通信種別: POTR_PEER_NA または POTR_PEER_ALL を指定します (通常は POTR_PEER_NA を使用)。
     *  @param[in]      data        送信するメッセージへのポインター。
     *  @param[in]      len         送信するメッセージのバイト数。
     *  @param[in]      flags       送信オプション フラグ。以下のフラグを論理和で組み合わせて指定します。
     *                              0 を指定すると非圧縮・非ブロッキング送信になります。
     *
     *  Table: 送信オプション フラグ
     *
     *                              | フラグ                | 説明                                     |
     *                              | --------------------- | ---------------------------------------- |
     *                              | `POTR_SEND_COMPRESS`  | メッセージを圧縮して送信します。         |
     *                              | `POTR_SEND_BLOCKING`  | ブロッキング送信を行います。             |
     *
     *  @retval         POTR_ERR_INVALID_HANDLE ハンドルが未登録または終了済みです。
     *  @retval         POTR_OK                 メッセージを送信キューへ登録しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  引数または N:1 モードの peer_id が不正です。
     *  @retval         POTR_ERR_UNSUPPORTED    片方向の受信専用サービスへ送信を要求しました。
     *  @retval         POTR_ERR_DISCONNECTED   送信先が論理 CONNECTED 前または切断中です。
     *                                          ブロッキング送信では、TCP のどの path にも書き込めなかった場合も返します。
     *  @retval         POTR_ERR_NOT_FOUND       N:1 モードで指定したピアが存在しません。
     *  @retval         POTR_ERR_FULL            POTR_SEND_BLOCKING を指定しない送信で、送信キューにメッセージの全フラグメントを
     *                                           登録できる空きがありません。N:1 モードでは、宛先のピアがフロー制御で
     *                                           抑制中の場合も返します。メッセージは 1 つも登録していません。
     *  @retval         POTR_ERR_OUT_OF_MEMORY   全ピア送信用または圧縮用の一時領域を確保できないか、通知継続不能です。
     *  @retval         POTR_ERR_CANCELED        終了処理によって送信を中止しました。
     *  @retval         POTR_ERR_UNKNOWN         圧縮などの分類不能な内部処理に失敗しました。
     *
     *  通信種別に応じて以下の宛先へ UDP パケットを送信します。
     *
     *  Table: 通信種別ごとの UDP パケット送信先
     *
     *  | 通信種別              | 送信先                                     |
     *  | --------------------- | ------------------------------------------ |
     *  | POTR_TYPE_UNICAST     | 接続相手の dst_port 宛にユニキャスト送信   |
     *  | POTR_TYPE_MULTICAST   | multicast_group:dst_port へ送信            |
     *  | POTR_TYPE_BROADCAST   | broadcast_addr:dst_port へ送信             |
     *  | POTR_TYPE_UNICAST_BIDIR (N:1) | peer_id で指定したピアへ送信       |
     *
     *  flags に `POTR_SEND_COMPRESS` を指定した場合、内部で圧縮処理を行ってから送信します。\n
     *  圧縮後のサイズが元のサイズ以上になった場合は、自動的に非圧縮で送信します。\n
     *  受信側の potr_recv_fn には、展開済みの元メッセージが渡されます。\n
     *  送受信ともにフラグメント化と組み合わせて使用できます。
     *
     *  @par            非ブロッキング送信 (flags に POTR_SEND_BLOCKING を指定しない場合)
     *  メッセージを内部送信キューに登録して即座に返ります。\n
     *  実際の sendto はバックグラウンド送信スレッドが非同期に実行します。\n
     *  キューにメッセージの全フラグメントを登録できる空きがない場合は、待機せず、1 つも登録せずに
     *  POTR_ERR_FULL を返します。POTR_PEER_ALL の送信は、全ピアの分を登録できる空きがなければ、
     *  どのピア宛も登録しません。\n
     *  POTR_ERR_FULL を受けた場合の再送信は利用側が判断します。porter は受け付けなかったメッセージを保持しません。\n
     *  フラグメントの数が send_queue_depth を超えるメッセージは、常に POTR_ERR_FULL になります。\n
     *  相手がフロー制御で抑制を要求している間は、送信スレッドが新しい DATA の送出を止めるため、
     *  キューの空きが増えません。N:1 モードで宛先のピアが抑制中の場合は、キューの空きにかかわらず
     *  POTR_ERR_FULL を返します。
     *
     *  @par            ブロッキング送信 (flags に POTR_SEND_BLOCKING を指定した場合)
     *  本呼び出しのメッセージをキューへ登録し、その最後のフラグメントの sendto が完了するまで待機します。\n
     *  キューに空きがない場合は、空きが生じるまで待機してから登録します。\n
     *  相手がフロー制御で抑制を要求している間は、抑制の解除、要求の期限の経過、または
     *  potr_service_close() の開始まで sendto が行われないため、その間待機します。\n
     *  送信スレッドは登録順に sendto するため、本関数が返った時点で、自身のメッセージと、
     *  それ以前に登録されたメッセージの sendto は完了しています。\n
     *  自身のメッセージは、直前に登録された他のメッセージと同じパケットにまとめて送信される場合があります。\n
     *  TCP で自身のメッセージをどの path にも書き込めなかった場合は POTR_ERR_DISCONNECTED を返します。
     *  送信できなかったメッセージを成功として返さないことを優先するため、本関数が返るまでに
     *  後続のメッセージの書き込みが失敗した場合も、POTR_ERR_DISCONNECTED を返すことがあります。
     *
     *  @note           圧縮フォーマットには raw DEFLATE (RFC 1951) を使用します。\n
     *                  Linux と Windows はともに cplat を通じて app/zlib を使用し、
     *                  同一フォーマットを出力するため、クロスプラットフォーム通信に対応します。\n
     *                  圧縮効果がない場合 (圧縮後サイズ >= 元サイズ) は、アプリケーションへの通知なしに
     *                  内部で非圧縮に切り替えて送信します。送受信のデータ内容に影響はありません。
     *
     *  @par            スレッド セーフ
     *  本関数は条件付きスレッド セーフです。\n
     *  同一 @p handle に対して、複数のスレッドから同時に呼び出せます。
     *  1 つのメッセージのフラグメントは、他のメッセージのフラグメントと交互に並ばずに送信されます。\n
     *  各スレッド内の送信順序は保たれ、スレッド間の順序はキューへの登録順になります。\n
     *  potr_service_close() と同時に呼び出せます。受付停止後の呼び出しは POTR_ERR_CANCELED を返します。\n
     *  通信種別を問わず、送信可能なサービスでは同一ハンドルのコールバック内からも呼び出せます。
     */
    POTR_EXPORT extern int POTR_API potr_service_send(potr_context *handle, potr_peer_id peer_id, const void *data,
                                                      size_t len, int flags);

    /**
     *  @brief          指定ピアを切断します (N:1 モード専用)。
     *  @param[in]      handle      potr_service_open() で取得したセッション ハンドル。
     *  @param[in]      peer_id     切断するピアの識別子 (POTR_PEER_NA および POTR_PEER_ALL 以外)。
     *  @retval         POTR_ERR_OUT_OF_MEMORY 通知領域の確保失敗でサービスが停止しています。
     *  @retval         POTR_ERR_CANCELED 正常終了の処理中のため受け付けません。
     *  @retval         POTR_ERR_INVALID_HANDLE ハンドルが未登録または終了済みです。
     *  @retval         POTR_OK                    指定ピアを切断しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  handle が NULL、または peer_id に POTR_PEER_NA / POTR_PEER_ALL を指定しました。
     *  @retval         POTR_ERR_NOT_FOUND         指定した peer_id のピアが存在しません。
     *  @retval         POTR_ERR_UNSUPPORTED       N:1 モード以外のサービスで呼び出しました。
     *
     *  指定したピアへ FIN パケットを送信し、ピアのリソースを解放します。\n
     *  切断完了後に POTR_EVENT_DISCONNECTED コールバックが発火します。\n
     *  N:1 モード (unicast_bidir かつ src 情報省略) 専用です。\n
     *  1:1 モードおよびその他の通信種別では POTR_ERR_UNSUPPORTED を返します。
     *
     *  @par            スレッド セーフ
     *  本関数は条件付きスレッド セーフです。\n
     *  同一 @p handle に対して、他のスレッドから potr_service_send() と本関数を同時に呼び出せます。\n
     *  potr_service_close() と同時に呼び出せます。受付停止後は POTR_ERR_CANCELED を返します。
     *
     *  コールバック内からも呼び出せます。切断通知は通知スレッドで後から実行され、本関数は通知完了を待ちません。
     */
    POTR_EXPORT extern int POTR_API potr_peer_disconnect(potr_context *handle, potr_peer_id peer_id);

    /**
     *  @brief          サービスを閉じます。
     *  @param[in]      handle  potr_service_open() で取得したセッション ハンドル。
     *  @retval         POTR_ERR_CANCELED 正常終了の処理中のため受け付けません。
     *  @retval         POTR_ERR_INVALID_HANDLE ハンドルが未登録または終了済みです。
     *  @retval         POTR_OK                    サービスを閉じました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  handle が NULL です。
     *  @retval         POTR_ERR_DISCONNECTED      送信済みデータがある状態で相手 FIN 未受理のまま全 TCP パスが切断されているか、
     *                                             相手端の正常な FIN 受理後に DATA を送信しました。
     *  @retval         POTR_ERR_TIMEOUT           TCP close の絶対期限 (tcp_close_timeout_ms) を超過しました。
     *  @retval         POTR_ERR_IO                TCP FIN の送信に失敗しました。
     *  @retval         POTR_ERR_UNSUPPORTED       同一サービスの受信コールバック内から呼び出しました。handle は有効なままです。
     *  @retval         POTR_ERR_UNKNOWN           暗号化または同期などの分類不能な内部処理に失敗しました。
     *
     *  受信スレッドを停止し、ソケットをクローズしてリソースを解放します。\n
     *  マルチキャストの場合はグループから離脱します。\n
     *  資源回収後、handle は無効となり、以後の操作は POTR_ERR_INVALID_HANDLE を返します。
     *
     *  @note
     *
     *  本関数呼び出し時、POTR_EVENT_DISCONNECTED コールバックは発火しません。\n
     *  アプリケーション自身が明示的にサービスを閉じる操作は、接続状態変化イベントとして通知しない設計です。\n
     *  UDP 通信種別の送信者 (POTR_ROLE_SENDER) が本関数を呼び出した場合は、POTR_FLAG_FIN パケットが送信されます。\n
     *  TCP 通信種別 (POTR_TYPE_TCP / POTR_TYPE_TCP_BIDIR) では、終了開始時に global 設定 tcp_close_timeout_ms
     *  から 1 つの絶対期限を決定し、受付済み API の通信待機の回収、送信キューの排出、protocol-level の
     *  POTR_FLAG_FIN 送信、および POTR_FLAG_FIN_ACK 待機で同じ期限を共有します (段階移行や再試行で期限を延長しません)。\n
     *  すでに相手端の正常な POTR_FLAG_FIN を受理済みの場合は FIN 送信と FIN_ACK 待機を行わず、
     *  相手 FIN 受理後にソケットへ書き込んだ DATA がなければ POTR_OK、あれば POTR_ERR_DISCONNECTED を返します。\n
     *  待機中に全 TCP パスが切断された場合はタイムアウトを待たずに復帰して
     *  (相手 FIN 受理済みなら受理後の DATA 送信がなければ POTR_OK・あれば POTR_ERR_DISCONNECTED、
     *  未受理なら現セッションの送信済みデータがなければ POTR_OK・あれば POTR_ERR_DISCONNECTED)、
     *  いずれかの段階で期限を超過した場合は、送信キューの中断と未送信データの破棄、通信待機の解除を行い、
     *  参照とスレッドをすべて回収してから POTR_ERR_TIMEOUT を返します。\n
     *  tcp_close_timeout_ms が 0 の場合も、同じ期限で各待機を打ち切り、使用中の資源を回収前に解放しません。\n
     *  いずれの場合も、相手側では POTR_EVENT_DISCONNECTED が発火します。
     *
     *  @attention      受付停止前の API と登録済み通知の完了を待ってから通信資源を解放します。\n
     *                  同一ハンドルのコールバック内では、終了要求を設定せず POTR_ERR_UNSUPPORTED を返します。
     *
     *  @par            スレッド セーフ
     *  本関数は条件付きスレッド セーフです。\n
     *  異なる @p handle に対する操作は同時に実行できます。\n
     *  同一 @p handle の send、disconnect、close と並行して呼び出せます。\n
     *  進行中の後続 close は POTR_ERR_CANCELED、完了後は POTR_ERR_INVALID_HANDLE を返します。
     */
    POTR_EXPORT extern int POTR_API potr_service_close(potr_context *handle);

    /**
     *  @brief          porter 内部トレーサー ハンドルを返します。
     *  @return         成功時は共有ハンドル、生成できない場合は NULL を返します。
     *
     *  porter ライブラリが内部で使用する cplat_tracer ハンドルを返します。\n
     *  本関数は potr_service_open() の前に呼び出すことができます。\n
     *  生成に失敗した後の呼び出しでは、再度の生成を試みます。成功後は同じハンドルを返します。
     *  利用側はハンドルを破棄しません。NULL の場合は設定と開始を行わず、必要なら後で再取得してください。\n
     *  取得したハンドルに対して cplat_tracer_set_stderr_level() と
     *  cplat_tracer_start() を呼び出すことで、stderr へのトレース出力を有効化できます。
     *
     *  @note           出力先の詳細度は、すべて CPLAT_TRACE_LEVEL_NONE (無効) が初期値です。\n
     *                  cplat_tracer_start() だけを呼び出しても、どこへも出力されません。\n
     *                  開始する前に、使用する出力先の詳細度を設定してください。
     *                  標準エラー出力は cplat_tracer_set_stderr_level()、ファイルは
     *                  cplat_tracer_set_file_level()、OS のログ基盤は cplat_tracer_set_os_level() です。\n
     *                  porter 自身は出力先を設定しません。出力先の選択は利用側の判断であり、
     *                  ライブラリが利用側の実行ファイルの配置先へログ ファイルを作らないためです。
     *
     *  @par            stderr 出力を有効にする例
        @code{.c}
        cplat_tracer *tracer = potr_tracer_get();
        cplat_tracer_set_stderr_level(tracer, CPLAT_TRACE_LEVEL_INFO);
        cplat_tracer_start(tracer);
        @endcode
     *
     *  @par            ログ フォーマット (stderr)
        @code
        YYYY-MM-DD HH:MM:SS.mmm L [file.c:line] message
        @endcode
     *  タイムスタンプは UTC。L はレベル文字 (C/E/W/I/V)。
     *
     *  @par            スレッド セーフ
     *  同時の初回呼び出しを含め、本関数はスレッド セーフです。
     */
    POTR_EXPORT cplat_tracer *POTR_API potr_tracer_get(void);

    /**
     *  @brief          設定ファイルから指定サービスの通信種別を取得します。
     *  @param[in]      config_path 設定ファイルのパス。
     *  @param[in]      service_id  照会するサービスの ID。
     *  @param[out]     type        成功時に通信種別 (potr_type) を格納するポインター。失敗時は変更しません。
     *  @retval         POTR_OK                    通信種別を取得しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  config_path または type が NULL です。
     *  @retval         POTR_ERR_INVALID_CONFIG    設定ファイルの内容が不正です。誤りは ERROR のトレースへ出力します。
     *  @retval         POTR_ERR_NOT_FOUND         指定したサービス ID がありません。
     *  @retval         POTR_ERR_OUT_OF_MEMORY     メモリを確保できません。
     *  @retval         POTR_ERR_IO                設定ファイルを開けない、または読み取りに失敗しました。
     *
     *  設定ファイルを解析して指定サービスの通信種別を返します。\n
     *  potr_service_open() の前に呼び出すことで、ロール・コールバックの要否を
     *  アプリケーション側で判断できます。\n
     *  本関数はソケットの作成や通信スレッドの起動を行いません。\n
     *  誤りがある場合は ERROR のトレースへ出力し、*type を変更しません。
     *
     *  @par            使用例
        @code{.c}
        potr_type type;
        if (potr_service_get_type("porter-services.jsonc", 1031, &type) == POTR_OK) {
            if (type == POTR_TYPE_UNICAST_BIDIR) {
                // unicast_bidir: コールバックが必須
                potr_service_open_from_config("porter-services.jsonc", 1031,
                                            POTR_ROLE_SENDER, on_recv, &handle);
            }
        }
        @endcode
     *
     *  @par            スレッド セーフ
     *  本関数は条件付きスレッド セーフです。\n
     *  JSONC の解析はスレッドごとに独立した状態を使うため、ほかの解析と並行に実行できます。
     *  ほかの解析には、設定の読み込み・検証・列挙の解析と、同じプロセスで cJSON を直接呼ぶ解析
     *  (struct_meta_json_file_load() を含みます) が含まれます。\n
     *  ロード後に cJSON_InitHooks() を解除・再差し替えしないでください。\n
     *  入力と出力先を共有する場合の同期は、利用側が行います。
     */
    POTR_EXPORT extern int POTR_API potr_service_get_type(const char *config_path, int64_t service_id, potr_type *type);

    /**
     *  @brief          設定ファイル全体を検証し、誤りを報告します。
     *  @param[in]      config_path 設定ファイルのパス (UTF-8)。
     *  @param[in]      options     検証の動作。NULL の場合は、役割に依存する条件をすべての役割で検査し、
     *                              コールバックを呼びません。
     *  @param[out]     issue_count 見つけた誤りの件数を格納するポインター。NULL 可。
     *                              戻り値によらず、関数が復帰する時点までに報告した件数を格納します
     *                              (引数の誤りでは 0)。
     *  @retval         POTR_OK                    誤りはありません。
     *  @retval         POTR_ERR_INVALID_CONFIG    設定に誤りがあります (*issue_count は 1 以上)。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  config_path が NULL、options の roles に未定義のビット、
     *                                             または reserved が 0 でありません (*issue_count は 0)。
     *  @retval         POTR_ERR_IO                設定ファイルを開けない、または読み取りに失敗しました。
     *  @retval         POTR_ERR_OUT_OF_MEMORY     メモリを確保できません。それまでに報告した誤りは有効です。
     *
     *  global とすべてのサービスを検証し、誤りを見つけるたびに options->on_issue を呼びます。\n
     *  構文の誤りは 1 件だけ報告して検証を打ち切ります。読み取りの失敗とメモリ不足は、誤りを報告せずに打ち切ります。\n
     *  それ以外の誤りでは検証を続け、すべての誤りを報告します。前提の値が誤っている項目の間の制約は検査しません。\n
     *  検査は設定ファイルの内容だけで決まるものです。名前解決、ソケットの操作、証明書と鍵のファイルの読み込みは行いません。\n
     *  options->on_service は、ルートの構造の検査が完了し誤りがない場合に、同じ解析結果の各 ID を書かれた順で 1 回通知します。\n
     *  各サービス本文の検査より前に通知し、そのサービスに誤りがあっても通知します。\n
     *  構文またはルートの構造の誤りでは通知せず、global の本文の誤りは通知を妨げません。\n
     *  メモリ不足で打ち切るまでの通知は有効ですが、サービス一覧の完成と検証の完了は保証しません。\n
     *  options->on_auth_file を指定した場合は、証明書方式のサービスの cert_file、key_file、ca_file、crl_file、
     *  pinned_certs の各要素のうち、誤りのないパスを 1 件ずつ渡します。\n
     *  本関数が POTR_OK を返し、roles に POTR_CONFIG_ROLE_ALL を指定した設定は、
     *  potr_service_open_from_config() でも設定の誤りになりません
     *  (名前解決、ソケット、および認証ファイルの存在・読み取り・内容の検査を除きます)。\n
     *  誤りの内容には、設定の値 (鍵とパスフレーズを含みます) を含めません。\n
     *  本関数はトレースへ誤りを出力しません。
     *
     *  @par            使用例
        @code{.c}
        static void on_issue(const potr_config_issue *issue, void *user_data)
        {
            (void)user_data;
            fprintf(stderr, "%s: %s\n", issue->key_path, issue->message);
        }

        potr_config_validate_options options = {0};
        size_t count = 0;
        options.roles = POTR_CONFIG_ROLE_ALL;
        options.on_issue = on_issue;
        int ret = potr_config_validate_file("porter-services.jsonc", &options, &count);
        if (ret == POTR_ERR_INVALID_CONFIG) {
            // 設定の誤りが count 件ある
        } else if (ret != POTR_OK) {
            // 読み取りの失敗、メモリ不足、引数の誤り
        }
        @endcode
     *
     *  @par            スレッド セーフ
     *  本関数は条件付きスレッド セーフです。\n
     *  JSONC の解析はスレッドごとに独立した状態を使うため、ほかの解析と並行に実行できます。
     *  ほかの解析には、設定の読み込み・検証・列挙の解析と、同じプロセスで cJSON を直接呼ぶ解析
     *  (struct_meta_json_file_load() を含みます) が含まれます。\n
     *  ロード後に cJSON_InitHooks() を解除・再差し替えしないでください。\n
     *  入力、出力先、コールバックの user_data を共有する場合の同期は、利用側が行います。\n
     *  コールバックは、本関数を呼んだスレッドで、本関数の復帰の前に呼ばれます。\n
     *  コールバックから porter の関数を呼べます。1 回の本関数の呼び出しの中では、コールバックは同時には呼ばれません。別のスレッドが本関数を同時に呼んだ場合は、それぞれの呼び出しのコールバックが並行して呼ばれます。
     */
    POTR_EXPORT extern int POTR_API potr_config_validate_file(const char *config_path,
                                                              const potr_config_validate_options *options,
                                                              size_t *issue_count);

    /* potr-req: id=POTR-CONFIG-FUNC-007; uuid=a1ee646d-15a7-466f-904e-d1c2d752f69f */
    /**
     *  @brief          設定ファイルに定義されたサービスの ID を列挙します。
     *  @param[in]      config_path 設定ファイルのパス (UTF-8)。
     *  @param[out]     ids         成功時に、ID の配列の先頭を格納するポインター。
     *                              配列は potr_config_service_ids_free() で解放します。
     *                              サービスが 0 件の場合は NULL を格納します。失敗時は NULL を格納します。
     *  @param[out]     count       成功時に ID の件数を格納するポインター。失敗時は 0 を格納します。
     *  @retval         POTR_OK                    列挙しました。
     *  @retval         POTR_ERR_INVALID_ARGUMENT  config_path、ids、count のいずれかが NULL です。
     *  @retval         POTR_ERR_INVALID_CONFIG    構文の誤り、またはルートの構造の誤り
     *                                             (知らないルートのキー、正規でないサービスの ID、
     *                                             ルート直下または services 直下の重複したキー、global または services 自体の型の誤り) があります。
     *  @retval         POTR_ERR_IO                設定ファイルを開けない、または読み取りに失敗しました。
     *  @retval         POTR_ERR_OUT_OF_MEMORY     メモリを確保できません。
     *
     *  設定ファイルを読み、ルートの構造を検証して、services のキーを設定ファイルに書かれた順に返します。\n
     *  global の本文、サービス本文の型と項目は検証しません。本文を含めて検証する場合は potr_config_validate_file() を使います。\n
     *  誤りは ERROR のトレースへ出力します。
     *
     *  @par            スレッド セーフ
     *  本関数は条件付きスレッド セーフです。\n
     *  JSONC の解析はスレッドごとに独立した状態を使うため、ほかの解析と並行に実行できます。
     *  ほかの解析には、設定の読み込み・検証・列挙の解析と、同じプロセスで cJSON を直接呼ぶ解析
     *  (struct_meta_json_file_load() を含みます) が含まれます。\n
     *  ロード後に cJSON_InitHooks() を解除・再差し替えしないでください。\n
     *  入力と出力先を共有する場合の同期は、利用側が行います。
     */
    POTR_EXPORT extern int POTR_API potr_config_list_service_ids(const char *config_path, int64_t **ids,
                                                                 size_t *count);

    /**
     *  @brief          potr_config_list_service_ids() が返した配列を解放します。
     *  @param[in]      ids 解放する配列。NULL の場合は何もしません。
     *
     *  配列は libporter の中で確保するため、利用側の free() ではなく本関数で解放してください
     *  (Windows では、モジュールごとに C ランタイムのヒープが異なる構成があるためです)。
     *
     *  @par            スレッド セーフ
     *  本関数はスレッド セーフです (同じ配列を同時に解放しない場合)。
     */
    POTR_EXPORT extern void POTR_API potr_config_service_ids_free(int64_t *ids);

#ifdef __cplusplus
}
#endif /* __cplusplus */

/** @} */

#endif /* PORTER_SPEC_H */
