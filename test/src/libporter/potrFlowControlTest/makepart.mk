TEST_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/potr_flow_control.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/protocol/packet_seal.c
LIBS += mock_porter mock_cplat mock_libc

INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/thread
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/thread/thread_recv_session.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/potr_path_event.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/potr_notify.c
