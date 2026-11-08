// gpu.c - virtio-gpu драйвер MysticOS 2 (2D-вывод через GPU).
//
// Путь A: создаём 2D-ресурс размером с экран, привязываем к нему
// backbuffer (0x1000000), показываем через SET_SCANOUT, и обновляем
// экран через TRANSFER_TO_HOST_2D + RESOURCE_FLUSH.
//
// 3D/virgl (blur, тени на GPU) — отдельно позже.
//
// Референс: Linux drivers/gpu/drm/virtio/virtgpu_vq.c
#include "virtio.h"
#include "virtio_gpu.h"
#include "serial.h"
#include "pmem.h"

// --- Состояние GPU ---
static virtio_dev_t g_gpu;
static int          g_gpu_ok = 0;
// Аппаратный курсор (cursorq). 0 = software (см. авто-детект в gpu_init).
int g_cursor_hw = 0;
static v_u32        g_res_id = 1;      // id нашего экранного ресурса
static v_u32        g_scanout_w = 0;
static v_u32        g_scanout_h = 0;

// Backbuffer ядра (24bpp, см. vbe.c BB_ADDR).
#define BB_SRC_ADDR   0x01000000u
// Staging-буфер 32bpp для GPU (24->32 конвертация). 0x02000000 свободен в M2.
#define GPU_BB_ADDR   0x02000000u

// Память для virtqueue controlq (лежит в pmem-блоке, выровнена).
// controlq: 64 дескриптора. desc=16б*64=1КБ, avail~128+128, used~...
// Возьмём один блок pmem.
static void* g_qmem = 0;   // desc | avail | used

// ---- debug-вывод ----
static void gp(const char* s){ serial_print(s); }
static void gpi(int v){ serial_print_int(v); }
static void gph(v_u32 v){ serial_print_hex(v); }

// ============================================================
// Отправка одной команды и ожидание ответа.
// Формирует цепочку: [cmd out] -> [resp in], шлёт, ждёт ответ.
// Возвращает тип ответа (0 = таймаут).
// ============================================================
// resp_buf должен быть >= 64 байт (макс ответ 2D-команд — NODATA 24 байта).
static v_u32 gpu_cmd(void* cmd, v_u32 cmd_len, void* resp, v_u32 resp_len) {
    v_u64 addrs[2];
    v_u32 lens[2];
    v_u16 flags[2];

    addrs[0] = (v_u64)(v_u32)cmd;   lens[0] = cmd_len;  flags[0] = 0;                 // out
    addrs[1] = (v_u64)(v_u32)resp;  lens[1] = resp_len; flags[1] = VIRTQ_DESC_F_WRITE; // in

    int idx = virtio_queue_add_chain(&g_gpu, 0, addrs, lens, flags, 2);
    if (idx < 0) { gp("[gpu] queue_add_chain fail\n"); return 0; }

    virtio_queue_notify(&g_gpu, 0);

    // Ждём ответ (poll). Команды 2D быстрые, но дадим время.
    for (int spin = 0; spin < 5000000; spin++) {
        v_u32 rlen = 0;
        int id = virtio_queue_wait(&g_gpu, 0, &rlen);
        if (id >= 0) {
            virtio_gpu_ctrl_hdr_t* h = (virtio_gpu_ctrl_hdr_t*)resp;
            return h->type;
        }
    }
    gp("[gpu] cmd timeout\n");
    return 0;
}

