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

#endif // AMDGPU_DCN_H
