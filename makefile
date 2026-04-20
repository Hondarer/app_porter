SUBDIRS = \
	prod \
	test

APP_NAME = $(notdir $(CURDIR))
DOXYFW_DIR = ../../framework/doxyfw
TESTFW_BANNER = ../../framework/testfw/bin/banner.sh
DOXY_WARN_FILE = $(CURDIR)/doxy.warn
SUBDIR_TARGETS = $(addprefix __subdir__,$(SUBDIRS))

.DEFAULT_GOAL := default

.PHONY: default
default : SUBDIR_GOAL = default
default : $(SUBDIR_TARGETS)

.PHONY: clean
clean : SUBDIR_GOAL = clean
clean : $(SUBDIR_TARGETS)
	@rm -f "$(DOXY_WARN_FILE)"

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
			echo $(MAKE) -C $(DOXYFW_DIR) CATEGORY=$(APP_NAME); \
			rm -f "$(DOXY_WARN_FILE)"; \
			$(MAKE) -C $(DOXYFW_DIR) CATEGORY=$(APP_NAME); \
			MAKE_EXIT=$$?; \
			if [ -s "$(DOXY_WARN_FILE)" ]; then \
				printf '\n'; \
				bash "$(TESTFW_BANNER)" WARNING "\e[33m"; \
				printf '\n'; \
				printf '\033[33m===== %s =====\033[0m\n' "$(DOXY_WARN_FILE)"; \
				while IFS= read -r line || [ -n "$$line" ]; do \
					clean_line=$$(printf '%s' "$$line" | tr -d '\r'); \
					printf '\033[33m%s\033[0m\n' "$$clean_line"; \
				done < "$(DOXY_WARN_FILE)"; \
			fi; \
			if [ $$MAKE_EXIT -ne 0 ]; then exit $$MAKE_EXIT; fi; \
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
