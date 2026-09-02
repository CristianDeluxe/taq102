################################################################################
#
# mali-utgard
#
################################################################################

# Same mirror and commit as Buildroot's rockchip-mali package; only the blob
# picked out of it differs. The tarball name drops nothing, so it cannot
# collide with rockchip-mali's own download.
MALI_UTGARD_VERSION = 721653b5b3b525a4f80d15aa7e2f9df7b7e60427
MALI_UTGARD_SITE = $(call github,JeffyCN,mirrors,$(MALI_UTGARD_VERSION))
MALI_UTGARD_LICENSE = Proprietary
MALI_UTGARD_LICENSE_FILES = END_USER_LICENCE_AGREEMENT.txt
MALI_UTGARD_INSTALL_STAGING = YES
# The blob leaves six OpenSSL symbols undefined -- BN_bin2bn, BN_new,
# BN_set_word, RSA_new, RSA_public_decrypt, RSA_size -- because on Android
# they came from the process's own libcrypto. Nothing else resolves them
# here, so libcrypto has to be a real dependency and a real DT_NEEDED.
MALI_UTGARD_DEPENDENCIES = host-patchelf libdrm openssl
MALI_UTGARD_PROVIDES = libegl libgles libgbm

# Mali-400 MP2, r7p0 — the version Android on this tablet reports in
# ro.hardware.egl (blob r7p0-00rel1-5-25). The GBM flavour, because there is
# no X11 and no Wayland here: one process owns KMS.
MALI_UTGARD_LIB = libmali-utgard-400-r7p0-gbm.so
MALI_UTGARD_PKGCONFIG_FILES = egl gbm glesv2 mali
MALI_UTGARD_HEADERS = EGL GLES GLES2 GLES3 KHR gbm.h

# The blob carries no SONAME at all, and everything links against libmali.so.1.
MALI_UTGARD_LIB_SYMLINKS = \
	libmali.so.1 \
	libMali.so \
	libEGL.so \
	libEGL.so.1 \
	libgbm.so \
	libgbm.so.1 \
	libGLESv1_CM.so \
	libGLESv2.so \
	libGLESv2.so.2

define MALI_UTGARD_INSTALL_CMDS
	$(INSTALL) -D -m 0755 \
		$(@D)/lib/arm-linux-gnueabihf/$(MALI_UTGARD_LIB) \
		$(1)/usr/lib/$(MALI_UTGARD_LIB)

	$(HOST_DIR)/bin/patchelf --set-soname libmali.so.1 \
		$(1)/usr/lib/$(MALI_UTGARD_LIB)
	$(HOST_DIR)/bin/patchelf --add-needed libcrypto.so.3 \
		$(1)/usr/lib/$(MALI_UTGARD_LIB)

	mkdir -p $(1)/usr/lib/pkgconfig
	$(foreach pkgconfig,$(MALI_UTGARD_PKGCONFIG_FILES), \
		sed -e 's%@CMAKE_INSTALL_LIBDIR@%lib%;s%@CMAKE_INSTALL_INCLUDEDIR@%include%' \
			$(@D)/pkgconfig/$(pkgconfig).pc.cmake > \
			$(1)/usr/lib/pkgconfig/$(pkgconfig).pc
	)

	$(foreach d,$(MALI_UTGARD_HEADERS), \
		cp -dpfr $(@D)/include/$(d) $(1)/usr/include/
	)

	$(foreach symlink,$(MALI_UTGARD_LIB_SYMLINKS), \
		ln -sf $(MALI_UTGARD_LIB) $(1)/usr/lib/$(symlink)
	)
endef

define MALI_UTGARD_INSTALL_TARGET_CMDS
	$(call MALI_UTGARD_INSTALL_CMDS,$(TARGET_DIR))
endef

define MALI_UTGARD_INSTALL_STAGING_CMDS
	$(call MALI_UTGARD_INSTALL_CMDS,$(STAGING_DIR))
endef

$(eval $(generic-package))
