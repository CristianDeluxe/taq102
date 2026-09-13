################################################################################
#
# taq102-diag
#
################################################################################

TAQ102_DIAG_VERSION = 1.0
TAQ102_DIAG_SITE = $(BR2_EXTERNAL_TAQ102_PATH)/../src
TAQ102_DIAG_SITE_METHOD = local
TAQ102_DIAG_LICENSE = MIT
TAQ102_DIAG_DEPENDENCIES = libdrm

TAQ102_DIAG_TOOLS = testpattern phytune lvdsdiag touchsim fliptest

define TAQ102_DIAG_BUILD_CMDS
	$(foreach t,$(TAQ102_DIAG_TOOLS), \
		$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -Werror -O2 -o $(@D)/$(t) $(@D)/$(t).c \
			`$(PKG_CONFIG_HOST_BINARY) --cflags --libs libdrm` -lm
	)
endef

define TAQ102_DIAG_INSTALL_TARGET_CMDS
	$(foreach t,$(TAQ102_DIAG_TOOLS), \
		$(INSTALL) -D -m 0755 $(@D)/$(t) $(TARGET_DIR)/usr/bin/$(t)
	)
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_TAQ102_PATH)/package/taq102-diag/panel-trap $(TARGET_DIR)/usr/bin/panel-trap
endef

$(eval $(generic-package))
