################################################################################
#
# particles
#
################################################################################

PARTICLES_VERSION = 1.0
PARTICLES_SITE = $(BR2_EXTERNAL_TAQ102_PATH)/../src
PARTICLES_SITE_METHOD = local
PARTICLES_LICENSE = MIT
PARTICLES_DEPENDENCIES = libdrm

define PARTICLES_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -o $(@D)/particles $(@D)/particles.c \
		`$(PKG_CONFIG_HOST_BINARY) --cflags --libs libdrm` -lm
endef

define PARTICLES_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/particles $(TARGET_DIR)/usr/bin/particles
endef

$(eval $(generic-package))
