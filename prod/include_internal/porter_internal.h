/**
 *******************************************************************************
 *  @file           porter_internal.h
 *  @brief          porter ライブラリの内部アンブレラ ヘッダー。
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
 *******************************************************************************
 */

#ifndef PORTER_INTERNAL_H
#define PORTER_INTERNAL_H

#include <porter.h> /* 内部 API で公開定数、公開型、公開関数などに依存している可能性を考慮 */

#include <porter/potrContext.h>
#include <porter/potrPathEvent.h>
#include <porter/potrPeerTable.h>

#include <porter/infra/potrPlatform.h>
#include <porter/infra/potrSendQueue.h>
#include <porter/infra/potrTrace.h>

#include <porter/protocol/config.h>
#include <porter/protocol/configParseCommon.h>
#include <porter/protocol/configParseKvCommon.h>
#include <porter/protocol/packet.h>
#include <porter/protocol/seqnum.h>
#include <porter/protocol/window.h>

#include <porter/thread/potrConnectThread.h>
#include <porter/thread/potrConnectedThreads.h>
#include <porter/thread/potrHealthThread.h>
#include <porter/thread/potrRecvThread.h>
#include <porter/thread/potrSendThread.h>

#include <porter/util/potrIpAddr.h>

#endif /* PORTER_INTERNAL_H */
