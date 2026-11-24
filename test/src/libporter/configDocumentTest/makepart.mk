# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/config_document.c

ADD_SRCS += \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/config_report.c

INCDIR += $(MYAPP_DIR)/test/include

# Windows で mock_cjson の実シンボルを直接参照し LNK4217/LNK4286 を防ぐ
DEFINES += CJSON_HIDE_SYMBOLS

# ライブラリの指定
LIBS += mock_porter mock_cplat mock_libc mock_cjson
