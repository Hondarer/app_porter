# テスト対象のソースファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/thread/potrConnectedThreads.c

# ライブラリの指定
LIBS += mock_porter mock_com_util
