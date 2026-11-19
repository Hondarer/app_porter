TEST_SRCS := $(MYAPP_DIR)/prod/src/cmd/tech-sample/tcpServer/tcpServer_linux.c
ADD_SRCS := $(MYAPP_DIR)/prod/src/cmd/tech-sample/tcpServer/tcpServer_common.c
INCDIR += $(MYAPP_DIR)/prod/src/cmd/tech-sample/tcpServer
LIBS += mock_cplat mock_libc
