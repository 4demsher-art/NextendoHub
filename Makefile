# NextendoHub Nintendo Switch NRO build
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set)
endif

PROJECT_ROOT ?= $(CURDIR)
TARGET := NextendoHub
BUILD := build
SOURCES := source
ROMFS := romfs
ICON := icon.jpg

include $(DEVKITPRO)/libnx/switch_rules

BOREALIS_PATH := $(PROJECT_ROOT)/lib/borealis
PORTLIBS_SWITCH := $(DEVKITPRO)/portlibs/switch

DEFINES := -DBOREALIS_RESOURCES=\"romfs:/\"

PROJECT_INCLUDES := \
	-I$(PROJECT_ROOT)/source \
	-I$(BOREALIS_PATH)/library/include \
	-I$(DEVKITPRO)/libnx/include \
	-I$(PORTLIBS_SWITCH)/include

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft
CFLAGS := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES) $(PROJECT_INCLUDES) -D__SWITCH__
CXXFLAGS := $(CFLAGS) -std=gnu++17
ASFLAGS := -g $(ARCH)

LIBDIRS := -L$(PORTLIBS_SWITCH)/lib -L$(DEVKITPRO)/libnx/lib
LIBS := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz

include $(BOREALIS_PATH)/library/borealis.mk

LIBS += -lpthread -lnx

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(PROJECT_ROOT)/$(TARGET)
export TOPDIR := $(PROJECT_ROOT)
export PROJECT_ROOT := $(PROJECT_ROOT)
export VPATH := $(PROJECT_ROOT)/$(SOURCES)
export DEPSDIR := $(PROJECT_ROOT)/$(BUILD)

CPPFILES := $(notdir $(wildcard $(PROJECT_ROOT)/source/*.cpp))
CFILES := $(notdir $(wildcard $(PROJECT_ROOT)/source/*.c))
SFILES := $(notdir $(wildcard $(PROJECT_ROOT)/source/*.s))

export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_SRC)
export LD := $(CXX)
export INCLUDE := $(PROJECT_INCLUDES) -I$(PROJECT_ROOT)/$(BUILD)
export CFLAGS := $(CFLAGS)
export CXXFLAGS := $(CXXFLAGS)
export ASFLAGS := $(ASFLAGS)
export LIBS := $(LIBS)
export LIBPATHS := $(LIBDIRS)

export APP_ICON := $(PROJECT_ROOT)/$(ICON)
export NROFLAGS += --icon=$(APP_ICON) --nacp=$(PROJECT_ROOT)/$(TARGET).nacp
export NROFLAGS += --romfsdir=$(PROJECT_ROOT)/$(ROMFS)

.PHONY: all clean $(BUILD)
all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $@ -f $(PROJECT_ROOT)/Makefile

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
