################################################################################
#
# taq102-wifi
#
################################################################################

TAQ102_WIFI_VERSION = 1.0
TAQ102_WIFI_SITE = $(BR2_EXTERNAL_TAQ102_PATH)/../blobs
TAQ102_WIFI_SITE_METHOD = local
TAQ102_WIFI_LICENSE = GPL-2.0 (driver), MIT (script)
TAQ102_WIFI_DEPENDENCIES = wpa_supplicant

# One module per kernel release, because a module only loads against the
# kernel it was built for. 4.4.103 is the stock kernel's vendor binary;
# 4.4.167 is ours, built from source in the qop_kernel tree. The image carries
# both and the script picks by `uname -r`, so one image boots on either kernel.
define TAQ102_WIFI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/8723cs-4.4.103.ko \
		$(TARGET_DIR)/lib/modules/4.4.103/extra/8723cs.ko
	$(INSTALL) -D -m 0644 $(@D)/8723cs-4.4.167.ko \
		$(TARGET_DIR)/lib/modules/4.4.167/extra/8723cs.ko
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_TAQ102_PATH)/package/taq102-wifi/taq102-wifi \
		$(TARGET_DIR)/usr/sbin/taq102-wifi
endef

$(eval $(generic-package))
