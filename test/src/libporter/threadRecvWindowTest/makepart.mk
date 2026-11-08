TEST_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/thread/thread_recv_window.c
INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/thread
LIBS += mock_porter mock_cplat mock_libc
ADD_SRCS += \
    $(MYAPP_DIR)/prod/libsrc/porter/protocol/packet.c \
    $(MYAPP_DIR)/prod/libsrc/porter/protocol/window.c \
    $(MYAPP_DIR)/prod/libsrc/porter/protocol/seqnum.c \
    $(MYAPP_DIR)/prod/libsrc/porter/thread/thread_recv_session.c \
    $(MYAPP_DIR)/prod/libsrc/porter/potr_path_event.c
ADD_SRCS += \
    $(MYAPP_DIR)/prod/libsrc/porter/thread/thread_recv_health.c \
    $(MYAPP_DIR)/prod/libsrc/porter/potr_notify.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/infra/potr_send_queue.c
ADD_SRCS += $(MYAPP_DIR)/prod/libsrc/porter/infra/potr_send_queue_abort.c
