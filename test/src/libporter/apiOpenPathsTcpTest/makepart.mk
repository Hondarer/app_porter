TEST_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/api/api_open_paths_tcp.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/infra/potr_result.c
LIBS += mock_porter mock_cplat mock_libc
INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/api
