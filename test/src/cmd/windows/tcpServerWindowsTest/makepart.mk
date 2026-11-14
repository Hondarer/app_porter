TEST_SRCS := $(MYAPP_DIR)/prod/src/cmd/tech-sample/tcpServer/tcpServer_windows.c
ADD_SRCS := $(MYAPP_DIR)/prod/src/cmd/tech-sample/tcpServer/tcpServer_common.c
INCDIR += $(MYAPP_DIR)/prod/src/cmd/tech-sample/tcpServer
# mock_cplat は実 DLL へ動的に委譲するため、実ライブラリを同時にリンクしない。
LIBS += mock_cplat mock_libc
