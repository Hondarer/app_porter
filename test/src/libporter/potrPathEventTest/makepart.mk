# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/potr_path_event.c

# ライブラリの指定
LIBS += mock_cplat mock_libc

ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/potr_handle.c $(MYAPP_DIR)/prod/libsrc/porter/potr_notify.c $(MYAPP_DIR)/prod/libsrc/porter/infra/potr_send_queue_abort.c
INCDIR += $(MYAPP_DIR)/test/include
