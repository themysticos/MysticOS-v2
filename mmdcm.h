// mmdcm.h - MMDCM: MysticOS MS-DOS Compatibility Module.
//
// Эмуляция DOS через V86 (Virtual 8086) режим x86: реальный CPU
// исполняет 16-битный real-mode код нативно, ядро перехватывает
// прерывания (int 10h/21h/...) и эмулирует BIOS/DOS.
#ifndef MMDCM_H
#define MMDCM_H

// Проверка поддержки VME (Virtual Mode Extensions) в CPU.
// Возвращает 1, если есть (тогда перехват int в V86 идёт через IDT).
int mmdcm_has_vme(void);

// Инициализация MMDCM: выделить DOS-память, настроить IVT/PD.
void mmdcm_init(void);

// Запустить 16-битный real-mode код из буфера (простейший тест).
// code — массив байт (x86 real mode), len — длина.
// Возвращает 0 при успехе, -1 при ошибке.
int mmdcm_run_code(const unsigned char* code, unsigned int len);

// Тестовая DOS-программа (Hello from DOS! через int 21h).
void mmdcm_test_dos(void);

#endif // MMDCM_H
