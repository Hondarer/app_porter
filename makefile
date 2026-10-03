# 各 app 直下 makefile テンプレート
# すべての app/<app_name>/makefile で使用する標準テンプレート
# 本ファイルの直接編集は禁止する。

SUBDIRS = \
	prod \
	test

APP_NAME = $(notdir $(CURDIR))
MAKEFILE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
WORKSPACE_DIR ?= $(abspath $(MAKEFILE_DIR)/../..)
CONFIG ?= RelWithDebInfo
MAKEFW_HOME := $(strip $(MAKEFW_HOME))
ifeq ($(MAKEFW_HOME),)
    $(error MAKEFW_HOME is required. Export MAKEFW_HOME before running make)
endif
TESTFW_HOME   ?= $(WORKSPACE_DIR)/framework/testfw
TESTFW_BANNER = $(TESTFW_HOME)/bin_internal/banner.sh
APPDEPS_RESOLVER = $(MAKEFW_HOME)/bin_internal/resolve_app_deps.sh

# 子 make が必要になった時点で依存パスを解決する。
# make の export 変数で遅延評価すると、署名判定のレシピ起動時にも展開されるため、
# レシピのシェル内で解決・export し、prod と test の双方へ同じ結果を渡す。
# 同じ app の親から継承した結果だけ再利用し、別 app の結果は解決し直す。
define _MAKEFW_RESOLVE_APP_PATHS
if [ "$${MAKEFW_APP_PATHS_CACHE_APP:-}" != "$(CURDIR)" ] || [ "$${MAKEFW_APP_PATHS_CACHE+x}" != "x" ]; then \
    MAKEFW_APP_PATHS_CACHE=$$(bash "$(APPDEPS_RESOLVER)" --paths-all "$(CURDIR)" test) || { \
        makefw_paths_status=$$?; \
        echo "ERROR: Failed to resolve app dependencies for $(CURDIR)" >&2; \
        if [ -n "$${sig_file:-}" ]; then rm -f "$$sig_file"; fi; \
        exit $$makefw_paths_status; \
    }; \
    MAKEFW_APP_PATHS_CACHE_APP="$(CURDIR)"; \
fi; \
export MAKEFW_APP_PATHS_CACHE_APP MAKEFW_APP_PATHS_CACHE;
endef

# app/<name>.assured.stamp が HEAD と一致し、追加・削除・変更が無いとき 1。
# サブモジュールでない app は警告して 0。子 make へは export せず、署名コマンドの直前だけで渡す。
# Windows では bash の起動が 1 回あたり 1 秒近くかかり、app 直下 makefile の parse ごとに積み上がる。
# スタンプが無いときは bash を起動せず 0 とする。
# test ターゲットの再帰 make では、同じ app について親が判定した結果を
# MAKEFW_ASSURED_CACHE (<app ディレクトリ>|<0 または 1>) で引き継ぐ。
ifeq ($(wildcard $(CURDIR).assured.stamp),)
_MAKEFW_ASSURED_ACTIVE := 0
else ifeq ($(MAKEFW_ASSURED_CACHE),$(CURDIR)|1)
_MAKEFW_ASSURED_ACTIVE := 1
else ifeq ($(MAKEFW_ASSURED_CACHE),$(CURDIR)|0)
_MAKEFW_ASSURED_ACTIVE := 0
else
_MAKEFW_ASSURED_ACTIVE := $(strip $(shell bash "$(APPDEPS_RESOLVER)" --assured "$(CURDIR)"))
ifneq ($(strip $(.SHELLSTATUS)),0)
    $(error Failed to evaluate assured.stamp for $(CURDIR))
endif
endif

DOXY_SIGNATURE_GENERATOR = $(MAKEFW_HOME)/bin_internal/doxy_signature.py
COVERITY_MAKE_WRAPPER = $(MAKEFW_HOME)/bin_internal/cov-build-app.sh
COVERITY_CONFIG = $(CURDIR)/prod/coverity.mk
DOXY_WARN_FILE = $(CURDIR)/doxy.warn
BUILD_STAMP = $(CURDIR)/make_build.stamp
TEST_STAMP = $(CURDIR)/make_test.stamp
DOXY_STAMP  = $(CURDIR)/make_doxy.stamp
SUBDIR_TARGETS = $(addprefix __subdir__,$(SUBDIRS))

ifneq ($(wildcard $(COVERITY_CONFIG)),)
include $(COVERITY_CONFIG)
endif

