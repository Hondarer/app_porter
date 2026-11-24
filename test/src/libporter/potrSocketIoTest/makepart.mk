TEST_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/infra/potr_socket_io.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/infra/potr_result.c
LIBS += mock_porter mock_cplat mock_libc
INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/infra
