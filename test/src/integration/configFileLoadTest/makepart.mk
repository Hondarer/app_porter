# 実ファイルを使う設定読込の組み合わせテスト
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/configLoadGlobal.c \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/configLoadService.c \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/configListServiceIds.c \
	$(MYAPP_DIR)/../com_util/prod/libsrc/com_util/crt/stdio.c

# TEST_SRCS の相対インクルード解決
INCDIR += \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol \
	$(MYAPP_DIR)/prod/libsrc/porter/infra \
	$(MYAPP_DIR)/test/include \
	$(MYAPP_DIR)/../com_util/prod/libsrc/com_util/crt

# mock_com_util はリンクしない
LIBS += mock_porter com_util mock_libc
