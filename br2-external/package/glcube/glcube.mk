################################################################################
#
# glcube
#
################################################################################

GLCUBE_VERSION = 1.0
GLCUBE_SITE = $(BR2_EXTERNAL_TAQ102_PATH)/../src
GLCUBE_SITE_METHOD = local
GLCUBE_LICENSE = MIT
GLCUBE_DEPENDENCIES = stb taq102-fonts libdrm libegl libgles libgbm

define GLCUBE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -Werror -I$(STAGING_DIR)/usr/include -O2 -o $(@D)/glcube \
		$(@D)/glcube.c $(@D)/arcball.c $(@D)/oneeuro.c \
		$(@D)/canvas.c $(@D)/canvas_blend.c $(@D)/font.c $(@D)/status.c $(@D)/statusbar.c $(@D)/accel.c \
		$(@D)/accel_monitor.c $(@D)/touch_flip.c $(@D)/touch_input.c $(@D)/touch_router.c \
		$(@D)/control_center.c $(@D)/control_center_paint.c \
		$(@D)/control_runtime.c $(@D)/control_input.c $(@D)/control_overlay.c \
		$(@D)/settings.c $(@D)/settings_store.c $(@D)/backlight.c $(@D)/power_policy.c $(@D)/sleep_state.c \
		$(@D)/action_worker.c $(@D)/wifi_status.c $(@D)/reboot_target.c \
		$(@D)/display_power.c $(@D)/power_key.c \
		`$(PKG_CONFIG_HOST_BINARY) --cflags --libs libdrm egl glesv2 gbm` -lm -pthread
endef

define GLCUBE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/glcube $(TARGET_DIR)/usr/bin/glcube
endef

$(eval $(generic-package))
