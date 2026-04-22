# 結合テストはライブラリ本体をリンクしない。
# send / recv バイナリ (外部プロセス) を実行するため、リンク対象は testfw のみ。
TEST_SRCS := $(MYAPP_DIR)/../com_util/prod/libsrc/com_util/crt/stdio.c
LIBS      := mock_libc
INCDIR    += $(MYAPP_DIR)/test/include
INCDIR    += $(MYAPP_DIR)/../com_util/prod/libsrc/com_util/crt
