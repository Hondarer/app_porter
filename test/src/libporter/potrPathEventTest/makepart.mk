# テスト対象のソースファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/potrPathEvent.c

# TEST_SRCS の相対インクルード解決
INCDIR += $(MYAPP_DIR)/prod/libsrc/porter

# ライブラリの指定
LIBS += mock_libc
