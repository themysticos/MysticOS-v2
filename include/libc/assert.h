// libc/assert.h - проверки.
#ifndef LIBC_ASSERT_H
#define LIBC_ASSERT_H

void __libc_assert_fail(const char* expr, const char* file, int line);

#define assert(e) ((e) ? (void)0 : __libc_assert_fail(#e, __FILE__, __LINE__))

#endif
