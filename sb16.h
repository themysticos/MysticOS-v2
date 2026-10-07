// sb16.h - Sound Blaster 16 (PCM-вывод, QEMU -device sb16).
// Базовый звуковой драйвер MysticOS: играет PCM-буферы, системные звуки,
// используется Doom (SFX). Плюс доступ к FM (OPL) для будущего MIDI.
#ifndef SB16_H
#define SB16_H

// Инициализация. Возвращает 1 если SB16 найден.
int  sb16_init(void);
int  sb16_ready(void);

// Воспроизвести 8-битный БЕЗЗНАКОВЫЙ моно-PCM буфер (обёртка для теста).
void sb16_play_pcm(const unsigned char* buf, unsigned int len, unsigned int rate);

// Настроить частоту дискретизации (Гц).
void sb16_set_rate(unsigned int rate);

// Микшер: очередь на воспроизведение (для системных звуков/игр).
// Заполняется из основного цикла (sb16_poll) кусками.
void sb16_poll(void);

// Запустить одноразовый системный звук (простой тон) - тест.
void sb16_beep(unsigned int freq_hz, unsigned int ms);

#endif // SB16_H
