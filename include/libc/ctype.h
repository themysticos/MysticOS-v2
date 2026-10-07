// libc/ctype.h - классификация символов (общая libc MysticOS).
#ifndef LIBC_CTYPE_H
#define LIBC_CTYPE_H

int isspace(int c);
int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isupper(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int toupper(int c);
int tolower(int c);

// glibc-специфика: Doom зовёт __ctype_b_loc() (таблица классификации).
// Дадим минимальную заглушку.
const unsigned short** __ctype_b_loc(void);

#endif // LIBC_CTYPE_H
