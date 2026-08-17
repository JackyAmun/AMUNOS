/* kbd.c - Keyboard Interrupt Handler
 * Adapted from flash-4th-os/kernel/keyboard.c
 *
 * PS/2 键盘 IRQ1 → 中断向量 0x21 (PIC 重映射后)
 * 读取端口 0x60 获取 scancode，转换为 ASCII 字符
 */

#include "common.h"

#define KBD_PORT 0x60

// ── 特殊键的 make code ──
#define SHIFT_L      0x2A
#define SHIFT_R      0x36
#define CTRL_L       0x1D
#define ALT_L        0x38
#define CAPS_LOCK    0x3A
#define ENTER        0x1C
#define BACKSPACE    0x0E
#define KEY_C        0x2E

// ── 全局状态变量 (与 kernel.c 共享) ──
volatile int is_shift    = 0;
volatile int caps_lock   = 0;
volatile int key_pressed = 0;
volatile char current_char = 0;
unsigned char last_scancode = 0;
static int ext_scancode = 0;       // 0xE0 前缀标记
static int is_ctrl = 0;            // Ctrl 按下 (用于 Ctrl+C)

// ── 键盘 LED 状态 (bit 0=Scroll, bit 1=Num, bit 2=Caps) ──
static unsigned char kbd_leds = 0;

/* 等待键盘输入缓冲为空 */
static void kbd_wait_out() {
    while (io_in8(0x64) & 0x02);  // bit 1 = input buffer full
}

/* 等待键盘输出缓冲有数据 */
static void kbd_wait_in() {
    while (!(io_in8(0x64) & 0x01));  // bit 0 = output buffer full
}

/* 更新键盘 LED 指示灯 */
static void update_leds() {
    kbd_wait_out();
    io_out8(KBD_PORT, 0xED);       // 设置 LED 命令

    kbd_wait_out();
    io_out8(KBD_PORT, kbd_leds);   // LED 状态字节
}

// ── 普通键映射表 (scancode → ASCII) ──
// 仅映射 0x01-0x39，其余位置填 0
unsigned char kmap[] = {
    0,    0,    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-',  '=',  '\b','\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o',  'p',  '[', ']', '\n',0,    'a', 's', 'd', 'f', 'g', 'h',
    'j',  'k',  'l', ';', '\'','`',  0,   '\\','z', 'x', 'c', 'v',
    'b',  'n',  'm', ',', '.', '/',  0,   '*', 0,   ' '
};

// ── 上档映射表 (shift 按下时的字符) ──
unsigned char kmap_s[] = {
    0,    0,    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '_',  '+',  '\b','\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O',  'P',  '{', '}', '\n',0,    'A', 'S', 'D', 'F', 'G', 'H',
    'J',  'K',  'L', ':', '"', '~',  0,   '|', 'Z', 'X', 'C', 'V',
    'B',  'N',  'M', '<', '>', '?',  0,   '*', 0,   ' '
};

/* 初始化键盘控制器 (8042)
 * 通过端口 0x61 复位键盘，确保中断正常产生 */
void keyboard_init() {
    unsigned char a;
    // 读 8042 端口 B，设置位 7 复位键盘，再恢复
    a = io_in8(0x61);
    io_out8(0x61, a | 0x80);   // 拉高位 7：禁用键盘
    io_out8(0x61, a);           // 恢复原值：重新启用键盘

    // 初始化 LED 为全灭
    kbd_leds = 0;
    update_leds();
}

