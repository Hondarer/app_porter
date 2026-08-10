# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrSocketError.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrSocketError_linux.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrSocketError_windows.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrPlatform_linux.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrPlatform_windows.c

# ライブラリの指定
# TEST_SRCS の socket() / sendto() は include_override により mock_libc の関数へ置換される。
LIBS += com_util mock_libc
