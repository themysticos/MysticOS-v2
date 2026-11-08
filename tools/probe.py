import urllib.request, re, sys
url = sys.argv[1] if len(sys.argv)>1 else 'http://wiby.me/?q=linux'
try:
    d = urllib.request.urlopen(url, timeout=10).read().decode('utf-8','ignore')
except Exception as e:
    print('ERR', e); sys.exit(1)
print('LEN', len(d))
a = re.findall(r'<a\s[^>]*>', d, re.I)
print('A count', len(a))
for x in a[:15]:
    print(x[:140])
