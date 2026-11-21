// amdgpu_dcn.h - свой минимальный DCN 1.0 драйвер (Vega/Raven).
// ЭТАП 2: инициализация вывода картинки (заливка цвета).
//
// Работает ПОСЛЕ amdgpu_probe (MMIO найден). Использует регистры
// из dcn10_regs.h (сгенерированы из dcn_1_0_offset.h).
#ifndef AMDGPU_DCN_H
#define AMDGPU_DCN_H

// Инициализировать DCN и залить экран цветом. mmio — физ. адрес BAR2.
// Возврат: 0 — успех, иначе — код ошибки (этап, где упало).
int amdgpu_dcn_init(unsigned int mmio);

// Установить цвет заливки (24bpp RGB).
void amdgpu_dcn_set_color(unsigned char r, unsigned char g, unsigned char b);

// Разрешён ли захват вывода DCN (этап 2). По умолчанию 0: на реальном
// железе amdgpu_dcn_init() рвёт BIOS-дисплейный конвейер (нет SMU/DMUB FW,
// нет link training, TLB-регистры пишутся по неверным маскам) — экран гаснет.
// Включить вручную (=1) только для отладки на стенде с последовательным
// портом, когда терять картинку не страшно.
int amdgpu_dcn_takeover_enabled(void);

#endif // AMDGPU_DCN_H
