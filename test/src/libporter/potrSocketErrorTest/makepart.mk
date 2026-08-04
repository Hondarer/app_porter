# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrSocketError.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrSocketError_linux.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrSocketError_windows.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrPlatform_linux.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrPlatform_windows.c

# ライブラリの指定
LIBS += com_util
