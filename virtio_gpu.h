// virtio_gpu.h - протокол virtio-gpu (2D) для MysticOS 2.
//
// Основано на Linux UAPI include/uapi/linux/virtio_gpu.h (BSD-лицензия).
// Только 2D-часть (путь A): создать ресурс = экран, attach backing,
// set_scanout, transfer_to_host_2d, resource_flush.
//
// 3D/virgl (путь B) сюда НЕ входит — отдельно позже.
#ifndef VIRTIO_GPU_H
#define VIRTIO_GPU_H

#include "virtio.h"   // v_u8/v_u16/v_u32/v_u64

// ============================================================
// Device feature bits (device-specific config)
// ============================================================
#define VIRTIO_GPU_F_VIRGL  0   // 3D (нам НЕ нужен для 2D)
#define VIRTIO_GPU_F_EDID   1

// ============================================================
// Коды команд и ответов
// ============================================================
enum virtio_gpu_ctrl_type {
    VIRTIO_GPU_UNDEFINED = 0,

    // 2d commands
    VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
    VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
    VIRTIO_GPU_CMD_RESOURCE_UNREF,
    VIRTIO_GPU_CMD_SET_SCANOUT,
    VIRTIO_GPU_CMD_RESOURCE_FLUSH,
    VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
    VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
    VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
    VIRTIO_GPU_CMD_GET_CAPSET_INFO,
    VIRTIO_GPU_CMD_GET_CAPSET,
    VIRTIO_GPU_CMD_GET_EDID,

    // cursor commands
    VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
    VIRTIO_GPU_CMD_MOVE_CURSOR,

    // success responses
    VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
    VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
    VIRTIO_GPU_RESP_OK_CAPSET_INFO,
    VIRTIO_GPU_RESP_OK_CAPSET,
    VIRTIO_GPU_RESP_OK_EDID,

    // error responses
    VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
    VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
    VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
};

// Форматы пикселей (берём BGRA - совпадает с нашим backbuffer).
enum virtio_gpu_formats {
    VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM = 1,   // BGRA (то, что нам нужно)
    VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM = 2,
    VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM = 3,
    VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM = 4,
    VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM = 67,
};

// ============================================================
// Базовые структуры
// ============================================================
// Общий заголовок ЛЮБОЙ команды/ответа. 16 байт, выровнен.
typedef struct {
    v_u32 type;       // virtio_gpu_ctrl_type
    v_u32 flags;      // VIRTIO_GPU_FLAG_*
    v_u64 fence_id;   // для fence (мы 0)
    v_u32 ctx_id;     // для 3D (мы 0)
    v_u8  ring_idx;
    v_u8  padding[3];
} __attribute__((packed)) virtio_gpu_ctrl_hdr_t;

typedef struct {
    v_u32 x, y, width, height;
} __attribute__((packed)) virtio_gpu_rect_t;

// ============================================================
// 2D-команды
// ============================================================

// RESOURCE_CREATE_2D
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    v_u32 resource_id;
    v_u32 format;       // virtio_gpu_formats
    v_u32 width;
    v_u32 height;
} __attribute__((packed)) virtio_gpu_resource_create_2d_t;

// RESOURCE_UNREF (освободить ресурс)
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    v_u32 resource_id;
    v_u32 padding;
} __attribute__((packed)) virtio_gpu_resource_unref_t;

// SET_SCANOUT (показать ресурс на выходе)
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    v_u32 scanout_id;
    v_u32 resource_id;
} __attribute__((packed)) virtio_gpu_set_scanout_t;

// RESOURCE_FLUSH (обновить область на экране)
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    v_u32 resource_id;
    v_u32 padding;
} __attribute__((packed)) virtio_gpu_resource_flush_t;

// TRANSFER_TO_HOST_2D (скопировать из guest-памяти в host-ресурс)
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    v_u64 offset;
    v_u32 resource_id;
    v_u32 padding;
} __attribute__((packed)) virtio_gpu_transfer_to_host_2d_t;

// Запись привязки памяти (для ATTACH_BACKING)
typedef struct {
    v_u64 addr;
    v_u32 length;
    v_u32 padding;
} __attribute__((packed)) virtio_gpu_mem_entry_t;

// RESOURCE_ATTACH_BACKING: hdr + resource_id + nr_entries,
// затем nr_entries штук virtio_gpu_mem_entry_t в ТОМ ЖЕ буфере.
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    v_u32 resource_id;
    v_u32 nr_entries;
    // virtio_gpu_mem_entry_t entries[nr_entries];
} __attribute__((packed)) virtio_gpu_resource_attach_backing_t;

// ============================================================
// ============================================================
// Курсор (cursorq)
// ============================================================
// Позиция курсора (scanout + координаты).
typedef struct {
    v_u32 scanout_id;
    v_u32 x;
    v_u32 y;
    v_u32 padding;
} __attribute__((packed)) virtio_gpu_cursor_pos_t;

// UPDATE_CURSOR / MOVE_CURSOR: одна и та же структура.
// UPDATE задаёт resource_id + hot_x/hot_y (форма). MOVE только позицию.
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_cursor_pos_t pos;
    v_u32 resource_id;
    v_u32 hot_x;
    v_u32 hot_y;
    v_u32 padding;
} __attribute__((packed)) virtio_gpu_update_cursor_t;

#define VIRTIO_GPU_CURSOR_RES_ID  2   // id ресурса курсора (1 - экран)
#define VIRTIO_GPU_CURSOR_SIZE    64  // курсор ОБЯЗАН быть 64x64

// Device config (читать из device_cfg)
// ============================================================
typedef struct {
    v_u32 events_read;
    v_u32 events_clear;
    v_u32 num_scanouts;
    v_u32 num_capsets;
    v_u32 blob_alignment;
} __attribute__((packed)) virtio_gpu_config_t;

#endif // VIRTIO_GPU_H
