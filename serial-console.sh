#!/bin/bash
# 连接 run-serial 的串口远程控制台 (nc 连 127.0.0.1:5555)
# raw 模式关掉本地回显, 避免与 guest 回显双重叠加; 退出后恢复终端。
stty raw -echo
nc 127.0.0.1 5555
stty sane
