################################################################################
# postmerkos-hardware
################################################################################
POSTMERKOS_HARDWARE_VERSION = 1.0
POSTMERKOS_HARDWARE_SITE = package/postmerkos-hardware
POSTMERKOS_HARDWARE_SITE_METHOD = local
POSTMERKOS_HARDWARE_DEPENDENCIES = configd fwupdate

define POSTMERKOS_HARDWARE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/files/postmerkos-ledctl $(TARGET_DIR)/usr/sbin/postmerkos-ledctl
	$(INSTALL) -D -m 0755 $(@D)/files/postmerkos-buttond $(TARGET_DIR)/usr/sbin/postmerkos-buttond
	$(INSTALL) -D -m 0755 $(@D)/files/postmerkos-poe-prune $(TARGET_DIR)/usr/sbin/postmerkos-poe-prune
	$(INSTALL) -D -m 0755 $(@D)/files/S16postmerkos-hardware $(TARGET_DIR)/etc/init.d/S16postmerkos-hardware
endef
$(eval $(generic-package))
