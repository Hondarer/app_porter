# app 直下 makefile テンプレート
# すべての app/<app_name>/makefile で使用する標準テンプレート
# 本ファイルの直接編集は禁止する。

SUBDIRS = \
	prod \
	test

APP_NAME = $(notdir $(CURDIR))
DOXYFW_DIR = ../../framework/doxyfw
TESTFW_BANNER = ../../framework/testfw/bin/banner.sh
DOXY_WARN_FILE = $(CURDIR)/doxy.warn
BUILD_LOG = $(CURDIR)/make_build.log
DOXY_LOG  = $(CURDIR)/make_doxy.log
SUBDIR_TARGETS = $(addprefix __subdir__,$(SUBDIRS))

.DEFAULT_GOAL := default

.PHONY: default
default:
	@git_hash=$$(git -C "$(CURDIR)" rev-parse HEAD 2>/dev/null); \
	git_dirty=$$(git -C "$(CURDIR)" status --porcelain --untracked-files=no 2>/dev/null); \
	if [ -n "$$git_hash" ] && [ -z "$$git_dirty" ] && \
	   [ -f "$(BUILD_LOG)" ] && [ "$$(cat '$(BUILD_LOG)')" = "$$git_hash" ]; then \
		echo "INFO: Skipping build (already built at $$git_hash)"; \
	else \
		rm -f "$(BUILD_LOG)"; \
		for dir in $(SUBDIRS); do \
			if [ -f $$dir/makefile ]; then \
				echo $(MAKE) -C $$dir; \
				$(MAKE) -C $$dir || exit 1; \
			fi; \
		done; \
		if [ -n "$$git_hash" ] && [ -z "$$git_dirty" ]; then \
			echo "$$git_hash" > "$(BUILD_LOG)"; \
		fi; \
	fi

.PHONY: clean
clean : SUBDIR_GOAL = clean
clean : $(SUBDIR_TARGETS)
	@rm -f "$(DOXY_WARN_FILE)" "$(BUILD_LOG)" "$(DOXY_LOG)"

.PHONY: test
test :
	@if [ -f test/makefile ]; then \
		echo $(MAKE) -C test test; \
		$(MAKE) -C test test || exit 1; \
	else \
		:; # echo "Skipping directory 'test' (no makefile)"; \
	fi

.PHONY: doxy
doxy :
	@if [ -f Doxyfile.part ]; then \
		if [ -d $(DOXYFW_DIR) ] && [ -f $(DOXYFW_DIR)/makefile ]; then \
			git_hash=$$(git -C "$(CURDIR)" rev-parse HEAD 2>/dev/null); \
			git_dirty=$$(git -C "$(CURDIR)" status --porcelain --untracked-files=no 2>/dev/null); \
			if [ -n "$$git_hash" ] && [ -z "$$git_dirty" ] && \
			   [ -f "$(DOXY_LOG)" ] && [ "$$(cat '$(DOXY_LOG)')" = "$$git_hash" ]; then \
				echo "INFO: Skipping doxy (already generated at $$git_hash)"; \
			else \
				rm -f "$(DOXY_LOG)"; \
				echo $(MAKE) -C $(DOXYFW_DIR) CATEGORY=$(APP_NAME); \
				rm -f "$(DOXY_WARN_FILE)"; \
				$(MAKE) -C $(DOXYFW_DIR) CATEGORY=$(APP_NAME); \
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
			:; # echo "INFO: $(DOXYFW_DIR) directory not found, skipping."; \
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
