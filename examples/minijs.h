// examples/minijs.h - мини-интерпретатор подмножества JavaScript.
// НЕ полноценный JS: нет DOM/объектов/замыканий/промисов. Хватает для
// простых <script>: переменные, арифметика, строки, if/while/for,
// функции, return, document.write/alert/console.log, .length, Math.*.
#ifndef MINIJS_H
#define MINIJS_H

#define JS_MAXTOK 4096
#define JS_VSTRLEN 64

#define JT_NUM 1
#define JT_STR 2
#define JT_ID  3
#define JT_OP  4
#define JT_EOF 5

typedef struct { int t; long n; char s[JS_VSTRLEN]; } jsval;
typedef struct { int t; long n; char s[JS_VSTRLEN]; } jstok;
typedef struct { char name[32]; jsval v; } jsvar;
typedef struct { char name[32]; char params[4][16]; int nparams; int body_start, body_end; } jsfunc;

static jstok T[JS_MAXTOK];
static int NT, TP;
static jsvar g_vars[128]; static int g_nvars;
static jsfunc g_funcs[64]; static int g_nfuncs;
static char g_fname[64];
static char g_lname[32];
static int g_returning; static jsval g_retval;
static char* g_obuf; static int g_ocap, g_olen;

static int slen_(const char* s){ int n=0; while(s[n])n++; return n; }
static int streq_(const char* a,const char* b){ while(*a&&*b){ if(*a!=*b)return 0; a++;b++; } return *a==*b; }
static void scpy_(char* d,const char* s){ int i=0; while(s[i]&&i<JS_VSTRLEN-1){d[i]=s[i];i++;} d[i]=0; }
static void scat_(char* d,const char* s){ int i=slen_(d); int j=0; while(s[j]&&i<JS_VSTRLEN-1){d[i++]=s[j++];} d[i]=0; }
static jsval vundef(void){ jsval v; v.t=2; v.n=0; v.s[0]=0; return v; }
static long atoi_(const char* s){ long v=0; int neg=0; while(*s==' ')s++; if(*s=='-'){neg=1;s++;}else if(*s=='+')s++; while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;} return neg?-v:v; }
static long tonum(jsval v){ if(v.t==0) return v.n; if(v.t==1) return atoi_(v.s); return 0; }
static void num2str(long v,char* b){ if(v==0){b[0]='0';b[1]=0;return;} int neg=v<0; unsigned long u=neg?(unsigned long)(-v):(unsigned long)v; char t[24]; int k=0; while(u){t[k++]=(char)('0'+u%10);u/=10;} int o=0; if(neg)b[o++]='-'; while(k)b[o++]=t[--k]; b[o]=0; }
static void v2s(jsval v,char* b){ if(v.t==1) scpy_(b,v.s); else if(v.t==0) num2str(v.n,b); else b[0]=0; }
static int truthy(jsval v){ if(v.t==0) return v.n!=0; if(v.t==1) return v.s[0]!=0; return 0; }
static void emit(const char* s){ for(int i=0; s[i]; i++){ if(g_olen<g_ocap-1) g_obuf[g_olen++]=s[i]; } }
static jsval* findvar(const char* name){ for(int i=0;i<g_nvars;i++) if(streq_(g_vars[i].name,name)) return &g_vars[i].v; return 0; }
static void setvar(const char* name, jsval v){ for(int i=0;i<g_nvars;i++) if(streq_(g_vars[i].name,name)){ g_vars[i].v=v; return; } if(g_nvars<128){ scpy_(g_vars[g_nvars].name,name); g_vars[g_nvars].v=v; g_nvars++; } }
static int isop(const char* op){ return T[TP].t==JT_OP && streq_(T[TP].s,op); }

static jsval eval_expr(void);
static void exec_stmt(void);
static void skip_stmt(void);
static jsval call_func(const char* name, jsval* args, int n);

