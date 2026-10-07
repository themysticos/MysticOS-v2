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

STAGE2 = stage2.bin
$(STAGE2): stage2.asm
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

mmdcm.o: mmdcm.c mmdcm.h
	$(GCC) $(CFLAGS) -c $< -o $@

mmdcm_asm.o: mmdcm_asm.asm
	$(NASM) -f elf32 $< -o $@

vbe.o: vbe.c
	$(GCC) $(CFLAGS) -c $< -o $@

bmp.o: bmp.c bmp.h
	$(GCC) $(CFLAGS) -c $< -o $@

vfs.o: vfs.c fs.h fat.h msfs.h
	$(GCC) $(CFLAGS) -c $< -o $@

msfs.o: msfs.c msfs.h ata.h
	$(GCC) $(CFLAGS) -c $< -o $@

gdt.o: gdt.c gdt.h
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

msdll.o: dll/msdll.c dll/msdll.h
	$(GCC) $(CFLAGS) -c $< -o $@

pe.o: pe.c pe.h
	$(GCC) $(CFLAGS) -c $< -o $@

# --- PE-программы (.exe) — кросс-компилятор MinGW -----------------
# Без CRT и WinAPI: используем наш API (int 0x80). ImageBase = 0x04000000
# (совпадает с TASK_CODE_VIRT), subsystem=console (не GUI, нам всё равно).
MINGW = i686-w64-mingw32-gcc
PECFLAGS = -m32 -ffreestanding -nostdlib -nostartfiles -e _start \
           -Wl,--subsystem,console -Wl,--image-base,0x04000000 \
           -Wl,--disable-dynamicbase

# Тестовая PE-DLL + exe с импортом (тест DLL-подсистемы).
DLLFLAGS = -m32 -shared -nostdlib -nostartfiles -Wl,--image-base,0x0C000000

testdll.dll: examples/testdll.c
	$(MINGW) $(DLLFLAGS) -o $@ examples/testdll.c

libtestdll.a: examples/testdll.def testdll.dll
	i686-w64-mingw32-dlltool -d examples/testdll.def -l $@

testexe.exe: examples/testexe.c libtestdll.a
	$(MINGW) $(PECFLAGS) -Iinclude -o $@ examples/testexe.c -L. -ltestdll

pehello.exe: examples/pehello.c include/mysticos.h
	$(MINGW) $(PECFLAGS) -o $@ examples/pehello.c

# --- .msdll (DLL для MysticOS 2) ---------------------------------
# Сборка: gcc -> linker script dll/msdll.ld -> пост-линкер msdll_pack.py.
# Каждая DLL линкуется под базу 0x64000000; релокации ABS32 генерит pack.
MSDLL_CFLAGS = -m32 -ffreestanding -nostdlib -fno-pic -fno-builtin \
               -fno-stack-protector -fno-pie -O2 -Wall -Wextra
MSDLL_LDFLAGS = -m32 -nostdlib -no-pie -Wl,-T,dll/msdll.ld \
                -Wl,-e,_start -Wl,--build-id=none

# Список исходников DLL (пример). Добавляй сюда свои.
MSDLL_SRCS = dll/example_msgui.c
MSDLL_OUTS = example_msgui.msdll

example_msgui.msdll: dll/example_msgui.c dll/msdll.h dll/msdll.ld tools/msdll_pack.py
	$(GCC) $(MSDLL_CFLAGS) -c dll/example_msgui.c -o /tmp/msdll_ex.o
	$(GCC) $(MSDLL_LDFLAGS) /tmp/msdll_ex.o -o /tmp/msdll_ex.elf
	python3 tools/msdll_pack.py /tmp/msdll_ex.elf $@

pci.o: pci.c pci.h io.h console.h
	$(GCC) $(CFLAGS) -c $< -o $@

driver.o: driver.c driver.h pci.h console.h
	$(GCC) $(CFLAGS) -c $< -o $@

serial.o: serial.c serial.h io.h
	$(GCC) $(CFLAGS) -c $< -o $@

virtio.o: virtio.c virtio.h pci.h serial.h io.h
	$(GCC) $(CFLAGS) -c $< -o $@

virtio_net.o: virtio_net.c virtio_net.h virtio.h driver.h serial.h
	$(GCC) $(CFLAGS) -c $< -o $@

gpu.o: gpu.c virtio.h virtio_gpu.h serial.h pmem.h
	$(GCC) $(CFLAGS) -c $< -o $@

net.o: net.c net.h virtio_net.h serial.h
	$(GCC) $(CFLAGS) -c $< -o $@

tcp.o: tcp.c tcp.h net.h serial.h
	$(GCC) $(CFLAGS) -c $< -o $@

$(ELF): entry.o switch.o kernel.o task.o paging.o paging_asm.o pmem.o mmdcm.o mmdcm_asm.o vbe.o bmp.o vfs.o msfs.o gdt.o font.o console.o idt.o pic.o io.o cursor.o icons.o icons_gen.o ata.o fat.o kprint.o term.o exec.o msdll.o pe.o pci.o driver.o serial.o virtio.o virtio_net.o net.o tcp.o gpu.o
	$(LD) $(LDFLAGS) -o $@ $^

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

