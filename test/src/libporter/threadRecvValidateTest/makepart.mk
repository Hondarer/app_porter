TEST_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/thread/thread_recv_validate.c
LIBS += mock_porter mock_cplat mock_libc
INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/thread

ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/protocol/packet_seal.c