static jsval eval_primary(void){
    jsval v=vundef();
    if(T[TP].t==JT_NUM){ v.t=0; v.n=T[TP].n; TP++; g_lname[0]=0; }
    else if(T[TP].t==JT_STR){ v.t=1; scpy_(v.s,T[TP].s); TP++; g_lname[0]=0; }
    else if(T[TP].t==JT_ID){
        char name[32]; scpy_(name,T[TP].s);
        if(streq_(name,"true")){v.t=0;v.n=1;TP++;g_lname[0]=0;}
        else if(streq_(name,"false")){v.t=0;v.n=0;TP++;g_lname[0]=0;}
        else { scpy_(g_fname,name); scpy_(g_lname,name); jsval* pv=findvar(name); if(pv)v=*pv; TP++; }
    }
    else if(isop("(")){ TP++; v=eval_expr(); if(isop(")"))TP++; g_lname[0]=0; }
    return v;
}

static jsval eval_postfix(void){
    jsval v=eval_primary();
    for(;;){
        if(isop("(")){
            char fnm[64]; scpy_(fnm,g_fname);   // имя ДО разбора аргументов: они портят g_fname
            TP++; jsval args[8]; int na=0;
            if(!isop(")")){ if(na<8)args[na]=eval_expr(); na++; while(isop(",")){ TP++; if(na<8)args[na]=eval_expr(); na++; } }
            if(isop(")"))TP++;
            v=call_func(fnm,args, na>8?8:na);
        } else if(isop(".")){
            TP++; char m[32]; m[0]=0; if(T[TP].t==JT_ID){ scpy_(m,T[TP].s); TP++; }
            if(streq_(g_fname,"Math")||streq_(g_fname,"console")||streq_(g_fname,"document")){
                char tmp[64]; scpy_(tmp,g_fname); scat_(tmp,"."); scat_(tmp,m); scpy_(g_fname,tmp); v=vundef();
            } else if(streq_(m,"length")){
                jsval r; r.t=0; r.n=(v.t==1)?(long)slen_(v.s):0; v=r;
            } else v=vundef();
        } else if(isop("[")){
            TP++; jsval idx=eval_expr(); if(isop("]"))TP++;
            if(v.t==1){ long k=tonum(idx); long L=slen_(v.s); if(k>=0&&k<L){ jsval r; r.t=1; r.s[0]=v.s[k]; r.s[1]=0; v=r; } else v=vundef(); }
            else v=vundef();
        } else if(isop("++")){
            TP++; if(g_lname[0]){ jsval* pv=findvar(g_lname); if(pv){ long x=tonum(*pv)+1; pv->t=0; pv->n=x; } }
        } else if(isop("--")){
            TP++; if(g_lname[0]){ jsval* pv=findvar(g_lname); if(pv){ long x=tonum(*pv)-1; pv->t=0; pv->n=x; } }
        } else break;
    }
    return v;
}

static jsval eval_unary(void){
    if(isop("!")){ TP++; jsval v=eval_unary(); jsval r; r.t=0; r.n=!truthy(v); return r; }
    if(isop("-")){ TP++; jsval v=eval_unary(); jsval r; r.t=0; r.n=-tonum(v); return r; }
    if(isop("+")){ TP++; return eval_unary(); }
    return eval_postfix();
}
static jsval eval_mul(void){
    jsval l=eval_unary();
    while(isop("*")||isop("/")||isop("%")){
        char op=T[TP].s[0]; TP++; jsval r=eval_unary(); long x=tonum(l),y=tonum(r); jsval res; res.t=0;
        if(op=='*')res.n=x*y; else if(op=='/')res.n=(y==0)?0:x/y; else res.n=(y==0)?0:x%y; l=res;
    }
    return l;
}
static jsval eval_add(void){
    jsval l=eval_mul();
    while(isop("+")||isop("-")){
        char op=T[TP].s[0]; TP++; jsval r=eval_mul();
        if(op=='+'&&(l.t==1||r.t==1)){ char a[JS_VSTRLEN],b[JS_VSTRLEN]; v2s(l,a); v2s(r,b); jsval res; res.t=1; scpy_(res.s,a); scat_(res.s,b); l=res; }
        else { long x=tonum(l),y=tonum(r); jsval res; res.t=0; res.n=(op=='+')?x+y:x-y; l=res; }
    }
    return l;
}
static jsval eval_rel(void){
    jsval l=eval_add();
    while(isop("<")||isop(">")||isop("<=")||isop(">=")){
        char op0=T[TP].s[0], op1=T[TP].s[1]; TP++; jsval r=eval_add(); long x=tonum(l),y=tonum(r); int res=0;
        if(op0=='<'&&op1=='=')res=x<=y; else if(op0=='>'&&op1=='=')res=x>=y; else if(op0=='<')res=x<y; else res=x>y;
        jsval v; v.t=0; v.n=res; l=v;
    }
    return l;
}
static jsval eval_eq(void){
    jsval l=eval_rel();
    while(isop("==")||isop("!=")){
        int eq=streq_(T[TP].s,"=="); TP++; jsval r=eval_rel(); int res;
        if(l.t==1||r.t==1){ char a[JS_VSTRLEN],b[JS_VSTRLEN]; v2s(l,a); v2s(r,b); res=streq_(a,b); }
        else res= tonum(l)==tonum(r);
        jsval v; v.t=0; v.n= eq?res:!res; l=v;
    }
    return l;
}
static jsval eval_and(void){ jsval l=eval_eq(); while(isop("&&")){ TP++; jsval r=eval_eq(); jsval v; v.t=0; v.n=truthy(l)&&truthy(r); l=v; } return l; }
static jsval eval_or(void){ jsval l=eval_and(); while(isop("||")){ TP++; jsval r=eval_and(); jsval v; v.t=0; v.n=truthy(l)||truthy(r); l=v; } return l; }
static jsval eval_expr(void){
    if(T[TP].t==JT_ID && T[TP+1].t==JT_OP && streq_(T[TP+1].s,"=")){
        char name[32]; scpy_(name,T[TP].s); TP+=2; jsval v=eval_expr(); setvar(name,v); return v;
    }
    return eval_or();
}

