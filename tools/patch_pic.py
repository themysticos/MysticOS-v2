import io
p='pic.c'
s=open(p,encoding='utf-8').read()
anchor="    0,'\\\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '\n};"
print('anchor count', s.count(anchor))
if s.count(anchor)==1 and 'sc_shift[128]' not in s:
    shift=anchor+"\n// Shift-символы (0 = нет shift-версии).\nstatic const char sc_shift[128] = {\n    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\\b',\n    '\\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\\n',\n    0,'A','S','D','F','G','H','J','K','L',':','\"','~',\n    0,'|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '\n};"
    s=s.replace(anchor,shift,1)
    open(p,'w',encoding='utf-8').write(s)
    print('patched')
else:
    print('skip')
