# cplat の解除可能ソケット待機と porter の接続管理を取り込み、
# OS の起床通知 (Windows: WSASetEvent、Linux: eventfd write) の失敗注入で回収を検証する。
# OS mock (mock_winsock / mock_unistd / mock_poll / mock_socket) を効かせるため、
# cplat のソケット実装は TEST_SRCS へ指定する。
TEST_SRCS := \
	$(WORKSPACE_DIR)/app/cplat/prod/libsrc/cplat/net/socket_linux.c \
	$(WORKSPACE_DIR)/app/cplat/prod/libsrc/cplat/net/socket_windows.c \
	$(WORKSPACE_DIR)/app/cplat/prod/libsrc/cplat/net/socket_watch_linux.c \
	$(WORKSPACE_DIR)/app/cplat/prod/libsrc/cplat/net/socket_watch_windows.c

ADD_SRCS := \
	$(WORKSPACE_DIR)/app/cplat/prod/libsrc/cplat/base/error.c \
	$(WORKSPACE_DIR)/app/cplat/prod/libsrc/cplat/base/result.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potr_socket_io.c \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potr_result.c \
	$(MYAPP_DIR)/prod/libsrc/porter/protocol/packet_seal.c \
	$(MYAPP_DIR)/prod/libsrc/porter/thread/thread_connect_prepare.c \
	$(MYAPP_DIR)/prod/libsrc/porter/thread/potr_connect_thread.c

LIBS += mock_porter mock_cplat mock_libc
INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/thread $(WORKSPACE_DIR)/app/cplat/prod/include_internal
