# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/config_load_global.c

INCDIR += $(MYAPP_DIR)/test/include

# ライブラリの指定
LIBS += mock_porter mock_cplat mock_libc