static jsval call_func(const char* name, jsval* args, int n){
    jsval r=vundef();
    if(streq_(name,"alert")||streq_(name,"print")||streq_(name,"console.log")||streq_(name,"document.write")){
        for(int i=0;i<n;i++){ char b[JS_VSTRLEN]; v2s(args[i],b); emit(b); }
        return r;
    }
    if(streq_(name,"Math.floor")||streq_(name,"Math.round")||streq_(name,"Math.abs")){
        long x=n>0?tonum(args[0]):0; if(streq_(name,"Math.abs")&&x<0)x=-x; r.t=0;r.n=x;return r;
    }
    if(streq_(name,"Math.max")){ long a=n>0?tonum(args[0]):0,b=n>1?tonum(args[1]):0; r.t=0;r.n=a>b?a:b;return r; }
    if(streq_(name,"Math.min")){ long a=n>0?tonum(args[0]):0,b=n>1?tonum(args[1]):0; r.t=0;r.n=a<b?a:b;return r; }
    if(streq_(name,"parseInt")){ r.t=0; r.n=n>0?tonum(args[0]):0; return r; }
    if(streq_(name,"String")){ char b[JS_VSTRLEN]; if(n>0)v2s(args[0],b); else b[0]=0; r.t=1; scpy_(r.s,b); return r; }
    for(int f=0; f<g_nfuncs; f++){
        if(streq_(g_funcs[f].name,name)){
            for(int p=0;p<g_funcs[f].nparams && p<n;p++) setvar(g_funcs[f].params[p], args[p]);
            int save_tp=TP, save_ret=g_returning; jsval save_rv=g_retval;
            g_returning=0; g_retval=vundef(); TP=g_funcs[f].body_start;
            while(TP<g_funcs[f].body_end && !g_returning && T[TP].t!=JT_EOF) exec_stmt();
            jsval rv=g_retval;
            TP=save_tp; g_returning=save_ret; g_retval=save_rv;
            return rv;
        }
    }
    return r;
}

static void var_init(void){
    for(;;){
        if(T[TP].t!=JT_ID) return;
        char name[32]; scpy_(name,T[TP].s); TP++;
        jsval v=vundef();
        if(isop("=")){ TP++; v=eval_expr(); }
        setvar(name,v);
        if(isop(",")){ TP++; continue; }
        break;
    }
}

static void skip_function(void){
    TP++; if(T[TP].t==JT_ID)TP++;
    if(isop("(")){ int d=1;TP++;while(d&&T[TP].t!=JT_EOF){ if(isop("("))d++; else if(isop(")"))d--; TP++; } }
    if(isop("{")){ int depth=0; do{ if(isop("{"))depth++; else if(isop("}"))depth--; TP++; }while(depth>0&&T[TP].t!=JT_EOF); }
}

