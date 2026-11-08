// w_file_mysticos.c - WAD I/O для MysticOS (замена w_file_stdc.c).
// Использует нашу libc: fopen читает файл целиком в память (memory-backed),
// fseek/fread работают по этому буферу. Интерфейс - как w_file_stdc.
#include <stdio.h>
#include "doomgeneric/doomgeneric/m_misc.h"
#include "doomgeneric/doomgeneric/w_file.h"
#include "doomgeneric/doomgeneric/z_zone.h"

typedef struct {
    wad_file_t wad;
    FILE* fstream;
} stdc_wad_file_t;

extern wad_file_class_t stdc_wad_file;

static inline void ms_debug(const char* s) {
    __asm__ volatile ("int $0x80" :: "a"(700), "b"(s) : "memory");
}
static wad_file_t* W_StdC_OpenFile(char* path) {
    // Без serial-спама: fopen зовётся в горячем пути (лумпы текстур/спрайтов).
    FILE* fstream = fopen(path, "rb");
    if (fstream == NULL) { ms_debug("[w] WAD fopen FAIL\n"); return NULL; }
    ms_debug("[w] WAD fopen OK\n");
    stdc_wad_file_t* result = Z_Malloc(sizeof(stdc_wad_file_t), PU_STATIC, 0);
    result->wad.file_class = &stdc_wad_file;
    result->wad.mapped = NULL;
    result->wad.length = M_FileLength(fstream);
    result->fstream = fstream;
    return &result->wad;
}

static void W_StdC_CloseFile(wad_file_t* wad) {
    stdc_wad_file_t* s = (stdc_wad_file_t*)wad;
    fclose(s->fstream);
    Z_Free(s);
}

size_t W_StdC_Read(wad_file_t* wad, unsigned int offset,
                   void* buffer, size_t buffer_len) {
    stdc_wad_file_t* s = (stdc_wad_file_t*)wad;
    fseek(s->fstream, offset, SEEK_SET);
    return fread(buffer, 1, buffer_len, s->fstream);
}

wad_file_class_t stdc_wad_file = {
    W_StdC_OpenFile,
    W_StdC_CloseFile,
    W_StdC_Read,
};
