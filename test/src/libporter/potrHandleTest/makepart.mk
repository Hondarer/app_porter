TEST_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/potr_handle.c
ADD_SRCS := $(MYAPP_DIR)/prod/libsrc/porter/potr_path_event.c $(MYAPP_DIR)/prod/libsrc/porter/potr_notify.c $(MYAPP_DIR)/prod/libsrc/porter/infra/potr_send_queue_abort.c
LIBS += mock_cplat mock_libc
