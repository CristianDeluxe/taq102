################################################################################
#
# dmxdesk
#
################################################################################

DMXDESK_VERSION = 1.0
DMXDESK_SITE = $(BR2_EXTERNAL_TAQ102_PATH)/../src
DMXDESK_SITE_METHOD = local
DMXDESK_LICENSE = MIT
DMXDESK_DEPENDENCIES = libdrm stb taq102-fonts
# Same pinned cJSON as the standalone build: no new runtime .so for old ramdisks.
# Snapshot the non-src assets with the local sources, including on a re-rsync.
define DMXDESK_COPY_ASSETS
	mkdir -p $(@D)/include/cjson
	cp $(BR2_EXTERNAL_TAQ102_PATH)/../tools/vendor/cjson/cJSON.h $(@D)/include/cjson/
	cp $(BR2_EXTERNAL_TAQ102_PATH)/../tools/vendor/cjson/cJSON.c $(@D)/
	cp $(BR2_EXTERNAL_TAQ102_PATH)/../show/vibra.desk.json $(@D)/
	cp $(BR2_EXTERNAL_TAQ102_PATH)/package/dmxdesk/taq102-desk $(@D)/
endef
DMXDESK_POST_RSYNC_HOOKS += DMXDESK_COPY_ASSETS

define DMXDESK_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -w -c $(@D)/cJSON.c -I$(@D)/include/cjson -o $(@D)/cJSON.o
	$(TARGET_CC) $(TARGET_CFLAGS) -std=gnu99 -Wall -Wextra -Werror -O2 \
		-I$(@D) -I$(@D)/include -I$(STAGING_DIR)/usr/include/libdrm \
		-o $(@D)/dmxdesk $(addprefix $(@D)/,$(shell cat $(@D)/dmxdesk.sources)) \
		$(@D)/cJSON.o $(TARGET_LDFLAGS) -ldrm -lm -pthread
endef

define DMXDESK_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/dmxdesk $(TARGET_DIR)/usr/bin/dmxdesk
	$(INSTALL) -D -m 0644 $(@D)/vibra.desk.json $(TARGET_DIR)/usr/share/dmxdesk/vibra.desk.json
	$(INSTALL) -D -m 0755 $(@D)/taq102-desk $(TARGET_DIR)/usr/bin/taq102-desk
endef

$(eval $(generic-package))
