################################################################################
#
# pd690xx
#
################################################################################

PD690XX_VERSION = 0.42
PD690XX_SITE = package/pd690xx
PD690XX_SITE_METHOD = local
PD690XX_INSTALL_TARGET = YES

define PD690XX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CPPFLAGS="$(TARGET_CPPFLAGS)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define PD690XX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pd690xx $(TARGET_DIR)/bin/pd690xx
	$(INSTALL) -D -m 0755 $(@D)/libpd690xx.so $(TARGET_DIR)/lib/libpd690xx.so
endef

$(eval $(generic-package))
