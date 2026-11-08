NASM    = nasm
GCC     = gcc
# Параллельная сборка: все .c компилируются одновременно (nproc ядер).
# ВНИМАНИЕ: не параллелит внутри одной цели (fatput.py всё равно последователен).
MAKEFLAGS += -j$(shell nproc)
LD      = ld
OBJCOPY = objcopy
MAKE    = make

CFLAGS  = -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
          -fno-pie -fno-common -O2 -Wall -Wextra -g
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

BOOT  = bootloader.bin
ELF   = kernel.elf
BIN   = kernel.bin
IMAGE = mysticos2.img
IMAGE2 = disk2.img

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

gif.o: gif.c gif.h
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

apic.o: apic.c apic.h
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

amdgpu.o: amdgpu.c amdgpu.h amdgpu_dcn.h pci.h serial.h
	$(GCC) $(CFLAGS) -c $< -o $@

amdgpu_dcn.o: amdgpu_dcn.c amdgpu_dcn.h dcn10_regs.h
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

tls/sha256.o: tls/sha256.c tls/sha256.h
	$(GCC) $(CFLAGS) -c $< -o $@

tls/sha1.o: tls/sha1.c tls/sha1.h
	$(GCC) $(CFLAGS) -c $< -o $@

tls/hmac.o: tls/hmac.c tls/hmac.h tls/sha256.h tls/sha1.h
	$(GCC) $(CFLAGS) -c $< -o $@

tls/rand.o: tls/rand.c tls/rand.h
	$(GCC) $(CFLAGS) -c $< -o $@

tls/aes.o: tls/aes.c tls/aes.h
	$(GCC) $(CFLAGS) -c $< -o $@

tls/bigint.o: tls/bigint.c tls/bigint.h
	$(GCC) $(CFLAGS) -c $< -o $@

tls/rsa.o: tls/rsa.c tls/rsa.h tls/bigint.h tls/sha256.h
	$(GCC) $(CFLAGS) -c $< -o $@

tls/x509.o: tls/x509.c tls/x509.h tls/rsa.h tls/bigint.h
	$(GCC) $(CFLAGS) -c $< -o $@

tls/ec.o: tls/ec.c tls/ec.h tls/bigint.h tls/rand.h
	$(GCC) $(CFLAGS) -c $< -o $@

tls/gcm.o: tls/gcm.c tls/gcm.h tls/aes.h
	$(GCC) $(CFLAGS) -c $< -o $@

tls/tls.o: tls/tls.c tls/tls.h tls/sha256.h tls/hmac.h tls/aes.h tls/rsa.h tls/x509.h tls/bigint.h tls/rand.h tls/ec.h tls/gcm.h
	$(GCC) $(CFLAGS) -c $< -o $@

virtio.o: virtio.c virtio.h pci.h serial.h io.h
	$(GCC) $(CFLAGS) -c $< -o $@

virtio_net.o: virtio_net.c virtio_net.h virtio.h driver.h serial.h
	$(GCC) $(CFLAGS) -c $< -o $@

gpu.o: gpu.c virtio.h virtio_gpu.h serial.h pmem.h
	$(GCC) $(CFLAGS) -c $< -o $@

theme.o: theme.c theme.h
	$(GCC) $(CFLAGS) -c $< -o $@

vss.o: vss.c vss.h
	$(GCC) $(CFLAGS) -c $< -o $@

hda.o: hda.c hda.h vss.h driver.h pci.h io.h serial.h music.h
	$(GCC) $(CFLAGS) -c $< -o $@

ac97.o: ac97.c ac97.h vss.h driver.h pci.h serial.h pmem.h music.h
	$(GCC) $(CFLAGS) -c $< -o $@

music.o: music.c music.h music_tables.h pmem.h
	$(GCC) $(CFLAGS) -c $< -o $@

# --- OPL3 (Nuked) для FM-музыки. Отдельные CFLAGS: -nostdinc + наши libc-хидеры.
OPLCFLAGS = -m32 -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
            -fno-pie -fno-common -O2 -Wall -Wextra \
            -Iinclude/libc -Iinclude -IDOOM-PORT/opl

DOOM-PORT/opl/opl3.o: DOOM-PORT/opl/opl3.c DOOM-PORT/opl/opl3.h DOOM-PORT/opl/wf_rom.h
	$(GCC) $(OPLCFLAGS) -c $< -o $@

DOOM-PORT/opl/opl_mysticos.o: DOOM-PORT/opl/opl_mysticos.c DOOM-PORT/opl/opl.h DOOM-PORT/opl/opl3.h
	$(GCC) $(OPLCFLAGS) -c $< -o $@

net.o: net.c net.h virtio_net.h serial.h
	$(GCC) $(CFLAGS) -c $< -o $@

tcp.o: tcp.c tcp.h net.h serial.h
	$(GCC) $(CFLAGS) -c $< -o $@

