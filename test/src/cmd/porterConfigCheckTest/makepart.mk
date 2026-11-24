TEST_SRCS := $(MYAPP_DIR)/prod/src/cmd/porter-config-check/porter-config-check.c
LIBS += porter mock_cplat mock_libc
USE_WRAP_MAIN := 1
