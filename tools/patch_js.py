p='examples/minijs.h'
s=open(p,encoding='utf-8').read()
old='''        if(isop("(")){
            TP++; jsval args[8]; int na=0;
            if(!isop(")")){ if(na<8)args[na]=eval_expr(); na++; while(isop(",")){ TP++; if(na<8)args[na]=eval_expr(); na++; } }
            if(isop(")"))TP++;
            v=call_func(g_fname,args, na>8?8:na);
        }'''
new='''        if(isop("(")){
            char fnm[64]; scpy_(fnm,g_fname);   // имя ДО разбора аргументов: они портят g_fname
            TP++; jsval args[8]; int na=0;
            if(!isop(")")){ if(na<8)args[na]=eval_expr(); na++; while(isop(",")){ TP++; if(na<8)args[na]=eval_expr(); na++; } }
            if(isop(")"))TP++;
            v=call_func(fnm,args, na>8?8:na);
        }'''
print('count', s.count(old))
if s.count(old)==1:
    s=s.replace(old,new,1)
    open(p,'w',encoding='utf-8').write(s)
    print('patched')
else:
    print('skip')
