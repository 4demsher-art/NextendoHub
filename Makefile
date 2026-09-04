# NextendoHub Nintendo Switch NRO build
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set)
endif

TOPDIR := $(CURDIR)
TARGET := NextendoHub
BUILD := build
SOURCES := source
ROMFS := romfs
ICON := icon.jpg

# Standard Switch/libnx rules.
include $(DEVKITPRO)/libnx/switch_rules

BOREALIS_PATH := $(TOPDIR)/lib/borealis
PORTLIBS_SWITCH := $(DEVKITPRO)/portlibs/switch

# Explicit include paths. The recursive make below runs from build/,
# so every project/dependency path is anchored at TOPDIR.
PROJECT_INCLUDES := \
	-I$(TOPDIR)/source \
	-I$(BOREALIS_PATH)/library/include \
	-I$(BOREALIS_PATH)/library/include/libretro-common \
	-I$(DEVKITPRO)/libnx/include \
	-I$(PORTLIBS_SWITCH)/include

# Borealis needs this at compile time.
DEFINES := -DBOREALIS_RESOURCES=\"romfs:/\"

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES) $(PROJECT_INCLUDES) -D__SWITCH__
CXXFLAGS := $(CFLAGS) -std=gnu++17
ASFLAGS := -g $(ARCH)

# Portlib locations supplied by devkitpro/devkita64.
LIBDIRS := -L$(PORTLIBS_SWITCH)/lib -L$(DEVKITPRO)/libnx/lib
LIBS := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz

# Borealis adds its own sources and libraries.
include $(BOREALIS_PATH)/library/borealis.mk

LIBS += -lpthread -lnx

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CPPFILES := $(notdir $(wildcard $(CURDIR)/source/*.cpp))
CFILES := $(notdir $(wildcard $(CURDIR)/source/*.c))
SFILES := $(notdir $(wildcard $(CURDIR)/source/*.s))

export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_SRC)
export LD := $(CXX)

export INCLUDE := $(PROJECT_INCLUDES) -I$(CURDIR)/$(BUILD)
export CFLAGS := $(CFLAGS)
export CXXFLAGS := $(CXXFLAGS)
export ASFLAGS := $(ASFLAGS)
export LIBS := $(LIBS)
export LIBPATHS := $(LIBDIRS)

export APP_ICON := $(TOPDIR)/$(ICON)
export NROFLAGS += --icon=$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp
export NROFLAGS += --romfsdir=$(TOPDIR)/$(ROMFS)

.PHONY: all clean $(BUILD)
all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $@ -f $(TOPDIR)/Makefile

clean:
	@echo clean ...
	@rm -rf $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else

.PHONY: all
DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
