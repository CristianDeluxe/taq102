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

# The stock kernel is 4.4.103 and loads modules from the release it was built
# for; nothing here builds a kernel, so the path is fixed rather than derived.
TAQ102_WIFI_MODULE_DIR = /lib/modules/4.4.103/extra

define TAQ102_WIFI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/8723cs.ko \
		$(TARGET_DIR)$(TAQ102_WIFI_MODULE_DIR)/8723cs.ko
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_TAQ102_PATH)/package/taq102-wifi/taq102-wifi \
		$(TARGET_DIR)/usr/sbin/taq102-wifi
endef

$(eval $(generic-package))
