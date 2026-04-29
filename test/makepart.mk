# インクルードパス
INCDIR += \
	$(MYAPP_DIR)/../com_util/test/include \
	$(MYAPP_DIR)/test/include \
	$(TESTFW_DIR)/gtest/include \
	$(TESTFW_DIR)/include

# ライブラリの検索パス
LIBSDIR += \
	$(MYAPP_DIR)/test/lib \
	$(MYAPP_DIR)/../com_util/test/lib

ifdef PLATFORM_WINDOWS
    # 外部関数の static 定義
    CFLAGS   += /DPOTR_STATIC /DCOM_UTIL_STATIC
    CXXFLAGS += /DPOTR_STATIC /DCOM_UTIL_STATIC
endif