// ============================================================
// init: найти GPU, virtio_init, настроить controlq, driver_ok.
// ============================================================
int gpu_init(void) {
    gp("[gpu] probing virtio-gpu (0x1050)...\n");

    if (!virtio_find_pci(0x1050, &g_gpu)) {
        gp("[gpu] not found\n");
        return 0;
    }
    gp("[gpu] found at pci ");
    gpi(g_gpu.pci_bus); gp(":"); gpi(g_gpu.pci_slot); gp("."); gpi(g_gpu.pci_func);
    gp("\n");

    // Авто-детект курсора (как KDE): на виртуалке (QEMU virtio-gpu,
    // vendor 0x1AF4) hardware-курсор невидим -> software.
    // На реальном GPU (напр. AMD 0x1002) -> hardware (cursorq).
    if (g_gpu.pci_vendor == 0x1AF4) {
        g_cursor_hw = 0;
        gp("[gpu] virtual GPU -> software cursor\n");
    } else {
        g_cursor_hw = 1;
        gp("[gpu] real GPU -> hardware cursor\n");
    }

    // Хотим modern transport, без VIRGL (нам 2D).
    // VIRTIO_F_VERSION_1 = bit 32 (обязателен для modern).
    if (!virtio_init(&g_gpu, (v_u64)1 << 32)) {
        gp("[gpu] virtio_init fail\n");
        return 0;
    }
    gp("[gpu] virtio_init ok\n");

    // Память под controlq. Возьмём блоки из pmem (4МБ блоки).
    // desc: 64*16=1024, avail: 6+64*2=134, used: 6+64*8=518.
    // Всё влезает в 4КБ. Возьмём первый pmem-блок.
    unsigned int block = pmem_alloc_block();
    if (!block) { gp("[gpu] pmem_alloc_block fail\n"); return 0; }
    g_qmem = (void*)block;
    void* desc  = g_qmem;
    void* avail = (void*)((v_u32)g_qmem + 2048);   // выравнивание 2 (на 2048 ок)
    void* used  = (void*)((v_u32)g_qmem + 3072);   // выравнивание 4

    if (!virtio_queue_setup(&g_gpu, 0, 64, desc, avail, used)) {
        gp("[gpu] queue_setup fail\n");
        return 0;
    }
    gp("[gpu] controlq ready\n");

    // cursorq (queue 1): быстрый канал для курсора.
    // Нужна отдельная память (desc/avail/used).
    unsigned int block2 = pmem_alloc_block();
    if (block2) {
        void* cdesc  = (void*)block2;
        void* cavail = (void*)(block2 + 2048);
        void* cused  = (void*)(block2 + 3072);
        if (virtio_queue_setup(&g_gpu, 1, 64, cdesc, cavail, cused))
            gp("[gpu] cursorq ready\n");
        else
            gp("[gpu] cursorq setup fail\n");
    }

    virtio_driver_ok(&g_gpu);
    gp("[gpu] driver_ok\n");
    g_gpu_ok = 1;
    return 1;
}

// ============================================================
// Создать ресурс = экран и привязать backbuffer.
// ============================================================
int gpu_setup_framebuffer(v_u32 w, v_u32 h) {
    if (!g_gpu_ok) return 0;
    g_scanout_w = w; g_scanout_h = h;

    // Ответный буфер (общий для всех команд).
    static v_u8 resp[256] __attribute__((aligned(16)));

    // 1. RESOURCE_CREATE_2D
    static virtio_gpu_resource_create_2d_t cmd1 __attribute__((aligned(16)));
    cmd1.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd1.hdr.flags = 0; cmd1.hdr.fence_id = 0; cmd1.hdr.ctx_id = 0; cmd1.hdr.ring_idx = 0;
    cmd1.resource_id = g_res_id;
    cmd1.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    cmd1.width = w;
    cmd1.height = h;
    v_u32 r = gpu_cmd(&cmd1, sizeof(cmd1), resp, sizeof(resp));
    if (r != VIRTIO_GPU_RESP_OK_NODATA) {
        gp("[gpu] create_2d fail, resp="); gph(r); gp("\n");
        return 0;
    }
    gp("[gpu] resource created\n");

    // 2. ATTACH_BACKING: hdr + resource_id + nr_entries + mem_entry.
    static struct {
        virtio_gpu_ctrl_hdr_t hdr;
        v_u32 resource_id;
        v_u32 nr_entries;
        virtio_gpu_mem_entry_t entry;
    } __attribute__((packed, aligned(16))) cmd2;
    cmd2.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd2.hdr.flags = 0; cmd2.hdr.fence_id = 0; cmd2.hdr.ctx_id = 0; cmd2.hdr.ring_idx = 0;
    cmd2.resource_id = g_res_id;
    cmd2.nr_entries = 1;
    cmd2.entry.addr = GPU_BB_ADDR;
    cmd2.entry.length = w * h * 4;
    cmd2.entry.padding = 0;
    r = gpu_cmd(&cmd2, sizeof(cmd2), resp, sizeof(resp));
    if (r != VIRTIO_GPU_RESP_OK_NODATA) {
        gp("[gpu] attach_backing fail, resp="); gph(r); gp("\n");
        return 0;
    }
    gp("[gpu] backing attached\n");

    // 3. SET_SCANOUT: показать ресурс на scanout 0.
    static virtio_gpu_set_scanout_t cmd3 __attribute__((aligned(16)));
    cmd3.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd3.hdr.flags = 0; cmd3.hdr.fence_id = 0; cmd3.hdr.ctx_id = 0; cmd3.hdr.ring_idx = 0;
    cmd3.r.x = 0; cmd3.r.y = 0; cmd3.r.width = w; cmd3.r.height = h;
    cmd3.scanout_id = 0;
    cmd3.resource_id = g_res_id;
    r = gpu_cmd(&cmd3, sizeof(cmd3), resp, sizeof(resp));
    if (r != VIRTIO_GPU_RESP_OK_NODATA) {
        gp("[gpu] set_scanout fail, resp="); gph(r); gp("\n");
        return 0;
    }
    gp("[gpu] scanout set\n");
    return 1;
}

