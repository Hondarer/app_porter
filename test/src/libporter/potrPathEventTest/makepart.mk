# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/potr_path_event.c

# ライブラリの指定
LIBS += mock_cplat mock_libc
