/**
 *******************************************************************************
 *  @file           porter_internal.h
 *  @brief          porter ライブラリの内部傘ヘッダー (内部 API ひとまとめ)。
 *  @author         Tetsuo Honda
 *  @date           2026/05/21
 *  @version        1.0.0
 *
 *  porter ライブラリの内部ヘッダーを 1 つにまとめます。\n
 *  ライブラリ内部実装から `<porter_internal.h>` 1 行で全内部 API にアクセスできます。
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef PORTER_INTERNAL_H
#define PORTER_INTERNAL_H

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
