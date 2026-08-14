# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/potr_path_event.c

# ライブラリの指定
LIBS += mock_com_util mock_libc
