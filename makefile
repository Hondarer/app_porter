# app 直下 makefile テンプレート
# すべての app/<app_name>/makefile で使用する標準テンプレート
# 本ファイルの直接編集は禁止する。

SUBDIRS = \
	prod \
	test

APP_NAME = $(notdir $(CURDIR))
MAKEFILE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
WORKSPACE_DIR ?= $(abspath $(MAKEFILE_DIR)/../..)
MAKEFW_HOME := $(strip $(MAKEFW_HOME))
ifeq ($(MAKEFW_HOME),)
    $(error MAKEFW_HOME is required. Export MAKEFW_HOME before running make)
endif
TESTFW_HOME   ?= $(WORKSPACE_DIR)/framework/testfw
TESTFW_BANNER = $(TESTFW_HOME)/bin/banner.sh
APPDEPS_RESOLVER = $(MAKEFW_HOME)/bin/resolve_app_deps.sh
DOXY_WARN_FILE = $(CURDIR)/doxy.warn
BUILD_LOG = $(CURDIR)/make_build.log
TEST_LOG = $(CURDIR)/make_test.log
DOXY_LOG  = $(CURDIR)/make_doxy.log
SUBDIR_TARGETS = $(addprefix __subdir__,$(SUBDIRS))

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

.PHONY: default
default:
	@sig_file=$$(mktemp); \
	if ! MSVC_CRT_SUBDIR="$(MSVC_CRT_SUBDIR)" bash "$(APPDEPS_RESOLVER)" --signature "$(CURDIR)" > "$$sig_file"; then \
		rm -f "$$sig_file"; \
		exit 1; \
	fi; \
	if [ -f "$(BUILD_LOG)" ] && [ -n "$(MSVC_CRT_SUBDIR)" ]; then \
		prev_crt=$$(sed -n 's/^MSVC_CRT=//p' "$(BUILD_LOG)"); \
		if [ -n "$$prev_crt" ] && [ "$$prev_crt" != "$(MSVC_CRT_SUBDIR)" ]; then \
			rm -f "$$sig_file"; \
			echo "ERROR: MSVC runtime mismatch detected. Run 'make clean' first, then rebuild.  Previous build: $$prev_crt  Current request: $(MSVC_CRT_SUBDIR)" >&2; \
			exit 1; \
		fi; \
	fi; \
	current_clean=$$(sed -n '1s/^CLEAN=//p' "$$sig_file"); \
	if [ "$$current_clean" = "1" ] && [ -f "$(BUILD_LOG)" ] && cmp -s "$$sig_file" "$(BUILD_LOG)"; then \
		echo "INFO: Skipping build (dependencies are unchanged and clean)"; \
		rm -f "$$sig_file"; \
	else \
		rm -f "$(BUILD_LOG)"; \
		make_exit=0; \
		for dir in $(SUBDIRS); do \
			if [ -f $$dir/makefile ]; then \
				echo $(MAKE) -C $$dir; \
				$(MAKE) -C $$dir || { make_exit=$$?; break; }; \
			fi; \
		done; \
		if [ $$make_exit -eq 0 ] && [ "$$current_clean" = "1" ]; then \
			cp "$$sig_file" "$(BUILD_LOG)"; \
		fi; \
		rm -f "$$sig_file"; \
		if [ $$make_exit -ne 0 ]; then exit $$make_exit; fi; \
	fi

.PHONY: clean
clean : SUBDIR_GOAL = clean
clean : $(SUBDIR_TARGETS)
	@rm -f "$(DOXY_WARN_FILE)" "$(BUILD_LOG)" "$(TEST_LOG)" "$(DOXY_LOG)"

