import subprocess, socket, time, os
MON=44555
def tryconn():
    for _ in range(40):
        try: return socket.create_connection(('127.0.0.1',MON),timeout=3)
        except OSError: time.sleep(0.3)
    raise SystemExit('no monitor')
def cmd(s, c):
    s.sendall(c.encode()+b'\n'); time.sleep(0.8)
    try: s.recv(8192)
    except socket.timeout: pass
q=subprocess.Popen(['qemu-system-i386','-hda','A.img','-display','none','-monitor',
    f'tcp:127.0.0.1:{MON},server,nowait'], stderr=subprocess.DEVNULL)
time.sleep(3.5)
s=tryconn(); s.settimeout(4)
cmd(s,'pmemsave 0x8000 0x10000 "/tmp/k.bin"')
s.close(); q.terminate()
if not os.path.exists('/tmp/k.bin'):
    print('NO FILE'); raise SystemExit
k=open('kernel.bin','rb').read(); L=open('/tmp/k.bin','rb').read()
print('kernel', len(k))
for name,base in [('0x8000-0xffff:',0x0000), ('0x10000-0x17fff:',0x8000)]:
    off=base
    m=sum(1 for a,b in zip(L[off:off+0x8000],k[:0x8000]) if a==b) if base==0 else \
      sum(1 for a,b in zip(L[off:off+0x8000],k[0x8000:len(k)]) if a==b)
    print(name, 'match', m, '/', min(0x8000, len(k)- (0x8000 if base else 0)))