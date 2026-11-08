// music.h - приёмник музыки в ядре.
//
// Синтез (OPL3 + GENMIDI) делает Doom, шлёт готовый PCM сюда через
// SYS_MUSIC_PCM. HDA-миксер берёт сэмплы через music_next_sample().
#ifndef MUSIC_H
#define MUSIC_H

// Принять 16-бит моно PCM от программы (Doom). Возвращает число сэмплов.
int  music_feed(const short* buf, unsigned int n);

void music_stop(void);
int  music_playing(void);

// Следующий моно-отсчёт (-32768..32767). Зовётся из HDA-миксера (48 кГц).
int  music_next_sample(void);

#endif // MUSIC_H