.PHONY: test
test :
	@if [ -f test/makefile ]; then \
		sig_file=$$(mktemp); \
		if ! MSVC_CRT_SUBDIR="$(MSVC_CRT_SUBDIR)" bash "$(APPDEPS_RESOLVER)" --signature "$(CURDIR)" > "$$sig_file"; then \
			rm -f "$$sig_file"; \
			exit 1; \
		fi; \
		if [ -f "$(TEST_LOG)" ] && [ -n "$(MSVC_CRT_SUBDIR)" ]; then \
			prev_crt=$$(sed -n 's/^MSVC_CRT=//p' "$(TEST_LOG)"); \
			if [ -n "$$prev_crt" ] && [ "$$prev_crt" != "$(MSVC_CRT_SUBDIR)" ]; then \
				rm -f "$$sig_file"; \
				echo "ERROR: MSVC runtime mismatch detected. Run 'make clean' first, then rebuild.  Previous build: $$prev_crt  Current request: $(MSVC_CRT_SUBDIR)" >&2; \
				exit 1; \
			fi; \
		fi; \
		current_clean=$$(sed -n '1s/^CLEAN=//p' "$$sig_file"); \
		if [ "$$current_clean" = "1" ] && [ -f "$(TEST_LOG)" ] && cmp -s "$$sig_file" "$(TEST_LOG)"; then \
			echo "INFO: Skipping test (dependencies are unchanged and clean)"; \
			rm -f "$$sig_file"; \
			exit 0; \
		fi; \
		rm -f "$(TEST_LOG)"; \
		echo $(MAKE) -C test test; \
		$(MAKE) -C test test; \
		make_exit=$$?; \
		if [ $$make_exit -eq 0 ] && [ "$$current_clean" = "1" ]; then \
			cp "$$sig_file" "$(TEST_LOG)"; \
		fi; \
		rm -f "$$sig_file"; \
		if [ $$make_exit -ne 0 ]; then exit $$make_exit; fi; \
	else \
		:; # echo "Skipping directory 'test' (no makefile)"; \
	fi

.PHONY: doxy
doxy :
	@if [ -f Doxyfile.part ]; then \
		if [ -z "$(DOXYFW_HOME)" ]; then \
			echo "ERROR: DOXYFW_HOME is not defined."; \
			exit 1; \
		fi; \
		if [ -d "$(DOXYFW_HOME)" ] && [ -f "$(DOXYFW_HOME)/makefile" ]; then \
			git_hash=$$(git -C "$(CURDIR)" rev-parse HEAD 2>/dev/null); \
			git_dirty=$$(git -C "$(CURDIR)" status --porcelain --untracked-files=no 2>/dev/null); \
			if [ -n "$$git_hash" ] && [ -z "$$git_dirty" ] && \
			   [ -f "$(DOXY_LOG)" ] && [ "$$(cat '$(DOXY_LOG)')" = "$$git_hash" ]; then \
				echo "INFO: Skipping doxy (already generated at $$git_hash)"; \
			else \
				rm -f "$(DOXY_LOG)"; \
				echo $(MAKE) -C "$(DOXYFW_HOME)" CATEGORY=$(APP_NAME); \
				rm -f "$(DOXY_WARN_FILE)"; \
				$(MAKE) -C "$(DOXYFW_HOME)" CATEGORY=$(APP_NAME); \
				MAKE_EXIT=$$?; \
				if [ -z "$(SUPPRESS_DOXY_WARN_PRINT)" ] && [ -s "$(DOXY_WARN_FILE)" ]; then \
					printf '\n'; \
					bash "$(TESTFW_BANNER)" WARNING "\e[33m"; \
					printf '\n'; \
					printf '\033[33m===== %s =====\033[0m\n' "$(DOXY_WARN_FILE)"; \
					while IFS= read -r line || [ -n "$$line" ]; do \
						clean_line=$$(printf '%s' "$$line" | tr -d '\r'); \
						printf '\033[33m%s\033[0m\n' "$$clean_line"; \
					done < "$(DOXY_WARN_FILE)"; \
				fi; \
				if [ $$MAKE_EXIT -eq 0 ] && [ -n "$$git_hash" ] && [ -z "$$git_dirty" ]; then \
					echo "$$git_hash" > "$(DOXY_LOG)"; \
				fi; \
				if [ $$MAKE_EXIT -ne 0 ]; then exit $$MAKE_EXIT; fi; \
			fi; \
		else \
			:; # echo "INFO: $(DOXYFW_HOME) directory not found, skipping."; \
		fi; \
	else \
		:; # echo "INFO: Doxygen is not configured for $(APP_NAME), skipping."; \
	fi

.PHONY: $(SUBDIR_TARGETS)
$(SUBDIR_TARGETS) :
	@dir=$(patsubst __subdir__%,%,$@); \
	if [ -f $$dir/makefile ]; then \
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
