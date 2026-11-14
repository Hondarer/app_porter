TEST_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/security/potr_auth.c

# OpenSSL は mock の実委譲から読み込む。実ライブラリを同時にリンクしない。
LIBS += mock_openssl mock_cplat mock_libc
