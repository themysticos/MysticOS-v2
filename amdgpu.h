// amdgpu.h - минимальный драйвер AMD GPU (начало) для MysticOS.
//
// ЭТАП 1: только ОПРЕДЕЛЕНИЕ GPU через VDS (driver.h). Ничего не настраиваем.
// Драйвер регистрируется (amdgpu_driver_register), VDS сам зовёт match/probe.
#ifndef AMDGPU_H
#define AMDGPU_H

// Зарегистрировать драйвер в VDS. Звать при старте, ПОСЛЕ driver_init(),
// ДО driver_scan().
void amdgpu_driver_register(void);

// Готов ли драйвер к дальнейшим этапам (GPU найден, MMIO прочитан).
int amdgpu_ready(void);

// Физический адрес MMIO BAR (0 если нет).
unsigned int amdgpu_mmio_base(void);

// Управление Caps Lock LED (1 = горит, 0 = погашен). Индикатор результата
// на ноуте без serial/экрана.
void amdgpu_caps_led(int on);

#endif // AMDGPU_H
