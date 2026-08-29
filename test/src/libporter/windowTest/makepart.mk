# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/window.c

# window.c の依存ソースであり、本テストのカバレッジ対象ではない。
ADD_SRCS += \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/seqnum.c

# ライブラリの指定
LIBS += mock_cplat mock_libc
