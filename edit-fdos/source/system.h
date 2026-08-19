/* --------------- system.h -------------- */
/* AMUNOS 移植版: 16 位 DOS 平台层 → 32 位平坦模式 + sys_getkey/sys_getmods
 * 保留 DFLAT 需要的类型/宏/原型, 删除 BIOS 中断/寄存器/远指针/_dos_*。 */
#ifndef SYSTEM_H
#define SYSTEM_H

#define swap(a,b){int x=a;a=b;b=x;}
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif
/* ----- interrupt vectors (保留常量, 不再挂钩) ----- */
#define TIMER  8
#define VIDEO  0x10
#define KEYBRD 0x16
#define DOS    0x21
#define CRIT   0x24
#define MOUSE  0x33
#define KEYBOARDVECT 9
/* ------- platform-dependent values ------ */
#define KEYBOARDPORT 0x60
#define FREQUENCY 100
#define COUNT (1193280L / FREQUENCY)
#define ZEROFLAG 0x40
#define MAXSAVES 50
/* AMUNOS: 固定 80x25 彩色文本 */
#define SCREENWIDTH  80
#define SCREENHEIGHT 25
/* AMUNOS: DOS 路径常量桩 (原 dos.h) */
#define MAXPATH 80
#define clearBIOSbuffer()
#define waitforkeyboard()

/* ----- keyboard BIOS (0x16) functions (编号保留) -------- */
#define READKB 0
#define KBSTAT 1
/* ------- video BIOS (0x10) functions (编号保留) --------- */
#define SETCURSORTYPE 1
#define SETCURSOR     2
#define READCURSOR    3
#define READATTRCHAR  8
#define WRITEATTRCHAR 9
#define HIDECURSOR 0x20
/* ------- the interrupt function registers (无平台使用) -------- */
typedef struct {
    int bp,di,si,ds,es,dx,cx,bx,ax,ip,cs,fl;
} IREGS;

/* ---------- far/near 关键字在平坦模式无意义 ---------- */
#ifndef near
#define near
#endif
#ifndef far
#define far
#endif
#ifndef FAR
#define FAR
#endif

/* ---------- 平坦内存读写兼容宏 (p=线性地址, o=字节偏移) ---------- */
#define poke(a,b,c)  (*((char*)  (a) + (b)) = (char)(c))
#define pokeb(a,b,c) (*((char*)  (a) + (b)) = (char)(c))
#define peek(a,b)    (*((char*)  (a) + (b)))
#define peekb(a,b)   (*((unsigned char*)(a) + (b)))
#define MK_FP(s,o)   ((void*)(unsigned)(o))
#define FP_OFF(p)    ((unsigned)(p))
#define FP_SEG(p)    (0)
#define getvect(v)   (NULL)
#define setvect(v,f) (void)0
#define getdisk()    (0)
#define setdisk(d)   (void)0

/* ---------- keyboard prototypes -------- */
int AltConvert(int);
int Xbioskey(int); /* enhanced for 102 key keyboard support */
int getkey(void);
int getshift(void);
BOOL keyhit(void);
void beep(void);
/* ---------- cursor prototypes -------- */
void curr_cursor(int *x, int *y);
void cursor(int x, int y);
void hidecursor(void);
void unhidecursor(void);
void savecursor(void);
void restorecursor(void);
void normalcursor(void);
void set_cursor_type(unsigned t);
void underline_cursor(void);
void block_cursor(void);
void videomode(void);
void SwapCursorStack(void);
/* --------- screen prototpyes -------- */
void clearscreen(void);
/* ---------- mouse prototypes ---------- */
BOOL mouse_installed(void);
int mousebuttons(void);
void get_mouseposition(int *x, int *y);
void set_mouseposition(int x, int y);
void show_mousecursor(void);
void hide_mousecursor(void);
int button_releases(void);
void resetmouse(void);
void set_mousetravel(int, int, int, int);
#define leftbutton()     (mousebuttons()&1)
#define rightbutton()     (mousebuttons()&2)
#define waitformouse()     while(mousebuttons());

/* ------------ timer macros -------------- */
int timed_out(int timer);
void set_timer(int timer, int secs);
void set_timer_ticks(int timer, int ticks);
void disable_timer(int timer);
int timer_running(int timer);
int timer_disabled(int timer);

/* ----------- video adaptor prototypes ----------- */
BOOL isEGA(void);
BOOL isVGA(void);
void Set25(void);
void Set43(void);
void Set50(void);

/* ============= Color Macros ============ */
#define BLACK         0
#define BLUE          1
#define GREEN         2
#define CYAN          3
#define RED           4
#define MAGENTA       5
#define BROWN         6
#define LIGHTGRAY     7
#define DARKGRAY      8
#define LIGHTBLUE     9
#define LIGHTGREEN   10
#define LIGHTCYAN    11
#define LIGHTRED     12
#define LIGHTMAGENTA 13
#define YELLOW       14
#define WHITE        15

typedef enum messages {
	#undef DFlatMsg
	#define DFlatMsg(m) m,
	#include "dflatmsg.h"
	MESSAGECOUNT
} MESSAGE;

typedef enum window_class    {
	#define ClassDef(c,b,p,a) c,
	#include "classes.h"
	CLASSCOUNT
} CLASS;

#endif
