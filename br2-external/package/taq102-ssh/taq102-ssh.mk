################################################################################
#
# taq102-ssh
#
################################################################################

TAQ102_SSH_VERSION = 1.0
TAQ102_SSH_SITE = $(BR2_EXTERNAL_TAQ102_PATH)/package/taq102-ssh
TAQ102_SSH_SITE_METHOD = local
TAQ102_SSH_LICENSE = MIT
TAQ102_SSH_DEPENDENCIES = dropbear

define TAQ102_SSH_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/taq102-ssh $(TARGET_DIR)/usr/sbin/taq102-ssh
endef

$(eval $(generic-package))
