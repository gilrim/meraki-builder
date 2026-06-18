################################################################################
# postmerkos-hardware
################################################################################
POSTMERKOS_HARDWARE_VERSION = 1.2
POSTMERKOS_HARDWARE_SITE = package/postmerkos-hardware
POSTMERKOS_HARDWARE_SITE_METHOD = local
POSTMERKOS_HARDWARE_DEPENDENCIES = configd fwupdate json-c

define POSTMERKOS_HARDWARE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) \
		-o $(@D)/postmerkos-buttond $(@D)/buttond.c \
		$(TARGET_LDFLAGS) -ljson-c
endef

define POSTMERKOS_HARDWARE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/files/postmerkos-ledctl $(TARGET_DIR)/usr/sbin/postmerkos-ledctl
	$(INSTALL) -D -m 0755 $(@D)/files/postmerkos-hwprobe $(TARGET_DIR)/usr/sbin/postmerkos-hwprobe
	$(INSTALL) -D -m 0755 $(@D)/postmerkos-buttond $(TARGET_DIR)/usr/sbin/postmerkos-buttond
	$(INSTALL) -D -m 0755 $(@D)/files/postmerkos-poe-prune $(TARGET_DIR)/usr/sbin/postmerkos-poe-prune
	$(INSTALL) -D -m 0755 $(@D)/files/S16postmerkos-hardware $(TARGET_DIR)/etc/init.d/S16postmerkos-hardware
	$(INSTALL) -D -m 0644 $(@D)/files/hardware-controls-default.conf \
		$(TARGET_DIR)/usr/share/postmerkos/hardware-controls/default.conf
endef
$(eval $(generic-package))
