################################################################################
#
# glcube
#
################################################################################

GLCUBE_VERSION = 1.0
GLCUBE_SITE = $(BR2_EXTERNAL_TAQ102_PATH)/../src
GLCUBE_SITE_METHOD = local
GLCUBE_LICENSE = MIT
GLCUBE_DEPENDENCIES = libdrm libegl libgles libgbm

define GLCUBE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -o $(@D)/glcube \
		$(@D)/glcube.c $(@D)/arcball.c $(@D)/oneeuro.c \
		$(@D)/canvas.c $(@D)/status.c $(@D)/statusbar.c $(@D)/accel.c \
		$(@D)/accel_monitor.c $(@D)/touch_flip.c \
		`$(PKG_CONFIG_HOST_BINARY) --cflags --libs libdrm egl glesv2 gbm` -lm -pthread
endef

define GLCUBE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/glcube $(TARGET_DIR)/usr/bin/glcube
endef

$(eval $(generic-package))
