TEST_SRCS := \
    $(MYAPP_DIR)/prod/libsrc/porter/thread/potr_send_thread.c \
    $(MYAPP_DIR)/prod/libsrc/porter/thread/potr_health_thread.c \
    $(MYAPP_DIR)/prod/libsrc/porter/thread/thread_recv_window.c \
    $(MYAPP_DIR)/prod/libsrc/porter/thread/potr_recv_thread.c \
    $(MYAPP_DIR)/prod/libsrc/porter/infra/potr_tcp_control.c
ADD_SRCS += \
    $(MYAPP_DIR)/prod/libsrc/porter/protocol/packet_seal.c \
    $(MYAPP_DIR)/prod/libsrc/porter/protocol/packet.c \
    $(MYAPP_DIR)/prod/libsrc/porter/protocol/window.c \
    $(MYAPP_DIR)/prod/libsrc/porter/protocol/seqnum.c \
    $(MYAPP_DIR)/prod/libsrc/porter/thread/thread_recv_validate.c
LIBS += mock_porter mock_cplat mock_libc
INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/thread
ADD_SRCS += \
    $(MYAPP_DIR)/prod/libsrc/porter/thread/thread_recv_session.c \
    $(MYAPP_DIR)/prod/libsrc/porter/potr_path_event.c \
    $(MYAPP_DIR)/prod/libsrc/porter/potr_notify.c

ADD_SRCS += \
    $(MYAPP_DIR)/prod/libsrc/porter/protocol/path_group.c \
    $(MYAPP_DIR)/prod/libsrc/porter/protocol/path_group_context.c \
    $(MYAPP_DIR)/prod/libsrc/porter/protocol/receiver_report.c \
    $(MYAPP_DIR)/prod/libsrc/porter/thread/thread_recv_report.c