// ============================================================
// Показать область backbuffer на экране (transfer + flush).
// Вызывается вместо fb_flip() (или вместе с ним).
// ============================================================
void gpu_flush(v_u32 x, v_u32 y, v_u32 w, v_u32 h) {
    if (!g_gpu_ok) return;
    static int dbg_once = 0;
    if (!dbg_once) { dbg_once = 1; gp("[gpu] first flush\n"); }
    static v_u8 resp[256] __attribute__((aligned(16)));

    // Конвертация 24bpp (backbuffer ядра) -> 32bpp BGRA (staging для GPU).
    // Пока без оптимизаций: раскрываем весь экран. Позже - только регион.
    {
        // Конвертируем ТОЛЬКО регион (x,y,w,h) 24->32. Для курсора это
        // маленькая область -> быстро. Строки идут с шагом scanout_w.
        for (unsigned int row = 0; row < h; row++) {
            const unsigned char* s = (const unsigned char*)BB_SRC_ADDR
                + ((v_u64)(y + row) * g_scanout_w + x) * 3u;
            v_u32* d = (v_u32*)((unsigned char*)GPU_BB_ADDR
                + ((v_u64)(y + row) * g_scanout_w + x) * 4u);
            // 24->32: одна 32-битная запись на пиксель вместо 4 байтовых.
            // BGRA little-endian = B | G<<8 | R<<16 | A<<24; A=0 (не важно для XRGB).
            for (unsigned int i = 0; i < w; i++) {
                d[i] = (v_u32)s[0] | ((v_u32)s[1] << 8) | ((v_u32)s[2] << 16);
                s += 3;
            }
        }
    }

    // TRANSFER_TO_HOST_2D: offset = (y*scanout_w + x)*4 (байты).
    static virtio_gpu_transfer_to_host_2d_t cmd __attribute__((aligned(16)));
    cmd.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd.hdr.flags = 0; cmd.hdr.fence_id = 0; cmd.hdr.ctx_id = 0; cmd.hdr.ring_idx = 0;
    cmd.r.x = x; cmd.r.y = y; cmd.r.width = w; cmd.r.height = h;
    cmd.offset = (v_u64)((y * g_scanout_w + x) * 4u);
    cmd.resource_id = g_res_id;
    cmd.padding = 0;
    gpu_cmd(&cmd, sizeof(cmd), resp, sizeof(resp));

    // RESOURCE_FLUSH: обновить экран.
    static virtio_gpu_resource_flush_t flush __attribute__((aligned(16)));
    flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush.hdr.flags = 0; flush.hdr.fence_id = 0; flush.hdr.ctx_id = 0; flush.hdr.ring_idx = 0;
    flush.r.x = x; flush.r.y = y; flush.r.width = w; flush.r.height = h;
    flush.resource_id = g_res_id;
    flush.padding = 0;
    gpu_cmd(&flush, sizeof(flush), resp, sizeof(resp));
}

// ============================================================
// Аппаратный курсор (cursorq = queue 1)
// ============================================================
// Ресурс курсора: 64x64 BGRA в staging-буфере (после экранного).
// Экран: 800x600x4 = 1.87MB. Курсор: 64x64x4 = 16KB.
#define CURSOR_BUF_ADDR  (GPU_BB_ADDR + 0x00200000u)   // +2MB от начала staging

