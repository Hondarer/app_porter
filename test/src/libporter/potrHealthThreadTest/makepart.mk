# テスト対象のソース ファイル
TEST_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/thread/potr_health_thread.c

LIBS += mock_porter mock_cplat mock_libc

INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/thread
