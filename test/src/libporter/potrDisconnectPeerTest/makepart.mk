# テスト対象のソースファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/api/potrDisconnectPeer.c \
	$(MYAPP_DIR)/prod/libsrc/porter/potrPathEvent.c

# TEST_SRCS の相対インクルード解決
INCDIR += \
	$(MYAPP_DIR)/prod/libsrc/porter/api \
	$(MYAPP_DIR)/prod/libsrc/porter \
	$(MYAPP_DIR)/prod/libsrc/porter/infra

# ライブラリの指定
LIBS += mock_porter mock_com_util com_util mock_libc
