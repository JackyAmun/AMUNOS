#!/bin/bash
# test_fat32.sh — FAT32 冒烟: 切 D:, TYPE hello.txt / big.bin (首行), ECHO 写, TYPE 回读, CD/CD..
cd /mnt/c/Users/XU/Desktop/OSdev
rm -f /tmp/ser.log /tmp/q.pid
python3 mkd32.py D32.img >/dev/null
nohup qemu-system-i386 -nographic -rtc base=localtime -hda A.img -hdb B.img -hdc C.img -hdd D32.img \
  -serial file:/tmp/ser.log -monitor unix:/tmp/mon.sock,server,nowait -pidfile /tmp/q.pid -display none \
  >/dev/null 2>&1 &
sleep 14
send() { for k in "$@"; do python3 qmon.py "sendkey $k" >/dev/null; sleep 0.25; done; }
send d shift-semicolon ret; sleep 2
send t y p e spc h e l l o dot t x t ret; sleep 3
send e c h o spc f a t 3 2 w r i t e o k spc shift-dot spc n e w dot t x t ret; sleep 4
send t y p e spc n e w dot t x t ret; sleep 3
send c d spc s u b ret; sleep 2
send c d spc shift-dot ret; sleep 2
send d i r spc shift-minus w ret; sleep 4
python3 qmon.py 'quit' >/dev/null 2>&1
sleep 2
kill $(cat /tmp/q.pid) 2>/dev/null
sed -n '/ready/,$p' /tmp/ser.log
