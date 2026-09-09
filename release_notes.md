# AMUNOS Classic v6.5.6 — 存储栈正式版

第一个 Release。从零编写的 x86 32 位保护模式操作系统：FAT12 引导自举、内核驻留 GUI、TinyCC 自编译闭环、原生中文（GB2312/UTF-8）。

## v6.5.6 存储栈（本版核心）
- **FDC 软盘运行时驱动**：非 DMA (ND) 模式 + IRQ6 字节中断，软盘引导后 A: 可运行时读盘
- **FatFs R0.16b**：FAT12/16/32 统一后端（fs.c 公开 API 不变），`mkd32.py` 生成 FAT32 数据盘
- **ATAPI 光驱**：PIO 驱动（IDENTIFY PACKET / READ(10) / READ CAPACITY）
- **ISO9660 只读**：PVD / 目录记录 / extent 连续读，`mkiso.py` 构建测试光盘，光盘走 DIR/TYPE
- **AHCI SATA**：PCI 0106 → BAR5 MMIO，READ DMA EXT poll CI 只读（q35 ICH9 实测通过）
- 统一块设备层 7 槽（IDE×4 / FD / CD / SATA），`DEVS` 命令、PCI 枚举、自动挂载

## 附件
- `A.img` — 1.44MB 软盘镜像（引导 + 内核 + TCC + 字库 + 样例），可直接 `qemu-system-i386 -fda A.img` 启动
- `kernel.bin` — 内核二进制（112,604 B）

## 验证
- `validate_storage2.py` 3 项 OVERALL PASS（IDE 多盘 / ATAPI+ISO9660 / AHCI）
- kernel.bin 112,604 B < 192KB 上限

**Full Changelog**: fcf6b3c...4c9b691
