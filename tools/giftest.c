#include <stdio.h>
#include <stdlib.h>
#include "../gif.h"
int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "rb"); if (!f) { printf("no file\n"); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* d = malloc(sz); fread(d, 1, sz, f); fclose(f);
    printf("size=%ld\n", sz);
    gif_info_t gi;
    int rc = gif_open(d, (unsigned)sz, &gi);
    printf("gif_open rc=%d w=%d h=%d nframes=%d\n", rc, gi.width, gi.height, gi.nframes);
    if (rc) return 1;
    unsigned char out[64*64*3];
    int dl = gif_decode_frame(&gi, 0, out, 64, 64);
    printf("frame0 delay=%d\n", dl);
    return 0;
}
