#!/usr/bin/env python3
"""qmon.py — QEMU monitor helper: run commands, pmemsave, quit."""
import socket, sys, time, os

SOCK = '/tmp/mon.sock'

def mon(cmds, wait=1.5):
    s = socket.socket(socket.AF_UNIX)
    s.connect(SOCK)
    s.settimeout(wait)
    def read_all():
        data = b''
        try:
            while True:
                b = s.recv(65536)
                if not b:
                    break
                data += b
        except socket.timeout:
            pass
        return data.decode('utf-8', 'replace')
    read_all()  # banner
    out = []
    for c in cmds:
        s.sendall((c + '\n').encode())
        time.sleep(0.4)
        out.append(read_all())
    s.close()
    return '\n'.join(out)

if __name__ == '__main__':
    cmds = sys.argv[1:]
    if not cmds:
        cmds = ['info status']
    print(mon(cmds))
