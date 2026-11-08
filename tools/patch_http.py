p='kernel.c'
s=open(p,encoding='utf-8').read()
old="""    unsigned char ip[4];
    if (net_dns_resolve(host, ip) != 0) { g_http_state = 3; return -1; }"""
new="""    unsigned char ip[4];
    // Если host — это IP-литерал (напр. 10.0.2.2), НЕ идём в DNS, а парсим.
    // Иначе браузер резолвил бы даже числовой адрес как имя хоста.
    if (parse_ipv4(host, ip) != 0) {
        if (net_dns_resolve(host, ip) != 0) { g_http_state = 3; return -1; }
    }"""
print('old count', s.count(old))
if s.count(old)==1:
    s=s.replace(old,new,1)
    open(p,'w',encoding='utf-8').write(s)
    print('patched')
else:
    print('skip')
