################################################################################
#
# findhdr
#
################################################################################

FINDHDR_VERSION = 0.1
FINDHDR_SITE = package/findhdr
FINDHDR_SITE_METHOD = local
FINDHDR_INSTALL_TARGET = YES

define FINDHDR_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CPPFLAGS="$(TARGET_CPPFLAGS)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define FINDHDR_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/find_hdr $(TARGET_DIR)/bin/find_hdr
endef

$(eval $(generic-package))