# Windows の場合、MSVC_CRT_SUBDIR が未設定なら計算する
# Calculate MSVC_CRT_SUBDIR if not set (for standalone builds)
ifeq ($(OS),Windows_NT)
    MSVC_CRT ?= shared
    CONFIG ?= RelWithDebInfo
    ifeq ($(MSVC_CRT_SUBDIR),)
        ifeq ($(CONFIG),Debug)
            ifeq ($(MSVC_CRT),shared)
                MSVC_CRT_SUBDIR := mdd
            else
                MSVC_CRT_SUBDIR := mtd
            endif
        else
            ifeq ($(MSVC_CRT),shared)
                MSVC_CRT_SUBDIR := md
            else
                MSVC_CRT_SUBDIR := mt
            endif
        endif
    endif
endif

export WORKSPACE_DIR
export MAKEFW_HOME
export DOXYFW_HOME
export TESTFW_HOME

.DEFAULT_GOAL := default

.PHONY: __ensure-coverity
__ensure-coverity:
	@if [ -z "$(COVERITY_HOME)" ]; then \
		echo "ERROR: COVERITY_HOME is required for with-cov." >&2; \
		exit 1; \
	fi
	@if [ ! -f "$(COVERITY_CONFIG)" ]; then \
		echo "ERROR: prod/coverity.mk is required for $(CURDIR)/with-cov." >&2; \
		exit 1; \
	fi
	@if [ "$(COVERITY_TOOLCHAIN)" != "c_cpp" ] && [ "$(COVERITY_TOOLCHAIN)" != "dotnet" ]; then \
		echo "ERROR: COVERITY_TOOLCHAIN must be 'c_cpp' or 'dotnet' in $(COVERITY_CONFIG)." >&2; \
		exit 1; \
	fi
	@if [ ! -f "$(COVERITY_MAKE_WRAPPER)" ]; then \
		echo "ERROR: Coverity wrapper script was not found: $(COVERITY_MAKE_WRAPPER)" >&2; \
		exit 1; \
	fi

.PHONY: default
default:
	@sig_file=$$(mktemp); \
	signature_available=1; \
	if ! CONFIG="$(CONFIG)" MSVC_CRT_SUBDIR="$(MSVC_CRT_SUBDIR)" CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS)" LDFLAGS="$(LDFLAGS)" DEFINES="$(DEFINES)" LIBS="$(LIBS)" MAKEFW_ASSURED_ACTIVE="$(_MAKEFW_ASSURED_ACTIVE)" bash "$(APPDEPS_RESOLVER)" --signature "$(CURDIR)" build > "$$sig_file"; then \
		signature_available=0; \
		rm -f "$$sig_file"; \
		sig_file=""; \
		echo "Warning: failed to calculate build signature. Running build without skip."; \
	fi; \
	if [ $$signature_available -eq 1 ] && [ -f "$(BUILD_STAMP)" ] && [ -n "$(MSVC_CRT_SUBDIR)" ]; then \
		prev_crt=$$(sed -n 's/^MSVC_CRT=//p' "$(BUILD_STAMP)"); \
		if [ -n "$$prev_crt" ] && [ "$$prev_crt" != "$(MSVC_CRT_SUBDIR)" ]; then \
			rm -f "$$sig_file"; \
			echo "ERROR: MSVC runtime mismatch detected. Run 'make clean' first, then rebuild.  Previous build: $$prev_crt  Current request: $(MSVC_CRT_SUBDIR)" >&2; \
			exit 1; \
		fi; \
	fi; \
	current_clean=0; \
	if [ $$signature_available -eq 1 ]; then current_clean=$$(sed -n '1s/^CLEAN=//p' "$$sig_file"); fi; \
	if [ $$signature_available -eq 1 ] && [ "$$current_clean" = "1" ] && [ -f "$(BUILD_STAMP)" ] && cmp -s "$$sig_file" "$(BUILD_STAMP)"; then \
		echo "INFO: Skipping build (dependencies are unchanged and clean)"; \
		rm -f "$$sig_file"; \
	else \
		rm -f "$(BUILD_STAMP)"; \
		make_exit=0; \
		for dir in $(SUBDIRS); do \
			if [ -f $$dir/makefile ]; then \
				$(call _MAKEFW_RESOLVE_APP_PATHS) \
				skip_src=""; \
				if [ "$$dir" = "test" ] && [ "$(_MAKEFW_ASSURED_ACTIVE)" = "1" ]; then \
					skip_src="MAKEFW_SKIP_TEST_SRC=1"; \
				fi; \
				echo $(MAKE) -C $$dir $$skip_src; \
				$(MAKE) -C $$dir $$skip_src || { make_exit=$$?; break; }; \
			fi; \
		done; \
		if [ $$make_exit -eq 0 ] && [ $$signature_available -eq 1 ] && [ "$$current_clean" = "1" ]; then \
			cp "$$sig_file" "$(BUILD_STAMP)"; \
		fi; \
		if [ -n "$$sig_file" ]; then rm -f "$$sig_file"; fi; \
		if [ $$make_exit -ne 0 ]; then exit $$make_exit; fi; \
	fi

