# ライブラリの検索パス
LIBSDIR += $(MYAPP_DIR)/test/lib
LIBSDIR += $(MYAPP_DIR)/../com_util/test/lib
INCDIR  += $(MYAPP_DIR)/../com_util/test/include

ifdef PLATFORM_WINDOWS
    # 外部関数の static 定義
    CFLAGS   += /DPOTR_STATIC /DCOM_UTIL_STATIC
    CXXFLAGS += /DPOTR_STATIC /DCOM_UTIL_STATIC
endif
