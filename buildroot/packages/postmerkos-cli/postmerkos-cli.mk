################################################################################
#
# postmerkos-cli
#
################################################################################

POSTMERKOS_CLI_VERSION = 1.0
POSTMERKOS_CLI_SITE = package/postmerkos-cli
POSTMERKOS_CLI_SITE_METHOD = local
POSTMERKOS_CLI_INSTALL_TARGET = YES
POSTMERKOS_CLI_DEPENDENCIES = configd fwupdate jq

define POSTMERKOS_CLI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/files/postmerkos-cli \
		$(TARGET_DIR)/usr/bin/postmerkos-cli
	$(INSTALL) -D -m 0644 $(@D)/files/50-postmerkos-cli.sh \
		$(TARGET_DIR)/etc/profile.d/50-postmerkos-cli.sh
endef

$(eval $(generic-package))