/* 键盘中断处理函数 — 由 head.asm 的 asm_keyboard_handler 调用 */
void keyboard_handler() {
    unsigned char sc = io_in8(KBD_PORT);
    last_scancode = sc;

    // 应答键盘控制器 (避免键盘锁死)
    unsigned char kbd_ack = io_in8(0x61);
    io_out8(0x61, kbd_ack | 0x80);
    io_out8(0x61, kbd_ack);

    // ── 0xE0 前缀 (扩展键: 方向键等) ──
    if (sc == 0xE0) {
        ext_scancode = 1;
        return;
    }
    if (ext_scancode) {
        ext_scancode = 0;
        if (sc & 0x80) return;  // 忽略 break code (修复双击)
        switch (sc) {
        case 0x4B: key_pressed = 4;  return;  // ←
        case 0x4D: key_pressed = 5;  return;  // →
        case 0x48: key_pressed = 6;  return;  // ↑
        case 0x50: key_pressed = 7;  return;  // ↓
        case 0x53: key_pressed = 9;  return;  // DEL
        case 0x47: key_pressed = 10; return;  // HOME
        case 0x4F: key_pressed = 11; return;  // END
        case 0x49: key_pressed = 18; return;  // PgUp
        case 0x51: key_pressed = 19; return;  // PgDn
        default:   return;
        }
    }

    // 检查最高位：1=松开(break code)，0=按下(make code)
    if (sc & 0x80) {
        // ── 松开事件 ──
        sc &= 0x7F;  // 去掉 break 位
        if (sc == SHIFT_L || sc == SHIFT_R) is_shift = 0;
        if (sc == CTRL_L) is_ctrl = 0;
        return;
    }

    // ── 按下事件 ──
    switch (sc) {
    case SHIFT_L:
    case SHIFT_R:
        is_shift = 1;
        return;

    case CAPS_LOCK:
        caps_lock = !caps_lock;
        if (caps_lock) kbd_leds |= 0x04;   // 亮 Caps 灯
        else           kbd_leds &= ~0x04;  // 灭 Caps 灯
        update_leds();
        return;

    case CTRL_L:
        is_ctrl = 1;
        return;

    case ALT_L:
        return;  // 暂不处理

    case ENTER:
        key_pressed = 2;      // 回车
        return;

    case BACKSPACE:
        key_pressed = 3;      // 退格
        return;

    default:
        if (sc == 0x01) {     // ESC
            key_pressed = 8;
            return;
        }
        if (is_ctrl && sc == KEY_C) {
            force_kill = 1;   // 全局强制终止标志
            key_pressed = 12; // 通知 REPL 清行 (程序运行时由 syscall 层消费)
            return;
        }
        if (sc >= 0x3B && sc <= 0x3F) {  // F1-F5 功能键
            key_pressed = 13 + (sc - 0x3B);
            return;
        }
        if (sc < sizeof(kmap)) {
            // 根据 shift / caps_lock 状态选择映射表
            int shift = (is_shift ^ caps_lock) ? 1 : 0;
            // 对纯符号键，caps_lock 不影响，只用 is_shift
            if ((sc >= 0x02 && sc <= 0x0D) ||   // 数字/符号键
                 sc == 0x1A || sc == 0x1B ||      // [ ]
                 sc == 0x27 || sc == 0x28 ||      // ; '
                 sc == 0x29 || sc == 0x2B ||      // ` \
                 sc == 0x33 || sc == 0x34 ||       // , .
                 sc == 0x35) {                     // /
                shift = is_shift;
            }
            current_char = shift ? kmap_s[sc] : kmap[sc];
            if (current_char != 0) {
                key_pressed = 1;  // 普通字符
            }
        }
        return;
    }
}

/* 轮询键盘 — 中断失效时的后备方案
 * 读取 8042 状态端口 0x64，若输出缓冲区有数据则调用中断处理 */
void kbd_poll() {
    if (io_in8(0x64) & 0x01) {  // bit 0 = output buffer full
        keyboard_handler();
    }
}

/* 统一输入轮询: 键盘 + 串口 (远程控制台, v6.5)
 * 键盘事件优先; 无键盘事件时读 COM1 RX, 映射成同一套 key_pressed 编码。
 * 这样 shell / DIR 分页 / 编辑器 / 程序 stdin 都自动支持串口输入,
 * Ctrl+C (0x03) 在串口上也生效。
 * key_pressed: 1=字符 2=回车 3=退格 4-7=方向(←→↑↓) 8=ESC 9=DEL 10=HOME 11=END
 *              12=Ctrl+C 13-17=F1-F5 18=PgUp 19=PgDn
 * 串口 VT100: ESC[A-D=方向、ESC[H/F=Home/End、ESC[1~/7~/3~/4~/8~=Home/Del/End、ESC OP-T=F1-F5 */
