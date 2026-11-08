p='kernel.c'
s=open(p,encoding='utf-8').read()
anchor='static int api_http_start(const char* url) {'
func='''// Разбор IPv4-литерала a.b.c.d -> ip[4]. 0 = успех, -1 = не IP-адрес.
static int parse_ipv4(const char* s, unsigned char* ip) {
    int part = 0, val = 0, digits = 0;
    for (const char* p = s; ; p++) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            if (val > 255) return -1;
            if (++digits > 3) return -1;
        } else if (*p == '.' || *p == 0) {
            if (digits == 0) return -1;
            if (part > 3) return -1;
            ip[part++] = (unsigned char)val;
            val = 0; digits = 0;
            if (*p == 0) break;
        } else {
            return -1;
        }
    }
    return (part == 4) ? 0 : -1;
}

'''
print('anchor count', s.count(anchor), 'already', 'parse_ipv4(const char* s' in s)
if s.count(anchor)==1 and 'parse_ipv4(const char* s' not in s:
    s=s.replace(anchor, func+anchor, 1)
    open(p,'w',encoding='utf-8').write(s)
    print('patched')
else:
    print('skip')