# app/makefile の with-cov から、対象 app 直下で呼ばれる。
# prod/coverity.mk がある app は with-cov、無い app (coverity 対象の依存先) は prod だけを通常どおり make する。
# 依存先の test (モックとテスト コード) は with-cov の対象外であり、make_build.stamp も更新しない。
.PHONY: _makefw_with_cov_prod
_makefw_with_cov_prod:
	@if [ -f "$(COVERITY_CONFIG)" ]; then \
		echo $(MAKE) with-cov; \
		$(MAKE) with-cov; \
	elif [ -f prod/makefile ]; then \
		$(call _MAKEFW_RESOLVE_APP_PATHS) \
		echo $(MAKE) -C prod; \
		$(MAKE) -C prod || exit 1; \
	fi

# with-cov の前提。cov-build が prod の翻訳単位を取得できるよう、prod の成果物と make_build.stamp を削除する。
# assured.stamp の省略条件に関係なく、cov-build の外で毎回実行する。test は with-cov の対象外のため clean しない。
.PHONY: _makefw_clean_for_coverity
_makefw_clean_for_coverity: __ensure-coverity
	@if [ -f "$(BUILD_STAMP)" ] && [ -n "$(MSVC_CRT_SUBDIR)" ]; then \
		prev_crt=$$(sed -n 's/^MSVC_CRT=//p' "$(BUILD_STAMP)"); \
		if [ -n "$$prev_crt" ] && [ "$$prev_crt" != "$(MSVC_CRT_SUBDIR)" ]; then \
			echo "ERROR: MSVC runtime mismatch detected. Run 'make clean' first, then rebuild.  Previous build: $$prev_crt  Current request: $(MSVC_CRT_SUBDIR)" >&2; \
			exit 1; \
		fi; \
	fi
	@if [ -f prod/makefile ]; then \
		$(call _MAKEFW_RESOLVE_APP_PATHS) \
		echo $(MAKE) -C prod clean; \
		$(MAKE) -C prod clean || exit 1; \
	fi
	@rm -f "$(BUILD_STAMP)"

# with-cov は、解析対象の prod を cov-build 経由で通常どおり make (リンクを含む) する。
# 構文解析や自動生成を伴う app では、解析対象のソースがリンクまでのビルド過程で生成されるため、
# コンパイルのみにはしない。
# test (モックとテスト コード) のビルドと、make_build.stamp / make_test.stamp の更新は行わない。
.PHONY: with-cov
with-cov: __ensure-coverity
ifneq ($(wildcard $(COVERITY_CONFIG)),)
with-cov: _makefw_clean_for_coverity
endif
with-cov:
	@if [ -f prod/makefile ]; then \
		$(call _MAKEFW_RESOLVE_APP_PATHS) \
		echo "$(COVERITY_MAKE_WRAPPER)" "$(COVERITY_TOOLCHAIN)" $(MAKE) -C prod; \
		"$(COVERITY_MAKE_WRAPPER)" "$(COVERITY_TOOLCHAIN)" $(MAKE) -C prod || exit 1; \
	fi
	@if [ "$(IDENT)" = "1" ]; then \
		_idir="$(WORKSPACE_DIR)/app/idir"; \
		if [ -d "$$_idir" ]; then \
			echo "IDENT=1: removing _ident_manifest.c emit from Coverity idir"; \
			"$(COVERITY_HOME)/bin/cov-manage-emit" \
				--dir "$$_idir" \
				--tu-pattern "file('*_ident_manifest.c')" \
				delete; \
		fi; \
	fi

