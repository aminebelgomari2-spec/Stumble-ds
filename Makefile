.SUFFIXES:

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
export PATH := $(DEVKITPRO)/tools/bin:$(DEVKITARM)/bin:$(PATH)

CTRULIB  ?= $(DEVKITPRO)/libctru
PORTLIBS ?= $(DEVKITPRO)/portlibs/3ds

TARGET     := StumbleGuys3DS
BUILD      := build
APP_TITLE  := Stumble Guys 3DS
APP_DESC   := Jeu de Parkour 3D et Ragdoll
APP_AUTHOR := Community
ICON       := assets/icon.png

CC := arm-none-eabi-gcc

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -std=gnu11 -g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections \
           $(ARCH) -D__3DS__ -Isource \
           -I$(CTRULIB)/include -I$(PORTLIBS)/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map \
           -L$(CTRULIB)/lib -L$(PORTLIBS)/lib
LIBS    := -lcitro2d -lcitro3d -lctru -lm

SRC := $(wildcard source/*.c)
OBJ := $(patsubst source/%.c,$(BUILD)/%.o,$(SRC))

.PHONY: all cia clean

all: $(TARGET).3dsx

$(BUILD):
	@mkdir -p $@

$(BUILD)/%.o: source/%.c source/config.h | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

$(BUILD)/$(TARGET).smdh: $(ICON) | $(BUILD)
	smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" $(ICON) $@

$(TARGET).3dsx: $(TARGET).elf $(BUILD)/$(TARGET).smdh
	3dsxtool $(TARGET).elf $@ --smdh=$(BUILD)/$(TARGET).smdh

cia: $(TARGET).elf
	bash tools/make_cia.sh $(TARGET).elf $(TARGET).cia

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).cia

-include $(OBJ:.o=.d)
