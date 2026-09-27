# app 配下 makefile テンプレート
# すべての app/<app_name>/.../makefile で使用する標準テンプレート
# 本ファイルの直接編集は禁止する。
#
# [責務境界]
# - __template.mk: prepare.mk を読み込むための最小ブートストラップのみ
#   (ワークスペース ルート検出と include パス確定)
# - prepare.mk: 共有初期化 (MAKEFW_HOME 解決、環境・ツール判定、設定読み込み)

# ワークスペースのディレクトリ
find-up = \
    $(if $(wildcard $(1)/$(2)),$(1),\
        $(if $(filter $(1),$(patsubst %/,%,$(dir $(1)))),,\
            $(call find-up,$(patsubst %/,%,$(dir $(1))),$(2))\
        )\
    )

# 再帰 make 間でワークスペース ルートは不変のため、内部キャッシュ変数で継承する
ifeq ($(origin MAKEFW_WORKSPACE_DIR), undefined)
    MAKEFW_WORKSPACE_DIR := $(strip $(call find-up,$(CURDIR),.workspaceRoot))
endif
export MAKEFW_WORKSPACE_DIR

WORKSPACE_DIR := $(MAKEFW_WORKSPACE_DIR)
ifeq ($(WORKSPACE_DIR),)
    $(error Workspace root marker (.workspaceRoot) was not found from $(CURDIR))
endif

# 準備処理 (ビルド テンプレートより前に include)
# 共有初期化は prepare.mk 側で実施する。
include $(WORKSPACE_DIR)/framework/makefw/makefiles/prepare.mk

##### makepart.mk の内容は、このタイミングで処理される #####

# ビルド テンプレートを include
include $(MAKEFW_HOME)/makefiles/makemain.mk
