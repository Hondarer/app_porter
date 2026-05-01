# 結合テストはライブラリ本体をリンクしない。
# send / recv バイナリ (外部プロセス) を実行するため、リンク対象は testfw のみ。
LIBS      := com_util mock_libc
INCDIR    += $(MYAPP_DIR)/test/include
