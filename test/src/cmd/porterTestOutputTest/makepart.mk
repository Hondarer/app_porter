TEST_SRCS := $(MYAPP_DIR)/prod/src/cmd/porter-test/porter-test.c
LIBS += mock_porter mock_cplat mock_libc
USE_WRAP_MAIN := 1
LIBS += porter
