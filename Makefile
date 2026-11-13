.PHONY: all launcher launcher-slot2 forwarder bootstrap verify patch-arm9 host libfat

export DIAGNOSTIC ?= 0
export LAUNCH_TRACE ?= 0
export PREPARE_TRACE ?= 0
export SLOT2 ?= 0
export NP3D_VERSION := $(shell git rev-parse HEAD | cut -c1-7)
ifeq ($(GITHUB_REF_TYPE),tag)
export NP3D_VERSION := $(GITHUB_REF_NAME)
else ifeq ($(GITHUB_REF_TYPE),)
NP3D_TAG := $(shell git symbolic-ref -q HEAD >/dev/null || git tag --points-at HEAD | head -n1)
ifneq ($(NP3D_TAG),)
export NP3D_VERSION := $(NP3D_TAG)
endif
endif
export NP3D_LIBFAT := $(CURDIR)/vendor/libfat/nds/lib/libfat.a

all: launcher launcher-slot2 forwarder

patch-arm9:
	$(MAKE) -C source/patch MODE=arm9

host:
	$(MAKE) -C source/patch MODE=host

libfat:
	$(MAKE) -C vendor/libfat nds-release

build/ds_arm9_crt0.o: vendor/devkitarm-crtls/ds_arm9_crt0.s
	mkdir -p build
	$(DEVKITARM)/bin/arm-none-eabi-gcc -g -mthumb-interwork -march=armv5te -c $< -o $@

build/rom-save-types: source/build_save_types.cpp vendor/TWiLightMenu/universal/include/ROMList.h
	mkdir -p build
	g++ $< -o $@

build/rom-save-types.bin: build/rom-save-types
	$< $@

launcher: patch-arm9 bootstrap build/ds_arm9_crt0.o build/rom-save-types.bin
	mkdir -p build
	printf '%s %s\n' "$(DIAGNOSTIC)" "$(LAUNCH_TRACE)" > build/launcher-mode.next
	if ! cmp -s build/launcher-mode.next build/launcher-mode; then $(MAKE) -C vendor/TWiLightMenu/slot1launch clean; fi
	mv build/launcher-mode.next build/launcher-mode
	$(MAKE) -C vendor/TWiLightMenu/universal/bootloader_app LOADBIN=$(CURDIR)/build/np3d_load.bin
	mkdir -p vendor/TWiLightMenu/slot1launch/data build/nitrofs/fonts
	cp build/np3d_load.bin vendor/TWiLightMenu/slot1launch/data/np3d_load.bin
	rm -f vendor/TWiLightMenu/slot1launch/data/np3d_bootstrap.twl.bin
	cp assets/fonts/*.nftr build/nitrofs/fonts/
	python3 source/build_resources.py
	$(MAKE) -C vendor/TWiLightMenu/slot1launch
	mkdir -p out
	cp vendor/TWiLightMenu/slot1launch/slot1launch.nds out/NitroPatcherDSi.nds
	cp vendor/TWiLightMenu/slot1launch/slot1launch.arm9.elf out/NitroPatcherDSi.arm9.elf
	cp vendor/TWiLightMenu/slot1launch/slot1launch.arm7.elf out/NitroPatcherDSi.arm7.elf

launcher-slot2: launcher
	$(MAKE) -C vendor/TWiLightMenu/slot1launch clean
	rm -f build/launcher-mode
	mkdir -p vendor/TWiLightMenu/slot1launch/data build/nitrofs/fonts
	cp build/np3d_load.bin vendor/TWiLightMenu/slot1launch/data/np3d_load.bin
	cp assets/fonts/*.nftr build/nitrofs/fonts/
	python3 source/build_resources.py
	$(MAKE) -C vendor/TWiLightMenu/slot1launch SLOT2=1
	cp vendor/TWiLightMenu/slot1launch/slot1launch.nds out/NitroPatcherDS.nds
	cp vendor/TWiLightMenu/slot1launch/slot1launch.arm9.elf out/NitroPatcherDS.arm9.elf
	cp vendor/TWiLightMenu/slot1launch/slot1launch.arm7.elf out/NitroPatcherDS.arm7.elf

forwarder: launcher-slot2
	$(MAKE) -C source/forwarder

bootstrap: build/lzss libfat
	printf '%s\n' "$(LAUNCH_TRACE)" > build/bootstrap-mode.next
	if ! cmp -s build/bootstrap-mode.next build/bootstrap-mode; then for directory in arm9 bootloader bootloader2 bootloaderi bootloaderi2 cardenginei/arm7 cardenginei/arm7_alt cardenginei/arm7_twlsdk cardenginei/arm7_twlsdk3; do $(MAKE) -C vendor/nds-bootstrap/retail/$$directory clean || exit $$?; done; fi
	mv build/bootstrap-mode.next build/bootstrap-mode
	PATH="$(CURDIR)/build:$$PATH" $(MAKE) -C vendor/nds-bootstrap/retail GIT_VER=eb7feabb
	mkdir -p out
	cp vendor/nds-bootstrap/retail/bin/nds-bootstrap.nds out/nds-bootstrap.nds

build/lzss: vendor/nds-bootstrap/lzss.c
	mkdir -p build
	gcc $< -o $@

verify: all
	mkdir -p temp
	ndstool -i out/NitroPatcherDSi.nds
	ndstool -i out/nds-bootstrap.nds
	ndstool -x out/NitroPatcherDSi.nds -9 temp/packed.arm9.bin -7 temp/packed.arm7.bin
	$(DEVKITARM)/bin/arm-none-eabi-objcopy -O binary --remove-section=.twl --remove-section=.twl_bss out/NitroPatcherDSi.arm9.elf temp/linked.arm9.bin
	$(DEVKITARM)/bin/arm-none-eabi-objcopy -O binary --remove-section=.twl --remove-section=.twl_bss out/NitroPatcherDSi.arm7.elf temp/linked.arm7.bin
	cmp temp/packed.arm9.bin temp/linked.arm9.bin
	cmp temp/packed.arm7.bin temp/linked.arm7.bin
	ndstool -i out/NitroPatcherDS.nds
	ndstool -x out/NitroPatcherDS.nds -9 temp/packed-nds.arm9.bin -7 temp/packed-nds.arm7.bin
	$(DEVKITARM)/bin/arm-none-eabi-objcopy -O binary --remove-section=.twl --remove-section=.twl_bss out/NitroPatcherDS.arm9.elf temp/linked-nds.arm9.bin
	$(DEVKITARM)/bin/arm-none-eabi-objcopy -O binary --remove-section=.twl --remove-section=.twl_bss out/NitroPatcherDS.arm7.elf temp/linked-nds.arm7.bin
	cmp temp/packed-nds.arm9.bin temp/linked-nds.arm9.bin
	cmp temp/packed-nds.arm7.bin temp/linked-nds.arm7.bin
