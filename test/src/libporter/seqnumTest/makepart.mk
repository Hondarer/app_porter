# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/seqnum.c

# ライブラリの指定
LIBS += mock_libc