static void skip_stmt(void){
    if(T[TP].t==JT_EOF)return;
    if(isop(";")){TP++;return;}
    if(isop("{")){ int depth=0; do{ if(isop("{"))depth++; else if(isop("}"))depth--; TP++; }while(depth>0&&T[TP].t!=JT_EOF); return; }
    if(T[TP].t==JT_ID&&streq_(T[TP].s,"if")){ TP++; if(isop("(")){int d=1;TP++;while(d&&T[TP].t!=JT_EOF){if(isop("("))d++;else if(isop(")"))d--;TP++;}} skip_stmt(); if(T[TP].t==JT_ID&&streq_(T[TP].s,"else")){TP++;skip_stmt();} return; }
    if(T[TP].t==JT_ID&&(streq_(T[TP].s,"while")||streq_(T[TP].s,"for"))){ TP++; if(isop("(")){int d=1;TP++;while(d&&T[TP].t!=JT_EOF){if(isop("("))d++;else if(isop(")"))d--;TP++;}} skip_stmt(); return; }
    if(T[TP].t==JT_ID&&streq_(T[TP].s,"function")){ skip_function(); return; }
    while(T[TP].t!=JT_EOF && !isop(";") && !isop("}")) TP++;
    if(isop(";"))TP++;
}

static void if_stmt(void){
    TP++; jsval c=vundef();
    if(isop("(")){TP++; c=eval_expr(); if(isop(")"))TP++;}
    if(truthy(c)) exec_stmt(); else skip_stmt();
    if(T[TP].t==JT_ID && streq_(T[TP].s,"else")){ TP++; if(truthy(c)) skip_stmt(); else exec_stmt(); }
}

static void while_stmt(void){
    TP++; if(isop("("))TP++;
    int ce_lo=TP; jsval c=eval_expr();
    if(isop(")"))TP++;
    int body_lo=TP; int guard=0;
    while(truthy(c)&&!g_returning&&guard++<1000000){ TP=body_lo; exec_stmt(); TP=ce_lo; c=eval_expr(); }
    TP=body_lo; skip_stmt();
}

static void for_stmt(void){
    TP++; if(isop("("))TP++;
    if(!isop(";")){ if(T[TP].t==JT_ID&&(streq_(T[TP].s,"var")||streq_(T[TP].s,"let"))){ TP++; var_init(); } else eval_expr(); }
    if(isop(";"))TP++;
    int cond_lo=TP, has_cond=0; jsval c=vundef();
    if(!isop(";")){ c=eval_expr(); has_cond=1; }
    if(isop(";"))TP++;
    int incr_lo=TP; int t=TP; while(T[t].t!=JT_EOF && !(T[t].t==JT_OP&&streq_(T[t].s,")"))) t++; int incr_hi=t;
    TP=incr_hi; if(isop(")"))TP++;
    int body_lo=TP; int guard=0;
    for(;;){
        if(has_cond){ TP=cond_lo; c=eval_expr(); if(!truthy(c))break; }
        if(g_returning)break; if(guard++>1000000)break;
        TP=body_lo; exec_stmt();
        TP=incr_lo; if(incr_lo<incr_hi) eval_expr();
    }
    TP=body_lo; skip_stmt();
}

static void exec_stmt(void){
    if(T[TP].t==JT_EOF) return;
    if(T[TP].t==JT_OP){
        if(streq_(T[TP].s,";")){TP++;return;}
        if(streq_(T[TP].s,"{")){ TP++; while(T[TP].t!=JT_EOF && !isop("}") && !g_returning) exec_stmt(); if(isop("}"))TP++; return; }
    }
    if(T[TP].t==JT_ID){
        if(streq_(T[TP].s,"var")||streq_(T[TP].s,"let")){ TP++; var_init(); if(isop(";"))TP++; return; }
        if(streq_(T[TP].s,"if")){ if_stmt(); return; }
        if(streq_(T[TP].s,"while")){ while_stmt(); return; }
        if(streq_(T[TP].s,"for")){ for_stmt(); return; }
        if(streq_(T[TP].s,"function")){ skip_function(); return; }
        if(streq_(T[TP].s,"return")){ TP++; if(!isop(";")&&!isop("}")) g_retval=eval_expr(); g_returning=1; if(isop(";"))TP++; return; }
        if(streq_(T[TP].s,"break")){ TP++; if(isop(";"))TP++; return; }
    }
    eval_expr();
    if(isop(";"))TP++;
}