$(ELF): entry.o switch.o kernel.o task.o paging.o paging_asm.o pmem.o mmdcm.o mmdcm_asm.o vbe.o bmp.o gif.o vfs.o msfs.o gdt.o font.o console.o idt.o pic.o apic.o io.o cursor.o icons.o icons_gen.o ata.o fat.o kprint.o term.o exec.o msdll.o pe.o pci.o driver.o serial.o virtio.o virtio_net.o net.o tcp.o gpu.o theme.o vss.o hda.o ac97.o music.o amdgpu.o amdgpu_dcn.o tls/sha256.o tls/hmac.o tls/rand.o tls/aes.o tls/bigint.o tls/rsa.o tls/x509.o tls/ec.o tls/gcm.o tls/sha1.o tls/tls.o
	$(LD) $(LDFLAGS) -o $@ $^

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@
	# Проверка размера ядра (stage2 грузит ровно 496 секторов).
	@sh tools/check_size.sh $@

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
MSYS_PROGS = about term files wpick imgview editor winapp painter browser music zmejka

$(addprefix examples/,$(addsuffix .elf,$(MSYS_PROGS))): examples/%.elf: examples/%.c include/mysticos.h dll/msyscall.h
	gcc $(ELFCFLAGS) -Iinclude $< -o $@

# Старые программы (api->): пока работают в ring0, в ring3 упадут.
examples/%.elf: examples/%.c api.h
	gcc $(ELFCFLAGS) $< -o $@


MSYS_ELFS = $(addprefix examples/,$(addsuffix .elf,$(MSYS_PROGS)))

# --- DOOM (DOOM-PORT/) -------------------------------------------------
# doomgeneric собран под наш API (int 0x80). Собирается своим Makefile.
DOOM_ELF = DOOM-PORT/doom.elf
DOOM_WAD = DOOM-PORT/DOOM.WAD

$(DOOM_ELF):
	$(MAKE) -C DOOM-PORT

$(IMAGE): $(BOOT) $(STAGE2) $(BIN) $(MSYS_ELFS) $(MSDLL_OUTS) pehello.exe testdll.dll testexe.exe $(DOOM_ELF) tools/mbr.py tools/fatput.py
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
	echo 'hello world' > /tmp/hi.txt
	# ВСЕ файлы кладём ОДНИМ вызовом fatput (--batch): образ открывается и
	# сохраняется один раз вместо ~20. Формат spec: src=DEST[=PATH].
	python3 tools/fatput.py $@ --batch \
	  /tmp/readme.txt=README.TXT \
	  /tmp/hi.txt=HI.TXT \
	  $(WINAPP_ELF)=WINAPP.ELF \
	  examples/about.elf=ABOUT.ELF=kernel/SYSPR \
	  examples/files.elf=FILES.ELF=kernel/SYSPR \
	  examples/term.elf=TERM.ELF=kernel/SYSPR \
	  examples/wpick.elf=WPICK.ELF=kernel/SYSPR \
	  examples/imgview.elf=IMGVIEW.ELF=kernel/SYSPR \
	  examples/editor.elf=EDITOR.ELF=kernel/SYSPR \
	  examples/painter.elf=PAINTER.ELF=kernel/SYSPR \
	  examples/browser.elf=BROWSER.ELF \
	  examples/zmejka.elf=ZMEJKA.ELF \
	  examples/music.elf=MUSIC.ELF=kernel/SYSPR \
	  BG.BMP=BG.BMP=kernel/BG/ \
	  example_msgui.msdll=EXAMPLE.MSL=kernel/SYS \
	  pehello.exe=PEHELLO.EXE \
	  testdll.dll=TESTDLL.DLL=kernel/SYS \
	  testexe.exe=TESTEXE.EXE \
	  $(DOOM_ELF)=DOOM.ELF \
	  $(DOOM_WAD)=DOOM.WAD \
	  MEG.WAV=MEG.WAV \
	  SINE.WAV=SINE.WAV


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

# run: аппаратное ускорение CPU (WHPX). Включает компонент Windows
# "Windows Hypervisor Platform" (HypervisorPlatform). Даёт реальную
# виртуализацию CPU вместо эмуляции -> всё (рендер + звук) быстрее в разы.
# Быстрая итерация: пересобрать ТОЛЬКО kernel.bin, влить в образ dd'ом.
# НЕ пересоздаёт FAT (не копирует 45 МБ файлов). ~10 сек вместо 3 минут.
# Используй, когда менял только kernel.c / tls/* / net.c / tcp.c / etc.
# (browser.c / DOOM — лежат в FAT, для них нужен полный make run.)
krun: $(BIN)
	dd if=$(BIN) of=$(IMAGE) bs=512 seek=16 conv=notrunc 2>/dev/null
	qemu-system-x86_64.exe -accel whpx \
	  -drive format=raw,file=$(IMAGE),if=ide,index=0 \
	  -drive format=raw,file=$(IMAGE2),if=ide,index=1 \
	  -m 512 \
	  -device virtio-vga \
	  -netdev user,id=n0 \
	  -device virtio-net-pci,netdev=n0 \
	  -device intel-hda \
	  -device hda-duplex \
	  -display gtk \
	  -serial stdio

.PHONY: krun

