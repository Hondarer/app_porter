LIBS += cplat mock_libc

ifdef PLATFORM_WINDOWS
    NO_GTEST_MAIN := 1
    DEFINES += TCPSERVER_PREFORK_TEST
endif

INCDIR += $(MYAPP_DIR)/prod/src/cmd/tech-sample/tcpServer
# 実プロセス統合テストの補助実行体であり、単体カバレッジには計上しない。
# 両 OS で同じ入力一覧を使い、Windows のコピーを Linux の直接ソースと誤認させない。
ADD_SRCS += $(MYAPP_DIR)/prod/src/cmd/tech-sample/tcpServer/tcpServer.c \
                $(MYAPP_DIR)/prod/src/cmd/tech-sample/tcpServer/tcpServer_common.c \
                $(MYAPP_DIR)/prod/src/cmd/tech-sample/tcpServer/tcpServer_windows.c \
                $(MYAPP_DIR)/prod/src/cmd/tech-sample/tcpServer/tcpServer_linux.c
