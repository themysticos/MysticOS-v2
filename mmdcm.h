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
// seg  — сегмент загрузки (например 0x1000 = адрес 0x10000); 0 = по умолчанию.
// load_off — смещение внутри сегмента (для .COM обычно 0x100).
// Возвращает код выхода DOS (AL при int 21h:4C), или -1 при ошибке.
int mmdcm_run_code(const unsigned char* code, unsigned int len);

// Запустить код с явным размещением в DOS-памяти.
// Возвращает код выхода DOS или -1 при ошибке (нет VME, нулевая длина,
// не влезло в 64 КБ сегмент и т.п.).
int mmdcm_run_at(const unsigned char* code, unsigned int len,
                 unsigned int seg, unsigned int load_off);

// Загрузить и запустить .COM-программу (из буфера в памяти).
// .COM грузится в сегмент 0x1000 (адрес 0x10000), смещение 0x100,
// с PSP по смещению 0. Возвращает код выхода или -1.
int mmdcm_load_com(const unsigned char* image, unsigned int len);

// Загрузить и запустить .EXE (MZ) программу. Пока поддерживается только
// формат без релокаций (reloc count == 0) - простейшие DOS-утилиты.
// Возвращает код выхода или -1.
int mmdcm_load_exe(const unsigned char* image, unsigned int len);

// Запустить DOS-программу из файла ФС по пути (например "/DOS/HELLO.COM").
// Сам читает файл в память и определяет COM/EXE по расширению и/или MZ.
// Возвращает код выхода или -1.
int mmdcm_run_file(const char* path);

// Тестовая DOS-программа (Hello from DOS! через int 21h).
void mmdcm_test_dos(void);

// Информация о последнем запуске (для отладки/шелла).
unsigned int mmdcm_last_exit_code(void);   // AL из int 21h:4C / int 20h
const char*  mmdcm_last_error(void);       // NULL если ошибки не было

#endif // MMDCM_H
