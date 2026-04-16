# TWIBOOT — CH32V003, bare metal.
PREFIX ?= riscv64-unknown-elf-

WLINK ?= $(shell command -v wlink 2>/dev/null || echo "$(HOME)/.cargo/bin/wlink")

# BFLASH window (CH32V003 RM)
BFLASH_ADDR  := 0x1ffff000
BFLASH_MAX   := 1920

CC := "$(PREFIX)gcc"
OBJCOPY := "$(PREFIX)objcopy"
OBJDUMP := "$(PREFIX)objdump"
SIZE := "$(PREFIX)size"

ARCH := -march=rv32ec_zicsr -mabi=ilp32e -fsigned-char -fno-common

INCLUDES := -I.

# TWIBOOT_INFINITE=1: stay in bootloader (no autoboot).
TWIBOOT_INF_FLAG := $(if $(filter 1,$(TWIBOOT_INFINITE)),-DTWIBOOT_INFINITE=1,)

ifdef LED_PIN
  ifeq ($(LED_PIN),$(filter $(LED_PIN),PC1 PC2))
    $(error LED_PIN=$(LED_PIN) conflicts with I2C lines (PC1=SDA, PC2=SCL). Please choose another pin.)
  endif
  LED_PORT := $(shell echo $(LED_PIN) | cut -c 2)
  LED_PIN_NUM := $(shell echo $(LED_PIN) | cut -c 3-)
  LED_FLAGS := -DLED_PORT_$(LED_PORT) -DLED_PIN=$(LED_PIN_NUM)
endif

FLASH_PAGE_SIZE ?= 64

CFLAGS := $(ARCH) -g -Os -fno-strict-aliasing -std=gnu99 -Wall -Wno-main -ffunction-sections -fdata-sections \
	-DTWI_ADDRESS=$(TWI_ADDRESS) -DFLASH_PAGE_SIZE=$(FLASH_PAGE_SIZE) $(LED_FLAGS) $(TWIBOOT_INF_FLAG) $(INCLUDES)

LDFLAGS_COMMON := -nostdlib -nostartfiles -Wl,--gc-sections

BUILD_DIR := build

OBJS := $(addprefix $(BUILD_DIR)/, startup_bl.o boot_twiboot.o)
TARGET_ELF := $(BUILD_DIR)/twiboot_ch32.elf
TARGET_HEX := $(BUILD_DIR)/twiboot_ch32.hex
TARGET_BIN := $(BUILD_DIR)/twiboot_ch32.bin

.PHONY: all clean check flash 

all: check $(TARGET_ELF) $(TARGET_HEX) $(TARGET_BIN)

check:
	@if [ -z "$(TWI_ADDRESS)" ]; then echo "Set TWI_ADDRESS (7-bit), e.g. make TWI_ADDRESS=0x29"; exit 1; fi

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET_ELF): $(OBJS)
	$(CC) $(CFLAGS) -T Link.ld $(LDFLAGS_COMMON) -Wl,-Map=$(BUILD_DIR)/twiboot_ch32.map -o $@ $^
	$(SIZE) $@
	@$(SIZE) -B $@ | awk -v max=$(BFLASH_MAX) 'NR==2 { \
	  used=$$1+$$2; \
	  if (used > max) { \
	    printf "%s: BFLASH image too large: %u bytes (max %u)\n", "$@", used, max > "/dev/stderr"; \
	    exit 1; \
	  } \
	  printf "BFLASH (text+data LMA): %u / %u bytes OK\n", used, max; \
	}'
	$(OBJDUMP) -h -S $@ > $(BUILD_DIR)/twiboot_ch32.lss

$(TARGET_HEX): $(TARGET_ELF)
	$(OBJCOPY) --change-addresses $(BFLASH_ADDR) -O ihex $< $@

$(TARGET_BIN): $(TARGET_ELF)
	$(OBJCOPY) -O binary $< $@

flash: $(TARGET_BIN)
	$(WLINK) flash --system-boot system $(TARGET_BIN) --address $(BFLASH_ADDR)

clean:
	rm -rf $(BUILD_DIR)
