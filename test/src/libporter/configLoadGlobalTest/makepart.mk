# テスト対象のソースファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/configLoadGlobal.c

INCDIR += $(MYAPP_DIR)/test/include

# ライブラリの指定
LIBS += mock_porter mock_com_util mock_libc
