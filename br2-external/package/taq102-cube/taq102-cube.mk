################################################################################
#
# taq102-cube
#
################################################################################

TAQ102_CUBE_VERSION = 1.0
TAQ102_CUBE_LICENSE = MIT

# Only the loader script. The modules it loads (phy-rockchip-inno-dsidphy,
# drm_shmem_helper, gpu-sched, lima) are copied into the initramfs by hand from
# the mainline kernel build today; packaging them is an open TODO.
define TAQ102_CUBE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_TAQ102_PATH)/package/taq102-cube/taq102-cube \
		$(TARGET_DIR)/usr/sbin/taq102-cube
endef

$(eval $(generic-package))
