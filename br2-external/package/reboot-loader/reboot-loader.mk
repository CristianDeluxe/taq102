################################################################################
#
# reboot-loader
#
################################################################################

REBOOT_LOADER_VERSION = 1.0
REBOOT_LOADER_SITE = $(BR2_EXTERNAL_TAQ102_PATH)/../src
REBOOT_LOADER_SITE_METHOD = local
REBOOT_LOADER_LICENSE = MIT

define REBOOT_LOADER_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -o $(@D)/reboot-loader $(@D)/reboot-loader.c
endef

define REBOOT_LOADER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/reboot-loader $(TARGET_DIR)/usr/sbin/reboot-loader
endef

$(eval $(generic-package))
