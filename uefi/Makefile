# uefi/Makefile - сборка BOOTX64.EFI БЕЗ gnu-efi (свои типы, свой переход).
#
# Цели:
#   make boot   - собрать BOOTX64.EFI
#   make image  - собрать mysticos2-uefi.img (FAT32 ESP)
#   make run    - запустить в QEMU с OVMF
#   make clean

CC   = gcc
LD   = ld
NASM = nasm

# x86_64 UEFI (PE32+). Используем корректные ЗАГОЛОВКИ gnu-efi
# (типы/структуры), но БЕЗ линковки -lefi/-lgnuefi и без uefi_call_wrapper.
GNUEFI_INC = /usr/include/efi
GNUEFI_LIB = /usr/lib
CFLAGS = -I$(GNUEFI_INC) -I$(GNUEFI_INC)/x86_64 -I$(GNUEFI_INC)/protocol \
         -DGNU_EFI_USE_MS_ABI \
         -ffreestanding -fno-stack-protector -fshort-wchar \
         -mno-red-zone -fpic -fno-stack-check \
         -Wall -Wextra -O2

KERNEL   = ../kernel.elf
OUT_IMG  = ../mysticos2-uefi.img
OBJS     = boot_helpers.o boot.o

# crt0 от gnu-efi: даёт _start + корректные base relocations (.reloc).
# Мы НЕ линкуем -lefi/-lgnuefi, только этот объект — иначе objcopy
# генерит PE с нулевыми raw-указателями и OVMF пишет "Unsupported".
CRT = /usr/lib/crt0-efi-x86_64.o

boot.o: boot.c efi_min.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_helpers.o: boot_helpers.asm
	$(NASM) -f elf64 $< -o $@

# PE32+ линковка через gnu-efi (как рабочий HELLO.EFI):
# -lefi/-lgnuefi дают .rela (base relocations) -> OVMF грузит.
BOOTX64.EFI: $(OBJS) boot.lds $(CRT)
	$(LD) -nostdlib -znocombreloc -T $(GNUEFI_LIB)/elf_x86_64_efi.lds -shared -Bsymbolic \
	      $(CRT) $(OBJS) -o bootx64.so -lefi -lgnuefi -L$(GNUEFI_LIB)
	objcopy -I elf64-x86-64 -O efi-app-x86_64 \
	        -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	        -j .rel -j .rela -j .reloc \
	        bootx64.so $@

boot: BOOTX64.EFI

# UEFI-образ: MBR + ESP(FAT32) + EFI/BOOT/BOOTX64.EFI + kernel.elf.
# ВАЖНО: OVMF не грузит superfloppy (FAT с сектора 0) - нужна таблица
# разделов. Поэтому собираем через mkuefi.py (MBR + EFI System Partition).
image: BOOTX64.EFI $(KERNEL) mkuefi.py
	rm -f $(OUT_IMG)
	python3 mkuefi.py $(OUT_IMG) BOOTX64.EFI $(KERNEL)
	@echo "image: $(OUT_IMG)"

run: image
	qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
	  -drive if=pflash,format=raw,file=/usr/share/OVMF/OVMF_VARS_4M.fd \
	  -drive format=raw,file=$(OUT_IMG) -m 256 -serial stdio

clean:
	rm -f *.o *.so BOOTX64.EFI
	@echo clean

.PHONY: boot image run clean
