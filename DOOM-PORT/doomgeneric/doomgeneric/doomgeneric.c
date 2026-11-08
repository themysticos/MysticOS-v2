#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"

pixel_t* DG_ScreenBuffer = NULL;

// Рантайм-разрешение кадра (задаётся платформой под размер окна).
int DOOMGENERIC_RESX = 640;
int DOOMGENERIC_RESY = 400;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
    ms_debug("[dg] create start\n");
    myargc = argc;
    myargv = argv;

    ms_debug("[dg] M_FindResponseFile...\n");
    M_FindResponseFile();

    // DG_Init создаёт окно и выставляет DOOMGENERIC_RESX/RESY под его
    // клиентскую область — ДО аллокации буфера кадра.
    ms_debug("[dg] DG_Init...\n");
    DG_Init();

    ms_debug("[dg] malloc screenbuffer...\n");
    DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
    if (!DG_ScreenBuffer) ms_debug("[dg] SCREENBUFFER MALLOC FAILED!\n");
    else ms_debug("[dg] screenbuffer ok\n");

    ms_debug("[dg] D_DoomMain...\n");
    D_DoomMain ();
    ms_debug("[dg] D_DoomMain returned!\n");
}