# clean を省略するのは assured が有効かつ make_build.stamp があるときだけ。
# スタンプはサブディレクトリのビルド前に削除し、終了コード 0 のときだけ戻す。
# I/O エラーや異常終了でビルドが完了しないときはスタンプが無く、clean を実行する。
.PHONY: clean
ifneq ($(_MAKEFW_ASSURED_ACTIVE),1)
clean : SUBDIR_GOAL = clean
clean : $(SUBDIR_TARGETS)
	@rm -f "$(DOXY_WARN_FILE)" "$(BUILD_STAMP)" "$(TEST_STAMP)" "$(DOXY_STAMP)"
	@rm -f $(CURDIR)/doxy_*.warn
	@find "$(CURDIR)" -type d -name log -prune -exec rm -rf {} +
else ifeq ($(wildcard $(BUILD_STAMP)),)
clean : SUBDIR_GOAL = clean
clean : $(SUBDIR_TARGETS)
	@rm -f "$(DOXY_WARN_FILE)" "$(BUILD_STAMP)" "$(TEST_STAMP)" "$(DOXY_STAMP)"
	@rm -f $(CURDIR)/doxy_*.warn
	@find "$(CURDIR)" -type d -name log -prune -exec rm -rf {} +
else
clean:
	@echo "INFO: Skipping clean (assured.stamp matches the commit and make succeeded)"
endif

.PHONY: test
test :
	@MAKEFW_ASSURED_CACHE="$(CURDIR)|$(_MAKEFW_ASSURED_ACTIVE)" $(MAKE) $(MFLAGS)
	@if [ "$(_MAKEFW_ASSURED_ACTIVE)" = "1" ] && [ -f test/makefile ]; then \
		echo "INFO: Skipping test/src (assured.stamp matches the commit and the tree is clean)"; \
	elif [ -f test/makefile ]; then \
		sig_file=$$(mktemp); \
		signature_available=1; \
		if ! CONFIG="$(CONFIG)" MSVC_CRT_SUBDIR="$(MSVC_CRT_SUBDIR)" CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS)" LDFLAGS="$(LDFLAGS)" DEFINES="$(DEFINES)" LIBS="$(LIBS)" MAKEFW_ASSURED_ACTIVE="$(_MAKEFW_ASSURED_ACTIVE)" bash "$(APPDEPS_RESOLVER)" --signature "$(CURDIR)" test > "$$sig_file"; then \
			signature_available=0; \
			rm -f "$$sig_file"; \
			sig_file=""; \
			echo "Warning: failed to calculate test signature. Running test without skip."; \
		fi; \
		if [ $$signature_available -eq 1 ] && [ -f "$(TEST_STAMP)" ] && [ -n "$(MSVC_CRT_SUBDIR)" ]; then \
			prev_crt=$$(sed -n 's/^MSVC_CRT=//p' "$(TEST_STAMP)"); \
			if [ -n "$$prev_crt" ] && [ "$$prev_crt" != "$(MSVC_CRT_SUBDIR)" ]; then \
				rm -f "$$sig_file"; \
				echo "ERROR: MSVC runtime mismatch detected. Run 'make clean' first, then rebuild.  Previous build: $$prev_crt  Current request: $(MSVC_CRT_SUBDIR)" >&2; \
				exit 1; \
			fi; \
		fi; \
		current_clean=0; \
		if [ $$signature_available -eq 1 ]; then current_clean=$$(sed -n '1s/^CLEAN=//p' "$$sig_file"); fi; \
		if [ $$signature_available -eq 1 ] && [ "$$current_clean" = "1" ] && [ -f "$(TEST_STAMP)" ] && cmp -s "$$sig_file" "$(TEST_STAMP)"; then \
			echo "INFO: Skipping test (dependencies are unchanged and clean)"; \
			rm -f "$$sig_file"; \
			exit 0; \
		fi; \
		rm -f "$(TEST_STAMP)"; \
		$(call _MAKEFW_RESOLVE_APP_PATHS) \
		echo $(MAKE) -C test _test_run; \
		$(MAKE) -C test _test_run; \
		make_exit=$$?; \
		if [ $$make_exit -eq 0 ] && [ $$signature_available -eq 1 ] && [ "$$current_clean" = "1" ]; then \
			cp "$$sig_file" "$(TEST_STAMP)"; \
		fi; \
		if [ -n "$$sig_file" ]; then rm -f "$$sig_file"; fi; \
		if [ $$make_exit -ne 0 ]; then exit $$make_exit; fi; \
	else \
		:; # echo "Skipping directory 'test' (no makefile)"; \
	fi

