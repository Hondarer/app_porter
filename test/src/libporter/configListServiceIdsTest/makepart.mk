# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/configListServiceIds.c

INCDIR += $(MYAPP_DIR)/test/include

# ライブラリの指定
LIBS += mock_porter mock_com_util mock_libc
