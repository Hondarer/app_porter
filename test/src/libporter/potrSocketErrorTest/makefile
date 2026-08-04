# app 配下 makefile テンプレート
find-up = $(if $(wildcard $(1)/$(2)),$(1),$(if $(filter $(1),$(patsubst %/,%,$(dir $(1)))),,$(call find-up,$(patsubst %/,%,$(dir $(1))),$(2))))
ifeq ($(origin MAKEFW_WORKSPACE_DIR), undefined)
    MAKEFW_WORKSPACE_DIR := $(strip $(call find-up,$(CURDIR),.workspaceRoot))
endif
export MAKEFW_WORKSPACE_DIR
WORKSPACE_DIR := $(MAKEFW_WORKSPACE_DIR)
ifeq ($(WORKSPACE_DIR),)
    $(error Workspace root marker (.workspaceRoot) was not found from $(CURDIR))
endif
include $(WORKSPACE_DIR)/framework/makefw/makefiles/prepare.mk
include $(MAKEFW_HOME)/makefiles/makemain.mk
