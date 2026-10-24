# テスト対象のソース ファイル
TEST_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/api/api_context_cleanup.c

LIBS += mock_porter mock_cplat mock_libc

INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/api
