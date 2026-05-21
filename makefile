# app 直下 makefile テンプレート
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
TESTFW_BANNER = $(TESTFW_HOME)/bin/banner.sh
APPDEPS_RESOLVER = $(MAKEFW_HOME)/bin/resolve_app_deps.sh
DOXY_SIGNATURE_GENERATOR = $(MAKEFW_HOME)/bin/doxy_signature.py
COVERITY_MAKE_WRAPPER = $(MAKEFW_HOME)/bin/cov-build-app.sh
COVERITY_CONFIG = $(CURDIR)/coverity.mk
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
		echo "ERROR: coverity.mk is required for $(CURDIR)/with-cov." >&2; \
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
	if ! CONFIG="$(CONFIG)" MSVC_CRT_SUBDIR="$(MSVC_CRT_SUBDIR)" CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS)" LDFLAGS="$(LDFLAGS)" DEFINES="$(DEFINES)" LIBS="$(LIBS)" bash "$(APPDEPS_RESOLVER)" --signature "$(CURDIR)" build > "$$sig_file"; then \
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
				echo $(MAKE) -C $$dir; \
				$(MAKE) -C $$dir || { make_exit=$$?; break; }; \
			fi; \
		done; \
		if [ $$make_exit -eq 0 ] && [ $$signature_available -eq 1 ] && [ "$$current_clean" = "1" ]; then \
			cp "$$sig_file" "$(BUILD_STAMP)"; \
		fi; \
		if [ -n "$$sig_file" ]; then rm -f "$$sig_file"; fi; \
		if [ $$make_exit -ne 0 ]; then exit $$make_exit; fi; \
	fi

.PHONY: with-cov
with-cov: __ensure-coverity
	@sig_file=$$(mktemp); \
	signature_available=1; \
	if ! CONFIG="$(CONFIG)" MSVC_CRT_SUBDIR="$(MSVC_CRT_SUBDIR)" CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS)" LDFLAGS="$(LDFLAGS)" DEFINES="$(DEFINES)" LIBS="$(LIBS)" bash "$(APPDEPS_RESOLVER)" --signature "$(CURDIR)" build > "$$sig_file"; then \
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
				if [ "$$dir" = "prod" ]; then \
					echo "$(COVERITY_MAKE_WRAPPER)" "$(COVERITY_TOOLCHAIN)" $(MAKE) -C $$dir; \
					"$(COVERITY_MAKE_WRAPPER)" "$(COVERITY_TOOLCHAIN)" $(MAKE) -C $$dir || { make_exit=$$?; break; }; \
				else \
					echo $(MAKE) -C $$dir; \
					$(MAKE) -C $$dir || { make_exit=$$?; break; }; \
				fi; \
			fi; \
		done; \
		if [ $$make_exit -eq 0 ] && [ $$signature_available -eq 1 ] && [ "$$current_clean" = "1" ]; then \
			cp "$$sig_file" "$(BUILD_STAMP)"; \
		fi; \
		if [ -n "$$sig_file" ]; then rm -f "$$sig_file"; fi; \
		if [ $$make_exit -ne 0 ]; then exit $$make_exit; fi; \
	fi

.PHONY: clean
clean : SUBDIR_GOAL = clean
clean : $(SUBDIR_TARGETS)
	@rm -f "$(DOXY_WARN_FILE)" "$(BUILD_STAMP)" "$(TEST_STAMP)" "$(DOXY_STAMP)"
	@rm -f $(CURDIR)/doxy_*.warn

.PHONY: test
test :
	@$(MAKE) $(MFLAGS)
	@if [ -f test/makefile ]; then \
		sig_file=$$(mktemp); \
		signature_available=1; \
		if ! CONFIG="$(CONFIG)" MSVC_CRT_SUBDIR="$(MSVC_CRT_SUBDIR)" CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS)" LDFLAGS="$(LDFLAGS)" DEFINES="$(DEFINES)" LIBS="$(LIBS)" bash "$(APPDEPS_RESOLVER)" --signature "$(CURDIR)" test > "$$sig_file"; then \
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
		echo $(MAKE) -C test test; \
		$(MAKE) -C test test; \
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
