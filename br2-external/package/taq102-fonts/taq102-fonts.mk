################################################################################
#
# taq102-fonts
#
################################################################################

TAQ102_FONTS_VERSION = 4.1
TAQ102_FONTS_SITE = $(BR2_EXTERNAL_TAQ102_PATH)/package/taq102-fonts/fonts
TAQ102_FONTS_SITE_METHOD = local
TAQ102_FONTS_LICENSE = OFL-1.1
TAQ102_FONTS_LICENSE_FILES = LICENSE-Inter.txt

define TAQ102_FONTS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/Inter-Regular.ttf $(TARGET_DIR)/usr/share/fonts/taq102/Inter-Regular.ttf
	$(INSTALL) -D -m 0644 $(@D)/Inter-SemiBold.ttf $(TARGET_DIR)/usr/share/fonts/taq102/Inter-SemiBold.ttf
	$(INSTALL) -D -m 0644 $(@D)/LICENSE-Inter.txt $(TARGET_DIR)/usr/share/fonts/taq102/LICENSE-Inter.txt
endef

$(eval $(generic-package))
