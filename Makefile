#---------------------------------------------------------------------------------
# NextendoHub - Nintendo Switch homebrew
# Based on the standard devkitPro/libnx Makefile layout.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment")
endif

TOPDIR ?= $(CURDIR)

# Standard libnx Switch rules. This supplies switch.h and the NRO linker rules.
include $(DEVKITPRO)/libnx/switch_rules

TARGET := NextendoHub
BUILD := build

SOURCES := source
DATA := data
INCLUDES := include

ROMFS := romfs
ICON := icon.jpg

BOREALIS_PATH := $(TOPDIR)/lib/borealis

# Borealis legacy expects this compile-time resource definition.
DEFINES += -DBOREALIS_RESOURCES=\"romfs:/\"

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS := -g -Wall -O2 -ffunction-sections $(ARCH)
CFLAGS += $(DEFINES) $(INCLUDE) -D__SWITCH__

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS := -g $(ARCH)

# Libraries required by NextendoHub/Borealis.
LIBS := -lnx -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz

# Borealis must be included after LIBDIRS and BOREALIS_PATH are defined.
include $(BOREALIS_PATH)/library/borealis.mk

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(TOPDIR)

export VPATH := $(foreach dir,$(SOURCES),$(TOPDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(TOPDIR)/$(dir))

export DEPSDIR := $(TOPDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.*)))

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

# These are the directories actually passed to gcc/g++.
# libnx: switch.h
# Borealis: borealis.hpp
# libretro-common: libretro-common/features/features_cpu.h
export INCLUDE := \
    $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
    -I$(TOPDIR)/source \
    -I$(TOPDIR)/lib/borealis/library/include \
    -I$(TOPDIR)/lib/borealis \
    -I$(TOPDIR)/lib \
    -I$(DEVKITPRO)/libnx/include \
    $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
    -I$(CURDIR)/$(BUILD)

export CXXFLAGS := $(CXXFLAGS) $(DEFINES) $(INCLUDE)
export CFLAGS := $(CFLAGS) $(DEFINES) $(INCLUDE)

export LIBPATHS := \
    -L$(DEVKITPRO)/libnx/lib \
    $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON := $(TOPDIR)/$(ICON)
export NROFLAGS += --icon=$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp

ifneq ($(ROMFS),)
export NROFLAGS += --romfsdir=$(TOPDIR)/$(ROMFS)
endif

.PHONY: all clean $(BUILD)

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $@ -f $(TOPDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else

.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp

$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC): $(HFILES_BIN)

%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
