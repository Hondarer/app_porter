TEST_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/security/certificate.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/protocol/packet_seal.c
LIBS += mock_porter mock_cplat mock_libc