void input_poll(void) {
    kbd_poll();
    if (key_pressed) return;              /* 键盘事件优先 */
    /* ── 串口 VT100 功能键/方向键 (ESC 序列) ──
     * sesc: 1=已收ESC 2=已收ESC[ 3=已收ESC O; sdigit: ESC[ <数字> 累积
     * esc_ticks: 裸 ESC 超时基准 — ESC 后 0.2s (20 tick) 无后续字节即视为裸 ESC,
     * 否则串口上单按 ESC (退出编辑器) 会永远等第二字节而挂住 */
    static int sesc = 0, sdigit = 0;
    static unsigned esc_ticks = 0;
    int c = serial_getc();
    if (c < 0) {
        if (sesc == 1 && (unsigned)(task_ticks() - esc_ticks) >= 20) {
            sesc = 0; key_pressed = 8;    /* 裸 ESC */
        }
        return;
    }

    if (sesc == 1) {                      /* 已收 ESC, 等 [ 或 O */
        if (c == '[') { sesc = 2; sdigit = 0; return; }
        if (c == 'O') { sesc = 3; return; }
        sesc = 0; key_pressed = 8; return;/* 裸 ESC 后跟普通字节 */
    }
    if (sesc == 2) {                      /* ESC[ <数字>~ 或 ESC[<字母> */
        if (c >= '0' && c <= '9') { sdigit = sdigit * 10 + (c - '0'); return; }
        if (c == '~') {
            sesc = 0;
            int f = (sdigit == 11) ? 13 : (sdigit == 12) ? 14 : (sdigit == 13) ? 15
                  : (sdigit == 14) ? 16 : (sdigit == 15) ? 17
                  : (sdigit == 5) ? 18 : (sdigit == 6) ? 19       /* PgUp PgDn */
                  : (sdigit == 1 || sdigit == 7) ? 10             /* Home (1~/7~) */
                  : (sdigit == 3) ? 9                             /* Del  (3~) */
                  : (sdigit == 4 || sdigit == 8) ? 11             /* End  (4~/8~) */
                  : 0;
            sdigit = 0;
            if (f) key_pressed = f;
            return;
        }
        sesc = 0; sdigit = 0;
        if (c == 'A') { key_pressed = 6;  return; }    /* ↑ */
        if (c == 'B') { key_pressed = 7;  return; }    /* ↓ */
        if (c == 'C') { key_pressed = 5;  return; }    /* → */
        if (c == 'D') { key_pressed = 4;  return; }    /* ← */
        if (c == 'H') { key_pressed = 10; return; }    /* Home */
        if (c == 'F') { key_pressed = 11; return; }    /* End */
        return;                                         /* 非法序列丢弃 */
    }
    if (sesc == 3) {                      /* ESC O <键>: F1-F5 或方向/Home/End */
        sesc = 0;
        if (c >= 'P' && c <= 'T') key_pressed = 13 + (c - 'P');
        else if (c == 'A') key_pressed = 6;   /* ↑ */
        else if (c == 'B') key_pressed = 7;   /* ↓ */
        else if (c == 'C') key_pressed = 5;   /* → */
        else if (c == 'D') key_pressed = 4;   /* ← */
        else if (c == 'H') key_pressed = 10;  /* Home */
        else if (c == 'F') key_pressed = 11;  /* End */
        return;
    }
    if (c == 0x1B) { sesc = 1; esc_ticks = task_ticks(); return; }

    if (c == '\r' || c == '\n')      key_pressed = 2;                 /* 回车 */
    else if (c == '\b' || c == 0x7F) key_pressed = 3;                 /* 退格 */
    else if (c == 3)                 { force_kill = 1; key_pressed = 12; } /* Ctrl+C */
    else                             { current_char = (char)c; key_pressed = 1; }
}
