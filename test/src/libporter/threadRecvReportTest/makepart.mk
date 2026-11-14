TEST_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/thread/thread_recv_report.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/protocol/packet.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/protocol/packet_seal.c
INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/thread
LIBS += mock_porter mock_cplat mock_libc
