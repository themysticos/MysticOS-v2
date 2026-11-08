import urllib.request, re
d = urllib.request.urlopen('http://wiby.me/?q=linux', timeout=10).read().decode('utf-8','ignore')
# эмуляция: идём по HTML, для <a> печатаем, что видит парсер до текста
MAXC=100
line=0; col=0; in_a=False; a_line=0; a_col=0
out=[]
i=0; n=len(d)
while i<n:
    if d[i]=='<':
        j=d.find('>',i)
        if j<0: break
        tag=d[i+1:j]
        closing=tag.startswith('/')
        name=re.split(r'[\s/>]', tag.lstrip('/'))[0].lower()
        if name=='a' and not closing:
            m=re.search(r'href=["\']([^"\']*)', tag)
            a_line=line; a_col=col; in_a=True
            href=m.group(1) if m else ''
            out.append(('OPEN', line, col, href[:50]))
        elif name=='a' and closing:
            out.append(('CLOSE', line, col, 'len=%d'%(col-a_col)))
            in_a=False
        elif name in ('br','p','div','li','tr','blockquote','h1','h2','h3'):
            line+=1; col=0
        i=j+1; continue
    if d[i]=='\n': line+=1; col=0; i+=1; continue
    col+=1; i+=1
for x in out[:12]: print(x)
