# 通信シーケンス

通信種別と調べたい処理から、対応するシーケンス図を参照してください。  
API の契約は [API ガイド](api.md)、処理を担当する実装は [アーキテクチャー](architecture.md) を参照してください。

## サービスの開始と終了

- [サービス開始 (送信者)](sequence-lifecycle.md#サービス開始-送信者)
- [サービス開始 (受信者)](sequence-lifecycle.md#サービス開始-受信者)
- [サービス終了 (potr_service_close)](sequence-lifecycle.md#サービス終了-potr_service_close)
- [補足: 接続状態の遷移](sequence-lifecycle.md#補足-接続状態の遷移)

## UDP のデータ転送と再送

- [正常送受信 (非ブロッキング)](sequence-data.md#正常送受信-非ブロッキング)
- [正常送受信 (ブロッキング)](sequence-data.md#正常送受信-ブロッキング)
- [フラグメント化と結合](sequence-data.md#フラグメント化と結合)
- [NACK による再送](sequence-data.md#nack-による再送)
- [リオーダー バッファー (reorder_timeout_ms > 0)](sequence-data.md#reorder-buffer)
- [REJECT による切断と復帰](sequence-data.md#reject-による切断と復帰)
- [RAW モード: ギャップ検出による切断と復帰 (DATA)](sequence-data.md#raw-モード-ギャップ検出による切断と復帰-data)
- [RAW モード: ギャップ検出による切断と復帰 (PING)](sequence-data.md#raw-モード-ギャップ検出による切断と復帰-ping)

## UDP のヘルスチェック

- [ヘルスチェック (正常疎通)](sequence-health.md#ヘルスチェック-正常疎通)
- [ヘルスチェック タイムアウト](sequence-health.md#ヘルスチェック-タイムアウト)

## UDP 双方向通信

- [unicast_bidir 1:1 双方向通信](sequence-bidir.md#unicast_bidir-11-双方向通信)
- [unicast_bidir N:1 サーバーでの接続と送受信](sequence-bidir.md#unicast_bidir-n1-サーバーでの接続と送受信)
- [unicast_bidir N:1 サーバーでの切断](sequence-bidir.md#unicast_bidir-n1-サーバーでの切断)
- [unicast_bidir ヘルスチェック タイムアウトによる切断検知](sequence-bidir.md#unicast_bidir-ヘルスチェック-タイムアウトによる切断検知)

## TCP 通信

- [TCP サービス開始 (SENDER)](sequence-tcp.md#tcp-サービス開始-sender)
- [TCP サービス開始 (RECEIVER)](sequence-tcp.md#tcp-サービス開始-receiver)
- [TCP 正常接続・通信・切断](sequence-tcp.md#tcp-正常接続通信切断)
- [TCP SENDER 再起動・自動再接続](sequence-tcp.md#tcp-sender-再起動自動再接続)
- [TCP PING 応答タイムアウト](sequence-tcp.md#tcp-ping-応答タイムアウト)
- [TCP RECEIVER 側 PING タイムアウト](sequence-tcp.md#tcp-receiver-側-ping-タイムアウト)
- [TCP マルチパス](sequence-tcp.md#tcp-マルチパス)
