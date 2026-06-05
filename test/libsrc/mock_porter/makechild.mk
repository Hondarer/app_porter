# サブフォルダーはコンパイルのみ
NO_LINK = 1

# 静的ライブラリの場合に指定: サブフォルダーのターゲットを明示的に指定
# (Windows 環境で、pdb がサブフォルダー名単位で生成されないようにする)
THIS_MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
TARGET := $(notdir $(patsubst %/,%,$(THIS_MAKEFILE_DIR)))
