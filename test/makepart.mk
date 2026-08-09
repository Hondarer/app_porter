ifdef PLATFORM_WINDOWS
    # mock_porter と mock_com_util をテスト実行体へ直接定義する。
    # 製品ライブラリのリンク方式は変更しない。
    CFLAGS   += /DPOTR_STATIC /DCOM_UTIL_STATIC
    CXXFLAGS += /DPOTR_STATIC /DCOM_UTIL_STATIC
endif
