TARGET = i686-elf

OBJ_DIR = obj
SRC_DIR = src
BOOT_DIR = $(SRC_DIR)/boot
CPU_DIR = $(SRC_DIR)/cpu
DRIVERS_DIR = $(SRC_DIR)/drivers
KLIB_DIR = $(SRC_DIR)/klib
MEMORY_DIR = $(SRC_DIR)/memory
KERNEL_DIR = kernel

LINKER_SCRIPT = $(SRC_DIR)/linker.ld
GRUB_CFG = grub.cfg

HEKAOS_ISO = hekaos.iso
HEKAOS_BIN = hekaos.bin

KERNEL_BIN = $(KERNEL_DIR)/$(HEKAOS_BIN)
KERNEL_ISO = $(KERNEL_DIR)/$(HEKAOS_ISO)

TARGET_PATH = $(HOME)/opt/cross/bin/$(TARGET)

CC = $(TARGET_PATH)-gcc
AS = $(TARGET_PATH)-as
LD = $(TARGET_PATH)-ld

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
QEMU_KVM = -enable-kvm
endif

ASFLAGS = -I$(SRC_DIR) -I$(BOOT_DIR) -I$(CPU_DIR) -I$(DRIVERS_DIR)
CFLAGS  = -std=gnu99 -ffreestanding -Wall -Wextra -fno-builtin -fno-stack-protector -I$(SRC_DIR) -Iinclude -MMD -MP
LDFLAGS = -T $(LINKER_SCRIPT) -ffreestanding -nostdlib -nodefaultlibs

BOOT_OBJS = $(OBJ_DIR)/boot.o

CPU_OBJS = $(OBJ_DIR)/gdt.o $(OBJ_DIR)/gdt_flush.o $(OBJ_DIR)/idt.o $(OBJ_DIR)/idt_load.o \
           $(OBJ_DIR)/isr.o $(OBJ_DIR)/isr_stubs.o $(OBJ_DIR)/interrupts.o \
           $(OBJ_DIR)/rdtsc.o $(OBJ_DIR)/paging.o $(OBJ_DIR)/paging_stubs.o \
           $(OBJ_DIR)/trap_frame.o $(OBJ_DIR)/trap_frame_stubs.o

DRIVERS_OBJS = $(OBJ_DIR)/io.o $(OBJ_DIR)/pic.o

MEMORY_OBJS = $(OBJ_DIR)/phys_page_frame.o $(OBJ_DIR)/kmalloc.o $(OBJ_DIR)/vmalloc.o

KLIB_OBJS = $(OBJ_DIR)/string.o $(OBJ_DIR)/printk.o $(OBJ_DIR)/kpanic.o

KERNEL_OBJS = $(OBJ_DIR)/kernel.o $(OBJ_DIR)/signal.o

OBJS = $(BOOT_OBJS) $(CPU_OBJS) $(MEMORY_OBJS) $(DRIVERS_OBJS) $(KLIB_OBJS) $(KERNEL_OBJS)

-include $(OBJS:.o=.d)

all: $(KERNEL_BIN) $(KERNEL_ISO)

vpath %.s $(BOOT_DIR):$(CPU_DIR):$(DRIVERS_DIR)
vpath %.c $(SRC_DIR):$(CPU_DIR):$(MEMORY_DIR):$(DRIVERS_DIR):$(KLIB_DIR)

$(OBJ_DIR)/%.o: %.s | $(OBJ_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_BIN): $(OBJS) $(LINKER_SCRIPT) | $(KERNEL_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(KERNEL_ISO): $(KERNEL_BIN) $(GRUB_CFG) | $(KERNEL_DIR)
	mkdir -p $(KERNEL_DIR)/boot/grub
	cp $(KERNEL_BIN) $(KERNEL_DIR)/boot/
	cp $(GRUB_CFG) $(KERNEL_DIR)/boot/grub/
	grub-mkrescue --compress=xz -o $@ $(KERNEL_DIR)

$(OBJ_DIR) $(KERNEL_DIR):
	mkdir -p $@

run-bin:
	@test -f $(KERNEL_BIN) || { echo "Error: $(KERNEL_BIN) not found. Run 'make docker-build' first."; exit 1; }
	qemu-system-i386 $(QEMU_KVM) -kernel $(KERNEL_BIN)

run-iso:
	@test -f $(KERNEL_ISO) || { echo "Error: $(KERNEL_ISO) not found. Run 'make docker-build' first."; exit 1; }
	qemu-system-i386 $(QEMU_KVM) -cdrom $(KERNEL_ISO)

clean:
	rm -rf $(OBJ_DIR) $(KERNEL_DIR)
	-docker rm hekaos_tmp 2>/dev/null || true

docker-build:
	docker build -t hekaos .
	-docker rm hekaos_tmp 2>/dev/null || true
	docker create --name hekaos_tmp hekaos
	docker cp hekaos_tmp:/root/kernel ./
	docker rm hekaos_tmp

docker-prune-all:
	docker system prune --all

binary-size:
	@echo "Binary Size"
	@test -f $(KERNEL_BIN) || { echo "Error: $(KERNEL_BIN) not found. Run 'make docker-build' first."; exit 1; }
	@SIZE=$$(stat -c%s $(KERNEL_BIN)); \
	MB=$$(echo "scale=2; $$SIZE / 1048576" | bc); \
	echo "  $(HEKAOS_BIN): $$MB MB ($$SIZE B)"
	@echo "ISO Size"
	@test -f $(KERNEL_ISO) || { echo "Error: $(KERNEL_ISO) not found. Run 'make docker-build' first."; exit 1; }
	@SIZE=$$(stat -c%s $(KERNEL_ISO)); \
	MB=$$(echo "scale=2; $$SIZE / 1048576" | bc); \
	echo "  $(HEKAOS_ISO): $$MB MB ($$SIZE B)"

format:
	find . -name '*.c' -o -name '*.h' | xargs clang-format -i

help:
	@echo "Available targets:\n"
	@echo "  all              - build kernel binary and ISO (default)"
	@echo "  run-bin          - run kernel binary with QEMU"
	@echo "  run-iso          - run ISO with QEMU"
	@echo "  docker-build     - build kernel using Docker"
	@echo "  docker-prune-all - prune all Docker resources"
	@echo "  clean            - clean build artifacts"
	@echo "  format           - format source code with clang-format"
	@echo "  binary-size      - show binary and ISO sizes"

.PHONY: all clean run-iso run-bin docker-build docker-prune-all format help binary-size
