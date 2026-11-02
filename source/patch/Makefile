ROOT := $(abspath ../..)
MODE ?= host
PREPARE_TRACE ?= 0
BUILD := $(ROOT)/build/patch-$(MODE)

COMMON := $(ROOT)/vendor/ndstool/source
MBEDTLS := $(ROOT)/vendor/mbedtls
MINIZ := $(ROOT)/vendor/miniz
XDELTA := $(ROOT)/vendor/xdelta/xdelta3
CJSON := $(ROOT)/vendor/cJSON
MD4C := $(ROOT)/vendor/md4c/src
FMT := $(ROOT)/vendor/fmt

ifeq ($(MODE),arm9)
CC := $(DEVKITARM)/bin/arm-none-eabi-gcc
CXX := $(DEVKITARM)/bin/arm-none-eabi-g++
AR := $(DEVKITARM)/bin/arm-none-eabi-ar
ARCH := -mthumb -mthumb-interwork -march=armv5te -mtune=arm946e-s -DARM9
ICONV_INCLUDE := -I/opt/np3d/libiconv/include
LZMA_INCLUDE := -I/opt/np3d/liblzma/include
NDS_INCLUDE := -I$(DEVKITPRO)/libnds/include
else
CC := gcc
CXX := g++
AR := ar
endif

CPPFLAGS := $(ICONV_INCLUDE) $(LZMA_INCLUDE) $(NDS_INCLUDE) -I$(CURDIR) -I$(COMMON) -I$(MINIZ) -I$(XDELTA) -I$(CJSON) -I$(MBEDTLS)/include -I$(ROOT)/vendor/uni-algo/include
CPPFLAGS += -I$(ROOT)/vendor/BlzHelper/native
CPPFLAGS += -I$(ROOT)/vendor/TWiLightMenu/romsel_dsimenutheme/arm9/source
CPPFLAGS += -DUNI_ALGO_STATIC_DATA
CPPFLAGS += -DNP3D_PREPARE_TRACE=$(PREPARE_TRACE)
CPPFLAGS += -I$(MD4C)
CPPFLAGS += -I$(FMT)/include -DFMT_EXCEPTIONS=0 -DFMT_USE_LOCALE=0
CPPFLAGS += -D_GNU_SOURCE -DNP3D_LIBRARY -DMBEDTLS_CONFIG_FILE='"mbedtls_config.h"'
CPPFLAGS += -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
CPPFLAGS += -DXD3_ENCODER=0 -DXD3_USE_LARGEFILE64=0 -DXD3_USE_LARGESIZET=0 -DHAVE_ALIGNED_ACCESS_REQUIRED=1
CPPFLAGS += -DSECONDARY_DJW=1 -DHAVE_LZMA_H=1
CPPFLAGS += -DSIZEOF_SIZE_T=__SIZEOF_SIZE_T__ -DSIZEOF_UNSIGNED_INT=__SIZEOF_INT__ -DSIZEOF_UNSIGNED_LONG=__SIZEOF_LONG__ -DSIZEOF_UNSIGNED_LONG_LONG=__SIZEOF_LONG_LONG__
CFLAGS := -O2 -g -Wall -ffunction-sections -fdata-sections $(ARCH)
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fexceptions
VPATH := $(CURDIR):$(COMMON):$(MINIZ):$(XDELTA):$(CJSON):$(MBEDTLS)/library:$(MD4C):$(ROOT)/vendor/BlzHelper/native
CFILES := miniz.c miniz_zip.c miniz_tinfl.c cJSON.c xdelta3.c md5.c sha1.c sha256.c md.c aes.c platform_util.c md4c.c entity.c
CPPFILES := core.cpp delta.cpp package.cpp cache.cpp rom.cpp twl.cpp twl_complete.cpp arm9_blz.cpp blz.cpp nds_adapter.cpp ndsextract.cpp fnt.cpp filename.cpp encryption.cpp crc.cpp environment.cpp ui_data.cpp preparation_progress.cpp cheats.cpp
OBJECTS := $(addprefix $(BUILD)/,$(CFILES:.c=.o) $(CPPFILES:.cpp=.o))
OBJECTS += $(BUILD)/format.o
OBJECTS += $(BUILD)/twl_cheats.o

.PHONY: all
all: $(BUILD)/libnp3d.a
ifeq ($(MODE),host)
all: $(ROOT)/out/np3d-prepare
endif

.PHONY: prepare-mode
prepare-mode:

$(BUILD)/prepare-mode: prepare-mode | $(BUILD)
	printf '%s\n' "$(PREPARE_TRACE)" > $(BUILD)/prepare-mode.next
	if cmp -s $(BUILD)/prepare-mode.next $@; then rm $(BUILD)/prepare-mode.next; else mv $(BUILD)/prepare-mode.next $@; fi

$(OBJECTS): $(BUILD)/prepare-mode

ifeq ($(MODE),arm9)
$(BUILD)/md5.o: CFLAGS += -marm
endif

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.c $(CURDIR)/Makefile $(CURDIR)/mbedtls_config.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.cpp $(CURDIR)/Makefile | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/libnp3d.a: $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

$(BUILD)/format.o: $(FMT)/src/format.cc $(CURDIR)/Makefile | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/cheats.o: CPPFLAGS += -DCHEAT_DATA_ONLY
$(BUILD)/twl_cheats.o: $(ROOT)/vendor/TWiLightMenu/romsel_dsimenutheme/arm9/source/cheat.cpp $(CURDIR)/Makefile | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DCHEAT_DATA_ONLY -MMD -MP -c $< -o $@

$(ROOT)/out/np3d-prepare: $(ROOT)/source/host/main.cpp $(BUILD)/libnp3d.a
	mkdir -p $(ROOT)/out
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD)/libnp3d.a -Wl,--gc-sections -llzma -o $@

-include $(OBJECTS:.o=.d)
