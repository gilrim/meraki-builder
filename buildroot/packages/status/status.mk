################################################################################
#
# status (clickswstatus)
#
################################################################################

STATUS_VERSION = 0.1
STATUS_SITE = package/status
STATUS_SITE_METHOD = local
STATUS_INSTALL_TARGET = YES
STATUS_DEPENDENCIES = json-c pd690xx

define STATUS_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CPPFLAGS="$(TARGET_CPPFLAGS)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		PD690XX_DIR="$(TOPDIR)/package/pd690xx" \
		POSTMERKOS_DIR="$(TOPDIR)/package/postmerkos"
endef

define STATUS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/clickswstatus $(TARGET_DIR)/bin/clickswstatus
endef

$(eval $(generic-package))