// Отправка в cursorq (queue 1). ВАЖНО: Linux шлёт ТОЛЬКО out-дескриптор,
// БЕЗ in-ответа (virtio_gpu_queue_cursor: outcnt=1, incnt=0) и НЕ ждёт.
// Раньше мы добавляли in -> chain был неверный -> QEMU игнорировал.
static void gpu_cursor_cmd(void* cmd, v_u32 cmd_len) {
    v_u64 addrs[1]; v_u32 lens[1]; v_u16 flags[1];
    addrs[0] = (v_u64)(v_u32)cmd;   lens[0] = cmd_len;  flags[0] = 0;
    int idx = virtio_queue_add_chain(&g_gpu, 1, addrs, lens, flags, 1);
    if (idx < 0) return;
    virtio_queue_notify(&g_gpu, 1);
    // Ответа не ждём (fire-and-forget).
}

// Инициализировать ресурс курсора (64x64) и залить его стрелкой.
int gpu_cursor_init(void) {
    if (!g_gpu_ok) return 0;
    static v_u8 resp[256] __attribute__((aligned(16)));

    // 1. Создать ресурс 64x64 BGRA (через controlq).
    static virtio_gpu_resource_create_2d_t c1 __attribute__((aligned(16)));
    c1.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    c1.hdr.flags = 0; c1.hdr.fence_id = 0; c1.hdr.ctx_id = 0; c1.hdr.ring_idx = 0;
    c1.resource_id = VIRTIO_GPU_CURSOR_RES_ID;
    c1.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    c1.width = VIRTIO_GPU_CURSOR_SIZE;
    c1.height = VIRTIO_GPU_CURSOR_SIZE;
    if (gpu_cmd(&c1, sizeof(c1), resp, sizeof(resp)) != VIRTIO_GPU_RESP_OK_NODATA) {
        gp("[gpu] cursor create fail\n"); return 0;
    }

    // 2. Залить картинку курсора в CURSOR_BUF_ADDR (BGRA).
    //    Стрелка: белая с чёрным контуром, в левом верхнем углу 12x19.
    {
        unsigned char* buf = (unsigned char*)CURSOR_BUF_ADDR;
        for (unsigned int i = 0; i < VIRTIO_GPU_CURSOR_SIZE * VIRTIO_GPU_CURSOR_SIZE; i++) {
            buf[i*4+0] = 0; buf[i*4+1] = 0; buf[i*4+2] = 0; buf[i*4+3] = 0; // прозрачно
        }
        // Маска стрелки (совпадает с cursor.c CUR).
        static const unsigned char cur[19][12] = {
            {2,0,0,0,0,0,0,0,0,0,0,0},{2,2,0,0,0,0,0,0,0,0,0,0},{2,1,2,0,0,0,0,0,0,0,0,0},
            {2,1,1,2,0,0,0,0,0,0,0,0},{2,1,1,1,2,0,0,0,0,0,0,0},{2,1,1,1,1,2,0,0,0,0,0,0},
            {2,1,1,1,1,1,2,0,0,0,0,0},{2,1,1,1,1,1,1,2,0,0,0,0},{2,1,1,1,1,1,1,1,2,0,0,0},
            {2,1,1,1,1,1,1,1,1,2,0,0},{2,1,1,1,1,1,1,1,1,1,2,0},{2,1,1,1,1,1,1,2,2,2,2,2},
            {2,1,1,1,2,1,1,2,0,0,0,0},{2,1,1,2,0,2,1,1,2,0,0,0},{2,1,2,0,0,2,1,1,2,0,0,0},
            {2,2,0,0,0,0,2,1,1,2,0,0},{0,0,0,0,0,0,2,1,1,2,0,0},{0,0,0,0,0,0,0,2,1,2,0,0},
            {0,0,0,0,0,0,0,2,2,0,0,0}
        };
        for (int r = 0; r < 19; r++) {
            for (int c = 0; c < 12; c++) {
                unsigned char m = cur[r][c];
                if (m == 0) continue;
                unsigned char* p = buf + ((unsigned int)r * 64u + (unsigned int)c) * 4u;
                if (m == 1) { p[0]=0xFF; p[1]=0xFF; p[2]=0xFF; p[3]=0xFF; } // белый
                else        { p[0]=0x00; p[1]=0x00; p[2]=0x00; p[3]=0xFF; } // чёрный контур
            }
        }
    }

    // 3. ATTACH_BACKING для ресурса курсора.
    static struct {
        virtio_gpu_ctrl_hdr_t hdr;
        v_u32 resource_id; v_u32 nr_entries;
        virtio_gpu_mem_entry_t entry;
    } __attribute__((packed, aligned(16))) c2;
    c2.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    c2.hdr.flags = 0; c2.hdr.fence_id = 0; c2.hdr.ctx_id = 0; c2.hdr.ring_idx = 0;
    c2.resource_id = VIRTIO_GPU_CURSOR_RES_ID;
    c2.nr_entries = 1;
    c2.entry.addr = CURSOR_BUF_ADDR;
    c2.entry.length = VIRTIO_GPU_CURSOR_SIZE * VIRTIO_GPU_CURSOR_SIZE * 4;
    c2.entry.padding = 0;
    if (gpu_cmd(&c2, sizeof(c2), resp, sizeof(resp)) != VIRTIO_GPU_RESP_OK_NODATA) {
        gp("[gpu] cursor attach fail\n"); return 0;
    }

    // 4. TRANSFER курсора в host.
    static virtio_gpu_transfer_to_host_2d_t c3 __attribute__((aligned(16)));
    c3.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    c3.hdr.flags = 0; c3.hdr.fence_id = 0; c3.hdr.ctx_id = 0; c3.hdr.ring_idx = 0;
    c3.r.x = 0; c3.r.y = 0; c3.r.width = VIRTIO_GPU_CURSOR_SIZE; c3.r.height = VIRTIO_GPU_CURSOR_SIZE;
    c3.offset = 0;
    c3.resource_id = VIRTIO_GPU_CURSOR_RES_ID;
    c3.padding = 0;
    gpu_cmd(&c3, sizeof(c3), resp, sizeof(resp));

    // UPDATE_CURSOR: задать GPU ресурс курсора + hot spot (без этого MOVE
    // не покажет курсор).
    static virtio_gpu_update_cursor_t upd __attribute__((aligned(16)));
    upd.hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
    upd.hdr.flags = 0; upd.hdr.fence_id = 0; upd.hdr.ctx_id = 0; upd.hdr.ring_idx = 0;
    upd.pos.scanout_id = 0;
    upd.pos.x = 0; upd.pos.y = 0; upd.pos.padding = 0;
    upd.resource_id = VIRTIO_GPU_CURSOR_RES_ID;
    upd.hot_x = 0; upd.hot_y = 0;
    upd.padding = 0;
    gpu_cursor_cmd(&upd, sizeof(upd));

    gp("[gpu] cursor resource ready\n");
    return 1;
}

