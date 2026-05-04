# テスト対象のソースファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/api/potrDisconnectPeer.c \
	$(MYAPP_DIR)/prod/libsrc/porter/potrPathEvent.c

# ライブラリの指定
LIBS += mock_porter mock_com_util mock_libc