WINAPP_ELF = examples/winapp.elf

# Правило сборки ELF-программ (API v2).
ELFCFLAGS = -m32 -ffreestanding -nostdlib -static -no-pie -fno-pic \
            -Wl,-Ttext-segment=0x4000000 -Wl,-e,_start -Wl,--build-id=none

# Системные программы (SYSPR): кладутся в /kernel/SYSPR/ образа.
SYSPR =
SYSPR_ELFS = $(addprefix examples/,$(addsuffix .elf,$(SYSPR)))

# Программы на новом API (include/mysticos.h): все вызовы — static inline
# в заголовках, каждый делает int 0x80 напрямую. msyscall.c больше не нужен.
# Список — MSYS_PROGS. WINAPP_ELF тоже сюда (он использует новый API).
MSYS_PROGS = about term files wpick imgview editor winapp painter browser

$(addprefix examples/,$(addsuffix .elf,$(MSYS_PROGS))): examples/%.elf: examples/%.c include/mysticos.h dll/msyscall.h
	gcc $(ELFCFLAGS) -Iinclude $< -o $@

# Старые программы (api->): пока работают в ring0, в ring3 упадут.
examples/%.elf: examples/%.c api.h
	gcc $(ELFCFLAGS) $< -o $@


MSYS_ELFS = $(addprefix examples/,$(addsuffix .elf,$(MSYS_PROGS)))

$(IMAGE): $(BOOT) $(STAGE2) $(BIN) $(MSYS_ELFS) $(MSDLL_OUTS) pehello.exe testdll.dll testexe.exe tools/mbr.py tools/fatput.py
	# Один FAT16-образ: 64 зарезервированных сектора под загрузчик+ядро.
	rm -f $@
	mkfs.fat -C $@ -F 16 -s 4 -R 512 131072
	dd if=$@ of=$(BOOT) bs=1 count=59 skip=3 seek=3 conv=notrunc 2>/dev/null
	dd if=$(BOOT) of=$@ conv=notrunc 2>/dev/null
	# stage2 — секторы 1..16
	dd if=$(STAGE2) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	# ядро — с LBA 16 (CHS сектор 17, как в stage2.asm)
	dd if=$(BIN) of=$@ bs=512 seek=16 conv=notrunc 2>/dev/null
	# Таблица разделов MBR — чтобы BIOS видел флешку загрузочной.
	python3 tools/mbr.py $@
	echo 'MysticOS 2 file system' > /tmp/readme.txt
	python3 tools/fatput.py $@ /tmp/readme.txt README.TXT 2>/dev/null || true
	echo 'hello world' > /tmp/hi.txt
	python3 tools/fatput.py $@ /tmp/hi.txt HI.TXT 2>/dev/null || true
	python3 tools/fatput.py $@ $(WINAPP_ELF) WINAPP.ELF 2>/dev/null || true
	# Системные программы в /kernel/SYSPR/
	python3 tools/fatput.py $@ examples/about.elf ABOUT.ELF kernel/SYSPR 2>/dev/null || true
	python3 tools/fatput.py $@ examples/files.elf FILES.ELF kernel/SYSPR 2>/dev/null || true
	python3 tools/fatput.py $@ examples/term.elf TERM.ELF kernel/SYSPR 2>/dev/null || true
	python3 tools/fatput.py $@ examples/wpick.elf WPICK.ELF kernel/SYSPR 2>/dev/null || true
	python3 tools/fatput.py $@ examples/imgview.elf IMGVIEW.ELF kernel/SYSPR 2>/dev/null || true
	python3 tools/fatput.py $@ examples/editor.elf EDITOR.ELF kernel/SYSPR 2>/dev/null || true
	python3 tools/fatput.py $@ examples/painter.elf PAINTER.ELF kernel/SYSPR 2>/dev/null || true
	python3 tools/fatput.py $@ examples/browser.elf BROWSER.ELF 2>/dev/null || true
	# Дефолтные обои: BG.BMP (если файл подготовлен в корне проекта).
	# Отсутствие файла НЕ ломает сборку (|| true) — фон останется fallback.
	python3 tools/fatput.py $@ BG.BMP BG.BMP kernel/BG/ 2>/dev/null || true
	# DLL в /kernel/SYS/ (FAT16 8.3: расширение .MSL = MyStic Library).
	# .DLL оставляем для будущей совместимости с Windows (PE).
	python3 tools/fatput.py $@ example_msgui.msdll EXAMPLE.MSL kernel/SYS 2>/dev/null || true
	# PE-программа (.exe) — в корень, как PEHELLO.EXE (FAT16 8.3).
	python3 tools/fatput.py $@ pehello.exe PEHELLO.EXE 2>/dev/null || true
	# Тестовые PE-DLL и exe (тест импортов).
	python3 tools/fatput.py $@ testdll.dll TESTDLL.DLL kernel/SYS 2>/dev/null || true
	python3 tools/fatput.py $@ testexe.exe TESTEXE.EXE 2>/dev/null || true


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
	rm -f msdll.o
	rm -f *.msdll *.exe *.dll *.a
	rm -f examples/*.elf disk2.img

.PHONY: all clean run
