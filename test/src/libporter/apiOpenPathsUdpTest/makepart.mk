TEST_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/api/api_open_paths_udp.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/infra/potr_result.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/protocol/config_check.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/protocol/config_report.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/protocol/config_schema.c
LIBS += mock_porter mock_cplat mock_libc
INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/api