static void tokenize(const char* src){
    NT=0; TP=0; const char* p=src;
    while(*p && NT<JS_MAXTOK-1){
        if(*p==' '||*p=='\t'||*p=='\r'||*p=='\n'){p++;continue;}
        if(*p=='/'&&p[1]=='/'){ while(*p&&*p!='\n')p++; continue; }
        if(*p=='/'&&p[1]=='*'){ p+=2; while(*p&&!(*p=='*'&&p[1]=='/'))p++; if(*p)p+=2; continue; }
        if(*p>='0'&&*p<='9'){ long v=0; while(*p>='0'&&*p<='9'){v=v*10+(*p-'0');p++;} if(*p=='.'&&p[1]>='0'&&p[1]<='9'){p++;while(*p>='0'&&*p<='9')p++;} T[NT].t=JT_NUM;T[NT].n=v;T[NT].s[0]=0;NT++;continue; }
        if(*p=='"'||*p=='\''){ char q=*p++; int k=0; while(*p&&*p!=q){ if(k<JS_VSTRLEN-1)T[NT].s[k++]=*p; p++; } if(*p)p++; T[NT].s[k]=0;T[NT].t=JT_STR;T[NT].n=0;NT++;continue; }
        if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='_'||*p==36){ int k=0; while((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p==36){ if(k<31)T[NT].s[k++]=*p; p++; } T[NT].s[k]=0;T[NT].t=JT_ID;T[NT].n=0;NT++;continue; }
        if((p[0]=='='&&p[1]=='=')||(p[0]=='!'&&p[1]=='=')||(p[0]=='<'&&p[1]=='=')||(p[0]=='>'&&p[1]=='=')||(p[0]=='&'&&p[1]=='&')||(p[0]=='|'&&p[1]=='|')||(p[0]=='+'&&p[1]=='+')||(p[0]=='-'&&p[1]=='-')){ T[NT].t=JT_OP;T[NT].s[0]=p[0];T[NT].s[1]=p[1];T[NT].s[2]=0;T[NT].n=0;NT++;p+=2;continue; }
        T[NT].t=JT_OP;T[NT].s[0]=*p;T[NT].s[1]=0;T[NT].n=0;NT++;p++;
    }
    T[NT].t=JT_EOF;T[NT].s[0]=0;NT++;
}

static void scan_functions(void){
    g_nfuncs=0; int i=0;
    while(i<NT){
        if(T[i].t==JT_ID && streq_(T[i].s,"function") && T[i+1].t==JT_ID){
            jsfunc fn; scpy_(fn.name,T[i+1].s); fn.nparams=0;
            int j=i+2;
            if(T[j].t==JT_OP && streq_(T[j].s,"(")){ j++; while(T[j].t!=JT_EOF && !(T[j].t==JT_OP&&streq_(T[j].s,")"))){ if(T[j].t==JT_ID&&fn.nparams<4) scpy_(fn.params[fn.nparams++],T[j].s); j++; } if(T[j].t==JT_OP&&streq_(T[j].s,")"))j++; }
            if(T[j].t==JT_OP && streq_(T[j].s,"{")){
                int bs=j+1; int depth=0; int k=j;
                do{ if(T[k].t==JT_OP&&streq_(T[k].s,"{"))depth++; else if(T[k].t==JT_OP&&streq_(T[k].s,"}"))depth--; k++; }while(depth>0 && k<NT && T[k].t!=JT_EOF);
                int be=k-1;
                if(g_nfuncs<64){ fn.body_start=bs; fn.body_end=be; g_funcs[g_nfuncs++]=fn; }
                i=k; continue;
            }
        }
        i++;
    }
}

static int minijs_run(const char* src, char* out, int outcap){
    g_obuf=out; g_ocap=outcap; g_olen=0;
    tokenize(src);
    scan_functions();
    g_nvars=0; g_returning=0; g_retval=vundef(); g_fname[0]=0; g_lname[0]=0;
    TP=0; int guard=0;
    while(T[TP].t!=JT_EOF && !g_returning && guard++<200000) exec_stmt();
    if(g_olen<g_ocap) out[g_olen]=0; else out[g_ocap-1]=0;
    return g_olen;
}

#endif