.PHONY: doxy
doxy :
	@parts=""; \
	if [ -f prod/Doxyfile.part ]; then parts="$$parts prod/Doxyfile.part"; fi; \
	for p in prod/Doxyfile.part.*; do \
		[ -f "$$p" ] || continue; \
		parts="$$parts $$p"; \
	done; \
	if [ -z "$$parts" ]; then \
		:; # echo "INFO: Doxygen is not configured for $(APP_NAME), skipping."; \
		exit 0; \
	fi; \
	if [ -z "$(DOXYFW_HOME)" ]; then \
		echo "ERROR: DOXYFW_HOME is not defined."; \
		exit 1; \
	fi; \
	if [ ! -d "$(DOXYFW_HOME)" ] || [ ! -f "$(DOXYFW_HOME)/makefile" ]; then \
		:; # echo "INFO: $(DOXYFW_HOME) directory not found, skipping."; \
		exit 0; \
	fi; \
	sig_file=$$(mktemp); \
	signature_available=1; \
	if ! python3 "$(DOXY_SIGNATURE_GENERATOR)" "$(CURDIR)" > "$$sig_file"; then \
		signature_available=0; \
		rm -f "$$sig_file"; \
		sig_file=""; \
		echo "Warning: failed to calculate doxy signature. Running doxy without skip."; \
	fi; \
	if [ $$signature_available -eq 1 ] && [ -f "$(DOXY_STAMP)" ] && cmp -s "$$sig_file" "$(DOXY_STAMP)"; then \
		echo "INFO: Skipping doxy (Doxygen inputs are unchanged)"; \
		rm -f "$$sig_file"; \
		exit 0; \
	fi; \
	rm -f "$(DOXY_STAMP)"; \
	overall_exit=0; \
	for p in $$parts; do \
		case "$$p" in \
			prod/Doxyfile.part) sub=""; warn="$(CURDIR)/doxy.warn";; \
			prod/Doxyfile.part.*) sub="$${p#prod/Doxyfile.part.}"; warn="$(CURDIR)/doxy_$$sub.warn";; \
		esac; \
		echo $(MAKE) -C "$(DOXYFW_HOME)" CATEGORY=$(APP_NAME) SUBCATEGORY=$$sub; \
		rm -f "$$warn"; \
		$(MAKE) -C "$(DOXYFW_HOME)" CATEGORY=$(APP_NAME) SUBCATEGORY=$$sub; \
		MAKE_EXIT=$$?; \
		if [ -z "$(SUPPRESS_DOXY_WARN_PRINT)" ] && [ -s "$$warn" ]; then \
			printf '\n'; \
			bash "$(TESTFW_BANNER)" WARNING "\e[33m"; \
			printf '\n'; \
			printf '\033[33m===== %s =====\033[0m\n' "$$warn"; \
			while IFS= read -r line || [ -n "$$line" ]; do \
				clean_line=$$(printf '%s' "$$line" | tr -d '\r'); \
				printf '\033[33m%s\033[0m\n' "$$clean_line"; \
			done < "$$warn"; \
		fi; \
		if [ $$MAKE_EXIT -ne 0 ]; then overall_exit=$$MAKE_EXIT; break; fi; \
	done; \
	if [ $$overall_exit -eq 0 ] && [ $$signature_available -eq 1 ]; then \
		cp "$$sig_file" "$(DOXY_STAMP)"; \
	fi; \
	if [ -n "$$sig_file" ]; then rm -f "$$sig_file"; fi; \
	exit $$overall_exit

.PHONY: $(SUBDIR_TARGETS)
$(SUBDIR_TARGETS) :
	@dir=$(patsubst __subdir__%,%,$@); \
	if [ -f $$dir/makefile ]; then \
		$(call _MAKEFW_RESOLVE_APP_PATHS) \
		if [ "$(SUBDIR_GOAL)" = "default" ]; then \
			echo $(MAKE) -C $$dir; \
			$(MAKE) -C $$dir || exit 1; \
		else \
			echo $(MAKE) -C $$dir $(SUBDIR_GOAL); \
			$(MAKE) -C $$dir $(SUBDIR_GOAL) || exit 1; \
		fi; \
	else \
		:; # echo "Skipping directory '$$dir' (no makefile)"; \
	fi
