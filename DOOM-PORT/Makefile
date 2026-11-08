# Makefile - сборка doom.elf для MysticOS.
#
# Собирает: doomgeneric (все .c, кроме чужих слоёв), наша libc,
# платформенный слой doomgeneric_mysticos.c, w_file_mysticos.c.
#
# Результат: doom.elf (ELF32, entry=_start), кладётся в образ как /DOOM.ELF.

CC  = gcc
LD  = ld

DG  = doomgeneric/doomgeneric
ROOT = ..

# -nostdinc: НЕ тянуть системные glibc-заголовки (они дают _chk/isoc23).
# Вместо них - наши include/libc/*.h и include/ (ms/*).
CFLAGS = -m32 -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-pie -fno-pic -O2 \
         -I. -Iopl -I$(DG) -I$(ROOT)/include/libc -I$(ROOT)/include \
         -DLINUX -DNORMALUNIX -DFEATURE_SOUND \
         -fno-stack-protector

# Все .c Doom, кроме чужих платформенных слоёв и w_file_stdc (наша замена).
DG_SRCS = $(filter-out \
    $(wildcard $(DG)/doomgeneric_*.c) \
    $(DG)/w_file_stdc.c \
    $(DG)/i_allegromusic.c $(DG)/i_allegrosound.c \
    $(DG)/i_sdlmusic.c $(DG)/i_sdlsound.c $(DG)/i_cdmus.c, \
    $(wildcard $(DG)/*.c))

# libc (наша).
LIBC_SRCS = $(wildcard $(ROOT)/libc/*.c)

# Наш слой + WAD + звук.
# SFX -> SYS_SOUND_PCM -> VSS -> HDA. Музыка -> OPL3 (Nuked) + GENMIDI ->
# SYS_MUSIC_PCM -> кольцо в ядре -> HDA.
MY_SRCS = doomgeneric_mysticos.c w_file_mysticos.c dg_sound_mysticos.c \
          i_oplmusic.c midifile.c \
          opl/opl3.c opl/opl_mysticos.c
# dg_music_mysticos.c УБРАН: он шлёт SYS_MUSIC_PLAY, которого больше нет в
# msyscall.h (музыка теперь через OPL -> SYS_MUSIC_PCM в i_sound.c).

OBJS = $(DG_SRCS:.c=.o) $(LIBC_SRCS:.c=.o) $(MY_SRCS:.c=.o)

all: doom.elf

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

doom.elf: $(OBJS)
	$(LD) -m elf_i386 -Ttext-segment=0x04000000 -e _start --build-id=none \
	      $(OBJS) -o $@ -Map=doom.map

clean:
	rm -f $(OBJS) doom.elf doom.map

.PHONY: all clean
