# テスト対象のソースファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/api/potrSend.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrSendQueue.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrPlatform_linux.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrPlatform_windows.c

# ライブラリの指定
LIBS += mock_porter mock_com_util mock_libc
