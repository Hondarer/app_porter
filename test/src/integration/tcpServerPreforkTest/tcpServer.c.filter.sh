#!/bin/sh
# テスト実行体の main とコマンド本体の入口だけを分ける。
printf '#include "prefork_probe.h"\n'
sed 's/^int main(/int tcpserver_command_main(/'
