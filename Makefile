BUILD_DIR := build
SRC_DIR   := src
INC_DIR   := include
ARCH_DIR  := $(SRC_DIR)/arch

# Toolchain from PATH
CC      := aarch64-none-elf-gcc
LD      := aarch64-none-elf-ld
OBJCOPY := aarch64-none-elf-objcopy

CPU     := cortex-a72
CFLAGS  := -Wall -O2 -ffreestanding -nostdinc -nostdlib -nostartfiles -mstrict-align -mcpu=$(CPU) -I$(INC_DIR)
ASFLAGS := $(CFLAGS)
LDFLAGS := -T link.ld -nostdlib

# Collect sources
C_SRCS := $(shell find $(SRC_DIR) -type f -name "*.c")
S_SRCS := $(shell find $(ARCH_DIR) -type f -name "*.S")

# Map to build/ keeping folder structure
C_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))
S_OBJS := $(patsubst $(SRC_DIR)/%.S,$(BUILD_DIR)/%.o,$(S_SRCS))

ELF := $(BUILD_DIR)/kernel8.elf
IMG := $(BUILD_DIR)/kernel8.img

.PHONY: all clean info
all: $(IMG)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(ELF): $(S_OBJS) $(C_OBJS)
	$(LD) $(LDFLAGS) $(S_OBJS) $(C_OBJS) -o $@

$(IMG): $(ELF)
	$(OBJCOPY) -O binary $(ELF) $(IMG)

clean:
	rm -rf $(BUILD_DIR)

info:
	@echo "C:   $(words $(C_SRCS)) files"
	@echo "ASM: $(words $(S_SRCS)) files"
	@echo "ELF: $(ELF)"
	@echo "IMG: $(IMG)"