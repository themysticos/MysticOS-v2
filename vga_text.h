// vga_text.h - настоящий VGA 80x25 ТЕКСТОВЫЙ режим (регистры, без BIOS).
//
// Зачем отдельно от vgadbg.c: vgadbg пишет в 0xB8000, но если адаптер
// в graphics mode (VBE), эта память не видна. Здесь мы ПЕРЕКЛЮЧАЕМ
// контроллер VGA обратно в text mode 3 стандартной последовательностью
// записей в регистры (Misc/Sequencer/CRTC/Graphics/Attribute) — так же,
// как это делают DOS-программы и Linux vgacon. Не зависит ни от VBE LFB,
// ни от serial. Это SAFE CONSOLE: последний надёжный канал на железе,
// где VBE/GPU сломаны.
//
// После vga_text_enter() можно пользоваться vga_* из vgadbg.h — они пишут
// в тот же 0xB8000, который теперь отображается.
#ifndef VGA_TEXT_H
#define VGA_TEXT_H

// Переключить адаптер в 80x25 text mode (mode 3) и очистить экран.
void vga_text_enter(void);

// true, если text mode был включён через vga_text_enter().
int vga_text_active(void);

// Удобный вход в safe console: переключить режим и напечатать заголовок.
// reason — короткая причина (напр. "GPU init failed").
void safe_console_enter(const char* reason);

#endif // VGA_TEXT_H
