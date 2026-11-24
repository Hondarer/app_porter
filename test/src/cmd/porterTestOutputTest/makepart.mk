TEST_SRCS := $(MYAPP_DIR)/prod/src/cmd/porter-test/porter-test.c
# porter は、mock_cplat と mock_libc の後に並べる。
# 共有ライブラリを静的な mock のライブラリより前に並べると、Linux のリンカーが、共有ライブラリの未解決の参照を
# mock のライブラリから解決し、テスト対象が使わない cplat の関数の mock まで実行体へ取り込む。
# その mock が libcplat の内部の呼び出しへ割り込み、既定の委譲との間で再帰する。
LIBS += mock_cplat mock_libc
USE_WRAP_MAIN := 1
LIBS += porter
