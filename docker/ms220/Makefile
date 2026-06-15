ROOT := $(abspath ../..)

.PHONY: all base web distrobox help

help:
	@printf '%s\n' \
	  'The historical docker/ms220 workflow has been replaced.' \
	  'Use the integrated top-level targets:' \
	  '  make -C $(ROOT) all' \
	  '  make -C $(ROOT) base' \
	  '  make -C $(ROOT) web' \
	  '  make -C $(ROOT) distrobox'

all:
	@$(MAKE) -C "$(ROOT)" all

base:
	@$(MAKE) -C "$(ROOT)" base

web:
	@$(MAKE) -C "$(ROOT)" web

distrobox:
	@$(MAKE) -C "$(ROOT)" distrobox
