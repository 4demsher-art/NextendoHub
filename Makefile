#---------------------------------------------------------------------------------
# NextendoHub for Nintendo Switch homebrew (.nro)
# devkitPro / libnx + Borealis legacy
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO not set")
endif

TOPDIR ?= $(CURDIR)

# libnx's official Switch rules provide switch.h and the NRO/NSO toolchain.
include $(DEVKITPRO)/libnx/switch_rules

TARGET := NextendoHub
BUILD := build

SOURCES := source
DATA := data
ROMFS := romfs

APP_TITLE := Nextendo Hub
APP_AUTHOR := adxmm - Founders of Nextendo Network: JuanBrew, Kazu
APP_VERSION := 1.0.0
ICON := icon.jpg

BOREALIS_PATH := $(TOPDIR)/lib/borealis
BOREALIS_INCLUDE := $(BOREALIS_PATH)/library/include

# Borealis requires this resource path at compile time.
DEFINES += -DBOREALIS_RESOURCES=\"romfs:/\"

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS += -D__SWITCH__
CXXFLAGS := $(CFLAGS) -std=gnu++17
ASFLAGS := -g $(ARCH)

# Dependencies used by the application/Borealis.
LIBS := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz

# Borealis' makefile adds its own source/include/library requirements.
# BOREALIS_PATH must be defined before including it.
include $(BOREALIS_PATH)/library/borealis.mk

LIBS += -lpthread -lnx

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

export LD := $(CXX)

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

# IMPORTANT:
# The recursive make runs with CURDIR=$(TOPDIR)/build. Therefore relative
# include paths must be anchored to TOPDIR, not CURDIR.
export INCLUDE := \
    -I$(TOPDIR)/source \
    -I$(BOREALIS_INCLUDE) \
    -I$(BOREALIS_PATH) \
    -I$(DEVKITPRO)/libnx/include \
    $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
    -I$(TOPDIR)/$(BUILD)

# Borealis legacy's vendored/expected libretro-common headers, if present.
ifneq ($(wildcard $(BOREALIS_PATH)/library/include/libretro-common),)
export INCLUDE += -I$(BOREALIS_PATH)/library/include/libretro-common
endif
ifneq ($(wildcard $(BOREALIS_PATH)/library/libretro-common/include),)
export INCLUDE += -I$(BOREALIS_PATH)/library/libretro-common/include
endif

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
