# コマンドの別プロセス試験と、実ライブラリを使う TCP マルチパス試験。
# 複数コンポーネントの結合確認のため TEST_SRCS は指定しない。
LIBS      := porter cplat mock_libc
INCDIR    += $(MYAPP_DIR)/test/include
