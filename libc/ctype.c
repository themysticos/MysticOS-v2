// libc/ctype.c - классификация символов (общая libc MysticOS).
#include "../include/libc/ctype.h"

int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isprint(int c) { return c >= 32 && c < 127; }
int ispunct(int c) { return isprint(c) && !isalnum(c) && c != ' '; }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }

// glibc-совместимая таблица классификации: _ISbit(i) = 1<<i.
// Биты (glibc): 0=upper,1=lower,2=alpha,3=digit,4=xdigit,5=space,
// 6=print,7=graph,8=blank,9=cntrl,10=punct,11=alnum.
static const unsigned short ctype_table[384] = {0};
static const unsigned short* ctype_ptr = ctype_table + 128;
const unsigned short** __ctype_b_loc(void) { return &ctype_ptr; }
