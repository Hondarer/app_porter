ifdef PLATFORM_WINDOWS
    # mock_porter と mock_cplat をテスト実行体へ直接定義する。
    # 製品ライブラリのリンク方式は変更しない。
    CFLAGS   += /DPOTR_STATIC /DCPLAT_STATIC
    CXXFLAGS += /DPOTR_STATIC /DCPLAT_STATIC
endif
