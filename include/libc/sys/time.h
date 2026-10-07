// libc/sys/time.h - заглушка.
#ifndef LIBC_SYS_TIME_H
#define LIBC_SYS_TIME_H

struct timeval { long tv_sec; long tv_usec; };
struct timezone { int tz_minuteswest; int tz_dsttime; };

int gettimeofday(struct timeval* tv, struct timezone* tz);

#endif
