# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/packet.c

# ライブラリの指定
LIBS += mock_cplat mock_libc
