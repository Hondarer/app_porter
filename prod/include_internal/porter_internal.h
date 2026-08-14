/**
 *******************************************************************************
 *  @file           porter_internal.h
 *  @brief          porter ライブラリの公開 API と内部 API をまとめて取り込みます。
 *  @author         Tetsuo Honda
 *  @date           2026/05/21
 *  @version        1.0.0
 *
 *  porter ライブラリの内部ヘッダーを 1 つにまとめたヘッダーです。\n
 *  利用者は `#include <porter_internal.h>` で本ライブラリの全公開 + 全内部 API にアクセスできます。
 *
 *  アンブレラ ヘッダーは利便性と引き換えにコンパイル時間がかかります。\n
 *  個別ヘッダーを利用するか、アンブレラ ヘッダーを利用するかは利用者にて選択してください。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *  @hideincludedbygraph
 *
 *******************************************************************************
 */

/* NOTE: このヘッダーは多数のソース ファイルから参照されるため、            */
/*       @hideincludedbygraph によって "Included by" グラフを無効にします。 */

#ifndef PORTER_INTERNAL_H
#define PORTER_INTERNAL_H

#include <porter.h> /* 内部 API で公開定数、公開型、公開関数などに依存している可能性を考慮 */

#include <porter/potr_context.h>
#include <porter/potr_path_event.h>
#include <porter/potr_peer_table.h>

#include <porter/infra/potr_result.h>
#include <porter/infra/potr_send_queue.h>
#include <porter/infra/potr_trace.h>

#include <porter/protocol/config.h>
#include <porter/protocol/config_parse_common.h>
#include <porter/protocol/config_parse_kv_common.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/seqnum.h>
#include <porter/protocol/window.h>

#include <porter/thread/potr_connect_thread.h>
#include <porter/thread/potr_connected_threads.h>
#include <porter/thread/potr_health_thread.h>
#include <porter/thread/potr_recv_thread.h>
#include <porter/thread/potr_send_thread.h>

#endif /* PORTER_INTERNAL_H */
