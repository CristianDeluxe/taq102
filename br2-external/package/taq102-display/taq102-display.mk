################################################################################
#
# taq102-display
#
################################################################################

TAQ102_DISPLAY_VERSION = 1.0
TAQ102_DISPLAY_SITE = $(BR2_EXTERNAL_TAQ102_PATH)/../blobs
TAQ102_DISPLAY_SITE_METHOD = local
TAQ102_DISPLAY_LICENSE = GPL-2.0 (driver), MIT (script)

# The combo PHY driver built from our 4.4.167 tree with kernel/patches/0002
# applied, kept as a module because built in it hangs the boot before any
# console exists. Only the 4.4.167 kernel needs it; the stock 4.4.103 has its
# own LVDS driver built in, and the script does nothing there.
define TAQ102_DISPLAY_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/phy-rockchip-inno-video-combo-phy-4.4.167.ko \
		$(TARGET_DIR)/lib/modules/4.4.167/extra/phy-rockchip-inno-video-combo-phy.ko
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_TAQ102_PATH)/package/taq102-display/taq102-display \
		$(TARGET_DIR)/usr/sbin/taq102-display
endef

$(eval $(generic-package))
