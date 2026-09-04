#---------------------------------------------------------------------------------
# NextendoHub for Nintendo Switch homebrew (.nro)  —  devkitPro / libnx + borealis
#
#   ./setup.sh          (once: pulls borealis + stages its resources)
#   make               ->  NextendoHub.nro   (copy to sdmc:/switch/ , launch from hbmenu)
#
# Portlibs needed:
#   (dkp-)pacman -S switch-dev switch-curl switch-mbedtls switch-zlib \
#                   switch-glfw switch-glm switch-mesa switch-libdrm_nouveau
#
# Modeled on borealis's own demo Makefile — its `include ... borealis.mk` needs
# BOREALIS_PATH set beforehand (it's not automatic), and OUT_SHADERS set so its
# NanoVG/deko3d shaders get compiled (via `uam`, from devkitA64) into romfs.
# Skipping either breaks the build or breaks rendering at runtime.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO not set — run this inside the devkitPro environment")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
TARGET      := NextendoHub
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := source
ROMFS       := romfs
OUT_SHADERS := shaders

APP_TITLE   := Nextendo Hub
APP_AUTHOR  := adxmm  -  Founders of Nextendo Network: JuanBrew, Kazu
APP_VERSION := 1.0.0
ICON        := icon.jpg

# where borealis was cloned to by setup.sh, relative to this Makefile
BOREALIS_PATH := lib/borealis

#---------------------------------------------------------------------------------
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS   := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS   += $(INCLUDE) -D__SWITCH__
CXXFLAGS := $(CFLAGS) -std=gnu++17 -Wno-volatile
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# curl (mbedTLS) first, then whatever borealis.mk added, then libnx
LIBS    := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz
LIBDIRS := $(PORTLIBS) $(LIBNX)

# borealis: appends to SOURCES / INCLUDES / CFLAGS / CXXFLAGS / LIBS / LIBDIRS,
# and needs BOREALIS_PATH + LIBDIRS set above it (per its own README).
include $(TOPDIR)/$(BOREALIS_PATH)/library/borealis.mk

# std::thread/mutex + libnx last
LIBS += -lpthread -lnx

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                  $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES  := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
GLSLFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.glsl)))
BINFILES  := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD := $(CXX)

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# NanoVG's deko3d backend compiles its own .glsl shaders to .dksh at build
# time (via `uam`) and expects to find them under romfs:/shaders/ at runtime.
ifneq ($(strip $(ROMFS)),)
  ROMFS_TARGETS :=
  ROMFS_FOLDERS :=
  ifneq ($(strip $(OUT_SHADERS)),)
    ROMFS_SHADERS := $(ROMFS)/$(OUT_SHADERS)
    ROMFS_TARGETS += $(patsubst %.glsl, $(ROMFS_SHADERS)/%.dksh, $(GLSLFILES))
    ROMFS_FOLDERS += $(ROMFS_SHADERS)
  endif
  export ROMFS_DEPS := $(foreach file,$(ROMFS_TARGETS),$(CURDIR)/$(file))
endif

export APP_ICON := $(TOPDIR)/$(ICON)
export NROFLAGS += --icon=$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp
ifneq ($(ROMFS),)
export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean $(BUILD)
all: $(ROMFS_TARGETS) | $(BUILD)
	@MSYS2_ARG_CONV_EXCL="-D;$(MSYS2_ARG_CONV_EXCL)" $(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

ifneq ($(strip $(ROMFS_TARGETS)),)
$(ROMFS_TARGETS): | $(ROMFS_FOLDERS)

$(ROMFS_FOLDERS):
	@mkdir -p $@

$(ROMFS_SHADERS)/%_vsh.dksh: %_vsh.glsl
	@echo {vert} $(notdir $<)
	@uam -s vert -o $@ $<

$(ROMFS_SHADERS)/%_tcsh.dksh: %_tcsh.glsl
	@echo {tess_ctrl} $(notdir $<)
	@uam -s tess_ctrl -o $@ $<

$(ROMFS_SHADERS)/%_tesh.dksh: %_tesh.glsl
	@echo {tess_eval} $(notdir $<)
	@uam -s tess_eval -o $@ $<

$(ROMFS_SHADERS)/%_gsh.dksh: %_gsh.glsl
	@echo {geom} $(notdir $<)
	@uam -s geom -o $@ $<

$(ROMFS_SHADERS)/%_fsh.dksh: %_fsh.glsl
	@echo {frag} $(notdir $<)
	@uam -s frag -o $@ $<

$(ROMFS_SHADERS)/%.dksh: %.glsl
	@echo {comp} $(notdir $<)
	@uam -s comp -o $@ $<
endif

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(ROMFS_FOLDERS) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else
.PHONY: all
DEPENDS := $(OFILES:.o=.d)

all : $(OUTPUT).nro
$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp $(ROMFS_DEPS)
$(OUTPUT).elf : $(OFILES)
$(OFILES_SRC) : $(HFILES_BIN)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)
endif
