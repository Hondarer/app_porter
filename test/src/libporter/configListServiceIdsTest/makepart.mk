# テスト対象のソースファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/configListServiceIds.c

# TEST_SRCS の相対インクルード解決
INCDIR += \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol \
	$(MYAPP_DIR)/test/include

# ライブラリの指定
LIBS += mock_porter mock_com_util com_util mock_libc
