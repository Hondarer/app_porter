# 実ファイルを使う設定読込の組み合わせテスト
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/config_load_global.c \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/config_load_service.c \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/config_list_service_ids.c

INCDIR += \
	$(MYAPP_DIR)/test/include

# mock_com_util はリンクしない
LIBS += mock_porter com_util mock_libc
