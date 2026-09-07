################################################################################
#
# rescue-screen
#
################################################################################

RESCUE_SCREEN_VERSION = 1.0
RESCUE_SCREEN_SITE = $(BR2_EXTERNAL_TAQ102_PATH)/../src
RESCUE_SCREEN_SITE_METHOD = local
RESCUE_SCREEN_LICENSE = MIT
RESCUE_SCREEN_DEPENDENCIES = libdrm

define RESCUE_SCREEN_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -o $(@D)/rescue-screen $(@D)/rescue-screen.c \
		$(@D)/canvas.c $(@D)/status.c $(@D)/statusbar.c $(@D)/accel.c \
		`$(PKG_CONFIG_HOST_BINARY) --cflags --libs libdrm` -lm
endef

define RESCUE_SCREEN_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/rescue-screen $(TARGET_DIR)/usr/bin/rescue-screen
endef

$(eval $(generic-package))
