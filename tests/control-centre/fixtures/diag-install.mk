# Exercise the package's install recipe without compiling the hardware tools.
BR2_EXTERNAL_TAQ102_PATH := $(CURDIR)/br2-external
TARGET_DIR := $(TEST_OUT)/diag-target
# Buildroot uses GNU install; macOS supplies it as ginstall.
INSTALL := $(shell command -v ginstall 2>/dev/null || command -v install)
include br2-external/package/taq102-diag/taq102-diag.mk

ifneq ($(filter panel-trap,$(TAQ102_DIAG_TOOLS)),)
$(error panel-trap is a script, not a compiled diagnostic)
endif

.PHONY: install-test
install-test:
	mkdir -p $(TEST_OUT)/diag-build $(TARGET_DIR)/usr/bin
	$(foreach t,$(TAQ102_DIAG_TOOLS),touch $(TEST_OUT)/diag-build/$(t);)
	$(MAKE) --no-print-directory -f tests/control-centre/fixtures/diag-install.mk $(TEST_OUT)/diag-build/install

$(TEST_OUT)/diag-build/install:
	$(TAQ102_DIAG_INSTALL_TARGET_CMDS)
