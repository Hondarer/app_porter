# 認証から実ソケットの送受信までを結合し、OpenSSL は mock の実委譲だけで解決する。
ADD_SRCS += $(wildcard $(MYAPP_DIR)/prod/libsrc/porter/*.c)
ADD_SRCS += $(wildcard $(MYAPP_DIR)/prod/libsrc/porter/api/*.c)
ADD_SRCS += $(wildcard $(MYAPP_DIR)/prod/libsrc/porter/infra/*.c)
ADD_SRCS += $(wildcard $(MYAPP_DIR)/prod/libsrc/porter/protocol/*.c)
ADD_SRCS += $(wildcard $(MYAPP_DIR)/prod/libsrc/porter/security/*.c)
ADD_SRCS += $(wildcard $(MYAPP_DIR)/prod/libsrc/porter/thread/*.c)
LIBS := mock_openssl cplat cjson_app mock_libc
INCDIR += $(MYAPP_DIR)/test/include
INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/api
INCDIR += $(MYAPP_DIR)/prod/libsrc/porter/thread
# ADD_SRCS の認証モジュールにも OpenSSL の差し替えを適用する。
ifdef PLATFORM_WINDOWS
    CFLAGS += /I$(WORKSPACE_DIR)/app/openssl/test/include_override
else
    CFLAGS += -I$(WORKSPACE_DIR)/app/openssl/test/include_override
endif
