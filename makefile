NASM    = nasm
GCC     = gcc
LD      = ld
OBJCOPY = objcopy

CFLAGS  = -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
          -fno-pie -fno-common -O2 -Wall -Wextra
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

BOOT  = bootloader.bin
ELF   = kernel.elf
BIN   = kernel.bin
IMAGE = mysticos2.img
IMAGE2 = disk2.img   # второй диск (D1)

all: $(IMAGE) $(IMAGE2)

$(BOOT): bootloader.asm
	$(NASM) -f bin $< -o $@

entry.o: entry.asm
	$(NASM) -f elf32 $< -o $@

switch.o: switch.asm
	$(NASM) -f elf32 $< -o $@

task.o: task.c
	$(GCC) $(CFLAGS) -c $< -o $@

paging.o: paging.c
	$(GCC) $(CFLAGS) -c $< -o $@

paging_asm.o: paging.asm
	$(NASM) -f elf32 $< -o $@

pmem.o: pmem.c
	$(GCC) $(CFLAGS) -c $< -o $@

vbe.o: vbe.c
	$(GCC) $(CFLAGS) -c $< -o $@

bmp.o: bmp.c bmp.h
	$(GCC) $(CFLAGS) -c $< -o $@

vfs.o: vfs.c fs.h fat.h msfs.h
	$(GCC) $(CFLAGS) -c $< -o $@

msfs.o: msfs.c msfs.h ata.h
	$(GCC) $(CFLAGS) -c $< -o $@

font.o: font.c
	$(GCC) $(CFLAGS) -c $< -o $@

console.o: console.c
	$(GCC) $(CFLAGS) -c $< -o $@

kernel.o: kernel.c
	$(GCC) $(CFLAGS) -c $< -o $@

idt.o: idt.c
	$(GCC) $(CFLAGS) -c $< -o $@

pic.o: pic.c
	$(GCC) $(CFLAGS) -c $< -o $@

io.o: io.c
	$(GCC) $(CFLAGS) -c $< -o $@

cursor.o: cursor.c
	$(GCC) $(CFLAGS) -c $< -o $@

icons.o: icons.c
	$(GCC) $(CFLAGS) -c $< -o $@

icons_gen.o: icons_gen.c
	$(GCC) $(CFLAGS) -c $< -o $@

ata.o: ata.c
	$(GCC) $(CFLAGS) -c $< -o $@

fat.o: fat.c
	$(GCC) $(CFLAGS) -c $< -o $@

kprint.o: kprint.c
	$(GCC) $(CFLAGS) -c $< -o $@

term.o: term.c
	$(GCC) $(CFLAGS) -c $< -o $@

exec.o: exec.c
	$(GCC) $(CFLAGS) -c $< -o $@

$(ELF): entry.o switch.o kernel.o task.o paging.o paging_asm.o pmem.o vbe.o bmp.o vfs.o msfs.o font.o console.o idt.o pic.o io.o cursor.o icons.o icons_gen.o ata.o fat.o kprint.o term.o exec.o
	$(LD) $(LDFLAGS) -o $@ $^

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

WINAPP_ELF = examples/winapp.elf

# Правило сборки ELF-программ (API v2).
ELFCFLAGS = -m32 -ffreestanding -nostdlib -static -no-pie -fno-pic \
            -Wl,-Ttext-segment=0x4000000 -Wl,-e,_start -Wl,--build-id=none

# Тестовая ELF-программа: всегда собирается и кладётся в образ как WINAPP.ELF.
$(WINAPP_ELF): examples/winapp.c api.h
	gcc $(ELFCFLAGS) $< -o $@

# Системные программы (SYSPR): кладутся в /kernel/SYSPR/ образа.
SYSPR = about files term wpick imgview editor
SYSPR_ELFS = $(addprefix examples/,$(addsuffix .elf,$(SYSPR)))

examples/%.elf: examples/%.c api.h
	gcc $(ELFCFLAGS) $< -o $@

$(IMAGE): $(BOOT) $(BIN) $(WINAPP_ELF) $(SYSPR_ELFS)
	# Один FAT16-образ: 64 зарезервированных сектора под загрузчик+ядро.
	rm -f $@
	mkfs.fat -C $@ -F 16 -s 4 -R 256 131072
	dd if=$@ of=$(BOOT) bs=1 count=59 skip=3 seek=3 conv=notrunc 2>/dev/null
	dd if=$(BOOT) of=$@ conv=notrunc 2>/dev/null
	dd if=$(BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	echo 'MysticOS 2 file system' > /tmp/readme.txt
	python3 tools/fatput.py $@ /tmp/readme.txt README.TXT 2>/dev/null || true
	echo 'hello world' > /tmp/hi.txt
	python3 tools/fatput.py $@ /tmp/hi.txt HI.TXT 2>/dev/null || true
	python3 tools/fatput.py $@ $(WINAPP_ELF) WINAPP.ELF 2>/dev/null || true
	# Системные программы в /kernel/SYSPR/
	python3 tools/fatput.py $@ examples/about.elf ABOUT.ELF kernel/SYSPR 2>/dev/null || true
	python3 tools/fatput.py $@ examples/files.elf FILES.ELF kernel/SYSPR 2>/dev/null || true
	python3 tools/fatput.py $@ examples/term.elf  TERM.ELF  kernel/SYSPR 2>/dev/null || true
	python3 tools/fatput.py $@ examples/wpick.elf WPICK.ELF kernel/SYSPR 2>/dev/null || true
	python3 tools/fatput.py $@ examples/imgview.elf IMGVIEW.ELF kernel/SYSPR 2>/dev/null || true
	python3 tools/fatput.py $@ examples/editor.elf EDITOR.ELF kernel/SYSPR 2>/dev/null || true
	# Дефолтные обои: BG.BMP (если файл подготовлен в корне проекта).
	# Отсутствие файла НЕ ломает сборку (|| true) — фон останется fallback.
	python3 tools/fatput.py $@ BG.BMP BG.BMP kernel/BG 2>/dev/null || true

# Второй диск (D1): простой FAT16-образ с парой тестовых файлов.
$(IMAGE2):
	rm -f $@
	mkfs.fat -C $@ -F 16 -s 4 65536
	echo 'Second disk (D1)' > /tmp/d1readme.txt
	python3 tools/fatput.py $@ /tmp/d1readme.txt README.TXT 2>/dev/null || true
	echo 'hello from D1' > /tmp/d1hi.txt
	python3 tools/fatput.py $@ /tmp/d1hi.txt HI.TXT 2>/dev/null || true
	mkdir -p /tmp/d1dir
	echo 'in folder' > /tmp/d1dir/NOTE.TXT
	python3 tools/fatput.py $@ /tmp/d1dir/NOTE.TXT NOTE.TXT 2>/dev/null || true

run: $(IMAGE) $(IMAGE2)
	qemu-system-x86_64 -drive format=raw,file=$(IMAGE),if=ide,index=0 -drive format=raw,file=$(IMAGE2),if=ide,index=1 -m 256

clean:
	rm -f *.o *.elf *.bin *.img
	rm -f examples/*.elf disk2.img

.PHONY: all clean run
