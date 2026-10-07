// gpu.h - virtio-gpu драйвер MysticOS 2 (2D-вывод через GPU).
#ifndef GPU_H
#define GPU_H

// Найти virtio-gpu (0x1050), virtio_init, настроить controlq.
// Возвращает 1 при успехе.
int  gpu_init(void);

// Создать 2D-ресурс = экран (w x h), привязать backbuffer 0x1000000,
// показать через SET_SCANOUT. Возвращает 1 при успехе.
int  gpu_setup_framebuffer(unsigned int w, unsigned int h);

// Показать область backbuffer на экране (TRANSFER_TO_HOST_2D + RESOURCE_FLUSH).
void gpu_flush(unsigned int x, unsigned int y, unsigned int w, unsigned int h);

// Инициализировать ресурс аппаратного курсора (64x64) и показать его.
// Возвращает 1 при успехе.
int  gpu_cursor_init(void);

// Переместить аппаратный курсор в (x,y) через cursorq (быстро).
void gpu_cursor_move(int x, int y);

// Флаг: использовать ЛИ аппаратный курсор (cursorq).
// 1 = аппаратный (на реальном железе = отдельный plane, как в KDE).
// 0 = программный (ядро рисует курсор в backbuffer).
// В QEMU virtio-gpu аппаратный курсор двигает ХОСТОВЫЙ курсор -
// поэтому там удобнее 0.
extern int g_cursor_hw;
int  gpu_cursor_hw_enabled(void);

// 1 если GPU готов (можно вызывать gpu_flush).
int  gpu_is_ready(void);

#endif // GPU_H
