# 実ファイルを使う設定読込の組み合わせテスト
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/configLoadGlobal.c \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/configLoadService.c \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/configListServiceIds.c

INCDIR += \
	$(MYAPP_DIR)/test/include

# mock_com_util はリンクしない
LIBS += mock_porter com_util mock_libc