// Переместить курсор (MOVE_CURSOR в cursorq).
// Флаг: 1 = аппаратный курсор (cursorq). 0 = выключен (QEMU двигает
// хостовый курсор - сбивает, поэтому в QEMU ставим 0).
// g_cursor_hw объявлен вверху файла (нужен в gpu_init для авто-детекта).
int gpu_cursor_hw_enabled(void) { return g_cursor_hw; }

void gpu_cursor_move(int x, int y) {
    if (!g_gpu_ok) return;
    if (!g_cursor_hw) return;   // аппаратный курсор выключен -> не трогаем host
    static int cm_once = 0;
    if (!cm_once) { cm_once = 1; gp("[gpu] first cursor move x="); gpi(x); gp(" y="); gpi(y); gp("\n"); }
    static virtio_gpu_update_cursor_t mv __attribute__((aligned(16)));
    mv.hdr.type = VIRTIO_GPU_CMD_MOVE_CURSOR;
    mv.hdr.flags = 0; mv.hdr.fence_id = 0; mv.hdr.ctx_id = 0; mv.hdr.ring_idx = 0;
    mv.pos.scanout_id = 0;
    mv.pos.x = (v_u32)x;
    mv.pos.y = (v_u32)y;
    mv.pos.padding = 0;
    // resource_id ДОЛЖЕН быть id ресурса курсора: 0 = "скрыть курсор".
    mv.resource_id = VIRTIO_GPU_CURSOR_RES_ID;
    mv.hot_x = 0; mv.hot_y = 0; mv.padding = 0;
    gpu_cursor_cmd(&mv, sizeof(mv));
}

int gpu_is_ready(void) { return g_gpu_ok; }
