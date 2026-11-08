// vss.h - Virtual Sound System MysticOS 2.
//
// VSS стоит между ядром/API и конкретным звуковым драйвером (HDA, ...).
// Драйвер регистрируется в таблице vss.c (sound_driver_t). Ядро зовёт
// vss_* и НЕ знает, какой драйвер активен. Добавление нового звукового
// драйвера = написать драйвер + зарегистрировать его (vss_register).
//
// Полный аналог VFS (fs.h): та же идея vtable + метод перебора.
#ifndef VSS_H
#define VSS_H

// ------------------------------------------------------------
// Интерфейс звукового драйвера. Каждый драйвер реализует эти функции.
// PCM: 8-бит БЕЗЗНАКОВЫЙ моно (как исторический SYS_SOUND_PCM).
// ------------------------------------------------------------
typedef struct sound_driver_s {
    const char* name;
    // probe: 1 если устройство найдено и инициализировано.
    int  (*probe)(void);
    // ready: 1 если драйвер готов играть.
    int  (*ready)(void);
    // set_rate: настроить частоту дискретизации (Гц).
    void (*set_rate)(unsigned int rate);
    // play_pcm: проиграть 8-бит беззнаковый моно-PCM.
    void (*play_pcm)(const unsigned char* buf, unsigned int len, unsigned int rate);
    // beep: одноразовый тон (freq Гц, длительность мс).
    void (*beep)(unsigned int freq_hz, unsigned int ms);
    // poll: продвинуть состояние (стоп по завершении). Звать из гл. цикла.
    void (*poll)(void);
} sound_driver_t;

// --- API ---

// Очистить таблицу (звать один раз при старте).
void vss_init(void);

// Зарегистрировать драйвер. 1 при успехе, 0 если нет места.
int  vss_register(const sound_driver_t* drv);

// Найти и инициализировать первый рабочий драйвер (перебор probe()).
// Возвращает 1, если какой-то драйвер поднялся.
int  vss_probe_all(void);

// Активен ли какой-либо драйвер.
int  vss_ready(void);

// Имя активного драйвера (или "none").
const char* vss_name(void);

// --- Операции (делегируются активному драйверу) ---
void vss_set_rate(unsigned int rate);
void vss_play_pcm(const unsigned char* buf, unsigned int len, unsigned int rate);
void vss_beep(unsigned int freq_hz, unsigned int ms);
void vss_poll(void);

#endif // VSS_H
