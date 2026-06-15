SHELL := /usr/bin/env bash
.DEFAULT_GOAL := help

JOBS ?= $(shell nproc 2>/dev/null || echo 1)
export JOBS

.PHONY: help all base web doctor deps sources kernel donor ui prepare rootfs image \
        validate verify-inputs menuconfig distrobox clean distclean print-config

help:
	@printf '%s\n' \
	  'MS42P/postmerkOS build targets' \
	  '' \
	  '  make all          Interactive complete build; prompts for optional UI' \
	  '  make base         Complete build without the web UI' \
	  '  make web          Complete build with Gadorach/postmerkos-ui ms42p-dev' \
	  '  make doctor       Check host tools and local build state' \
	  '  make deps         Install dependencies for the current distribution' \
	  '  make sources      Clone/select the pinned kernel and OpenWrt source' \
	  '  make kernel       Build the OpenWrt toolchain and Linux 3.18 kernel' \
	  '  make donor        Download/extract donor modules and RedBoot as needed' \
	  '  make ui           Clone/update and compile postmerkos-ui' \
	  '  make verify-inputs Validate and hash all local binary build inputs' \
	  '  make prepare      Download/configure Buildroot and generated overlays' \
	  '  make rootfs       Build SquashFS and the complete 16 MiB NOR image' \
	  '  make validate     Validate the most recent image and rootfs contents' \
	  '  make menuconfig   Open Buildroot menuconfig after preparation' \
	  '  make distrobox    Run the complete build in Ubuntu 22.04 distrobox' \
	  '  make clean        Remove generated Buildroot output, keep downloads' \
	  '  make distclean    Remove .work and artifacts completely' \
	  '' \
	  'Useful variables:' \
	  '  JOBS=8 INCLUDE_UI=1 DONOR_IMAGE=/path/file.bin' \
	  '  USE_DISTROBOX=1 NONINTERACTIVE=1 AUTO_DOWNLOAD_DONOR=1'

all:
	@./scripts/build-all.sh

base:
	@INCLUDE_UI=0 ./scripts/build-all.sh

web:
	@INCLUDE_UI=1 ./scripts/build-all.sh

doctor:
	@./scripts/doctor.sh

deps:
	@./scripts/install-deps.sh

sources:
	@./scripts/prepare-sources.sh

kernel:
	@./scripts/build-kernel.sh

donor:
	@./scripts/prepare-donor.sh

ui:
	@./scripts/build-ui.sh

verify-inputs:
	@./scripts/verify-inputs.sh

prepare:
	@./scripts/prepare-buildroot.sh

rootfs:
	@./scripts/build-rootfs.sh

image:
	@./scripts/build-rootfs.sh
	@./scripts/validate-image.sh

validate:
	@./scripts/validate-image.sh

menuconfig: prepare
	@bash -c 'source ./scripts/common.sh; $(MAKE) -C "$$BUILDROOT_DIR" menuconfig'

distrobox:
	@./scripts/distrobox-run.sh env INCLUDE_UI="$${INCLUDE_UI:-ask}" ./scripts/build-all.sh

clean:
	@./scripts/clean.sh build

distclean:
	@./scripts/clean.sh all

print-config:
	@./scripts/doctor.sh || true
	@printf '\nBuildroot config: %s\n' '.work/build/buildroot-2023.02.4/.config'
