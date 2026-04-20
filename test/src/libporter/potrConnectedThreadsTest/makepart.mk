# テスト対象のソースファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/thread/potrConnectedThreads.c

# TEST_SRCS の相対インクルード解決
INCDIR += \
	$(MYAPP_DIR)/prod/libsrc/porter/thread \
	$(MYAPP_DIR)/prod/libsrc/porter/infra

# ライブラリの指定
LIBS += mock_porter mock_com_util com_util
