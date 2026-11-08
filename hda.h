// hda.h - Intel High Definition Audio драйвер MysticOS 2.
//
// Контроллер HDA (PCI class 0x0403) общается через MMIO (BAR0, 16 КБ).
// Связь с кодеками — через CORB/RIRB (кольцевые буферы команд/ответов)
// и immediate commands (IC). Вывод PCM — через output stream (BDLE).
//
// Драйвер регистрируется:
//   1) в VDS (driver.h) — match по PCI class 0x0403, probe делает init;
//   2) в VSS (vss.h) — как sound_driver_t, чтобы ядро звало vss_*.
#ifndef HDA_H
#define HDA_H

// Регистрация в VDS (звать при старте, до driver_scan).
void hda_driver_register(void);

// --- Прямой API (используется vss через sound_driver_t) ---
int  hda_init(void);            // 1 если контроллер+кодек подняты
int  hda_ready(void);           // 1 если готов играть
void hda_set_rate(unsigned int rate);
void hda_play_pcm(const unsigned char* buf, unsigned int len, unsigned int rate);
void hda_beep(unsigned int freq_hz, unsigned int ms);
void hda_poll(void);            // продвинуть состояние (стоп по завершении)

// Отладочный дамп в COM1.
void hda_debug(void);

#endif // HDA_H