run: $(IMAGE) $(IMAGE2)
	qemu-system-x86_64.exe -accel tcg \
  -drive format=raw,file=$(IMAGE),if=ide,index=0 \
  -drive format=raw,file=$(IMAGE2),if=ide,index=1 \
  -m 512 \
  -device virtio-vga \
  -netdev user,id=n0 \
  -device virtio-net-pci,netdev=n0 \
  -audiodev sdl,id=snd0 \
  -device AC97,audiodev=snd0 \
  -display gtk \
  -serial stdio 

# ============================================================
# ДИАГНОСТИКА ТАЙМЕРА под WHPX (IRQ0 не доставляется через PIC).
# Перебор machine/kernel_irqchip — какой оживит таймер.
# ============================================================
# t1: q35 (ICH9 вместо PIIX — другой путь IRQ)
t1: $(IMAGE) $(IMAGE2)
	qemu-system-x86_64.exe -accel whpx -machine q35 \
  -drive format=raw,file=$(IMAGE),if=ide,index=0 \
  -drive format=raw,file=$(IMAGE2),if=ide,index=1 \
  -m 512 -device virtio-vga \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -device intel-hda -device hda-duplex \
  -display gtk -serial stdio 

# t2: kernel_irqchip=on
t2: $(IMAGE) $(IMAGE2)
	qemu-system-x86_64.exe -accel whpx -machine kernel_irqchip=on \
  -drive format=raw,file=$(IMAGE),if=ide,index=0 \
  -drive format=raw,file=$(IMAGE2),if=ide,index=1 \
  -m 512 -device virtio-vga \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -device intel-hda -device hda-duplex \
  -display gtk -serial stdio 

# t3: kernel_irqchip=split
t3: $(IMAGE) $(IMAGE2)
	qemu-system-x86_64.exe -accel whpx -machine kernel_irqchip=split \
  -drive format=raw,file=$(IMAGE),if=ide,index=0 \
  -drive format=raw,file=$(IMAGE2),if=ide,index=1 \
  -m 512 -device virtio-vga \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -device intel-hda -device hda-duplex \
  -display gtk -serial stdio 

# t4: q35 + kernel_irqchip=on
t4: $(IMAGE) $(IMAGE2)
	qemu-system-x86_64.exe -accel whpx -machine q35,kernel_irqchip=on \
  -drive format=raw,file=$(IMAGE),if=ide,index=0 \
  -drive format=raw,file=$(IMAGE2),if=ide,index=1 \
  -m 512 -device virtio-vga \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -device intel-hda -device hda-duplex \
  -display gtk -serial stdio 

# run-msfs: основной диск (FAT) + диск 2 (MSFS2) — проверка маркерной ФС.
run-msfs: $(IMAGE) disk2_msfs.img
	qemu-system-x86_64.exe -accel tcg \
  -drive format=raw,file=$(IMAGE),if=ide,index=0 \
  -drive format=raw,file=disk2_msfs.img,if=ide,index=1 \
  -m 512 \
  -device virtio-vga \
  -netdev user,id=n0 \
  -device virtio-net-pci,netdev=n0 \
  -device intel-hda \
  -device hda-duplex \
  -display gtk \
  -serial stdio 

# run-tcg: чистая программная эмуляция (fallback, если WHPX не работает).
run-tcg: $(IMAGE) $(IMAGE2)
	qemu-system-x86_64.exe -accel tcg \
  -drive format=raw,file=$(IMAGE),if=ide,index=0 \
  -drive format=raw,file=$(IMAGE2),if=ide,index=1 \
  -m 512 \
  -device virtio-vga \
  -netdev user,id=n0 \
  -device virtio-net-pci,netdev=n0 \
  -device intel-hda \
  -device hda-duplex \
  -display gtk \
  -serial stdio 

# run-gl: экспериментальный — virtio-gpu-gl (virgl) + OpenGL.
# НЕ ускоряет текущую ОС: у неё нет virgl-драйвера (только 2D-команды).
# Оставлен для будущего 3D-API.
run-gl: $(IMAGE) $(IMAGE2)
	qemu-system-x86_64.exe -accel tcg \
  -drive format=raw,file=$(IMAGE),if=ide,index=0 \
  -drive format=raw,file=$(IMAGE2),if=ide,index=1 \
  -m 512 \
  -device virtio-vga-gl \
  -display gtk,gl=on \
  -netdev user,id=n0 \
  -device virtio-net-pci,netdev=n0 \
  -device intel-hda \
  -device hda-duplex \
  -serial stdio 

.PHONY: run run-tcg run-gl
clean:
	rm -f *.o *.elf *.bin *.img
	rm -f msdll.o
	# ВАЖНО: раньше tls/*.o (и другие подкаталоги) НЕ чистились — старые
	# объекты оставались и правки в tls/ не попадали в сборку.
	rm -f tls/*.o dll/*.o libc/*.o 2>/dev/null || true
	rm -f *.msdll *.exe *.dll *.a
	rm -f examples/*.elf disk2.img

.PHONY: all clean
