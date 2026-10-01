# サブフォルダーはコンパイルのみ
NO_LINK = 1

# 静的ライブラリの場合に指定: サブフォルダーのターゲットを明示的に指定
# (Windows 環境で、pdb がサブフォルダー名単位で生成されないようにする)
THIS_MAKEFILE_DIR := $(call _makefw_path_dir,$(call _makefw_path_abspath,$(call _makefw_decode_path,$(lastword $(call _makefw_pack_path_roots,$(MAKEFILE_LIST))))))
TARGET := $(call _makefw_path_notdir,$(patsubst %/,%,$(THIS_MAKEFILE_DIR)))
