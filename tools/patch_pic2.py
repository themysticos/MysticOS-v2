p='pic.c'
s=open(p,encoding='utf-8').read()
old="""        else if (pressed && sc < 128) {
            char c = sc_lower[sc];
            if (c >= 'a' && c <= 'z' && (shift ^ caps)) c -= 32;
            if (c) kb_push(c);
        }"""
new="""        else if (pressed && sc < 128) {
            char c;
            if (shift) {
                c = sc_shift[sc];
                if (!c) c = sc_lower[sc];   // нет shift-версии (стрелки/спец)
            } else {
                c = sc_lower[sc];
                if (c >= 'a' && c <= 'z' && caps) c -= 32;
            }
            if (c) kb_push(c);
        }"""
print('old count', s.count(old))
if s.count(old)==1:
    s=s.replace(old,new,1)
    open(p,'w',encoding='utf-8').write(s)
    print('patched')
else:
    print('skip')
