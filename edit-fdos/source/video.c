/* --------------------- video.c -------------------- */
/* AMUNOS 移植: BIOS int10h → 线性帧缓冲 0xB8000 直写。
 * 保留 DFLAT 的 CharInView 裁剪 / wputs 色码展开 / getvideo-storevideo 逻辑,
 * 只替换平台后端 (scroll_window 用 memmove 行滚动, 取消雪花等待/段远指针)。 */

#include "dflat.h"
#include <string.h>

BOOL ClipString;
static BOOL snowy = FALSE;

static unsigned video_address = 0xB8000;   /* 线性地址 (VA==PA, 平坦模式) */

/* -- read a rectangle of video memory into a save buffer -- */
void getvideo(RECT rc, void *bf)
{
    int ht = RectBottom(rc)-RectTop(rc)+1;
    int bytes_row = (RectRight(rc)-RectLeft(rc)+1) * 2;
    unsigned vadr = vad(RectLeft(rc), RectTop(rc));
    /* 捕获期间藏掉两个内核叠加 (_ 和 █), 否则 100Hz 定时器自愈把它们
     * 画进捕获区, 烤进背景缓冲 -> 残留 (v6.5.2) */
    hide_mousecursor();
    hidecursor();
    while (ht--)    {
		movefromscreen(bf, vadr, bytes_row);
        vadr += SCREENWIDTH*2;
        bf = (char *)bf + bytes_row;
    }
    unhidecursor();
    show_mousecursor();
}

/* -- write a rectangle of video memory from a save buffer -- */
void storevideo(RECT rc, void *bf)
{
    int ht = RectBottom(rc)-RectTop(rc)+1;
    int bytes_row = (RectRight(rc)-RectLeft(rc)+1) * 2;
    unsigned vadr = vad(RectLeft(rc), RectTop(rc));
    hide_mousecursor();
    hidecursor();
    while (ht--)    {
		movetoscreen(bf, vadr, bytes_row);
        vadr += SCREENWIDTH*2;
        bf = (char *)bf + bytes_row;
    }
    unhidecursor();
    show_mousecursor();
}

/* -------- read a character of video memory ------- */
unsigned int GetVideoChar(int x, int y)
{
    int c;
    hide_mousecursor();
    c = *(volatile unsigned short*)((char*)video_address + vad(x,y));
    show_mousecursor();
    return c;
}

/* -------- write a character of video memory ------- */
void PutVideoChar(int x, int y, int c)
{
    if (x < SCREENWIDTH && y < SCREENHEIGHT)    {
        hide_mousecursor();
        *(volatile unsigned short*)((char*)video_address + vad(x,y)) = (unsigned short)c;
        show_mousecursor();
    }
}

BOOL CharInView(WINDOW wnd, int x, int y)
{
	WINDOW nwnd = NextWindow(wnd);
	WINDOW pwnd;
	RECT rc;
    int x1 = GetLeft(wnd)+x;
    int y1 = GetTop(wnd)+y;

	if (!TestAttribute(wnd, VISIBLE))
		return FALSE;
    if (!TestAttribute(wnd, NOCLIP))    {
        WINDOW wnd1 = GetParent(wnd);
        while (wnd1 != NULL)    {
            /* --- clip character to parent's borders -- */
			if (!TestAttribute(wnd1, VISIBLE))
				return FALSE;
			if (!InsideRect(x1, y1, ClientRect(wnd1)))
                return FALSE;
            wnd1 = GetParent(wnd1);
        }
    }
	while (nwnd != NULL)	{
		if (!isHidden(nwnd) /* && !isAncestor(wnd, nwnd) */ )	{
			rc = WindowRect(nwnd);
    		if (TestAttribute(nwnd, SHADOW))    {
        		RectBottom(rc)++;
        		RectRight(rc)++;
    		}
			if (!TestAttribute(nwnd, NOCLIP))	{
				pwnd = nwnd;
				while (GetParent(pwnd))	{
					pwnd = GetParent(pwnd);
					rc = subRectangle(rc, ClientRect(pwnd));
				}
			}
			if (InsideRect(x1,y1,rc))
				return FALSE;
		}
		nwnd = NextWindow(nwnd);
	}
    return (x1 < SCREENWIDTH && y1 < SCREENHEIGHT);
}

/* -------- write a character to a window ------- */
void wputch(WINDOW wnd, int c, int x, int y)
{
	if (CharInView(wnd, x, y))	{
		int ch = (c & 255) | (clr(foreground, background) << 8);
		int xc = GetLeft(wnd)+x;
		int yc = GetTop(wnd)+y;
        hide_mousecursor();
        *(volatile unsigned short*)((char*)video_address + vad(xc, yc)) = (unsigned short)ch;
        show_mousecursor();
	}
}

/* ------- write a string to a window ---------- */
void wputs(WINDOW wnd, void *s, int x, int y)
{
    int x1=GetLeft(wnd)+x;
    int x2=x1;
    int y1=GetTop(wnd)+y;

    if (x1 < SCREENWIDTH && y1 < SCREENHEIGHT && isVisible(wnd))
        {
        unsigned short ln[200];
        unsigned short *cp1=ln;
        int fg=foreground;
        int bg=background;
        int len;
        int off=0;
        unsigned char *str=s;

        while (*str)
            {
            if (*str == CHANGECOLOR)
                {
                int fgcode, bgcode;	/* new 0.7c: sanity checks */
                str++;
                fgcode = (*str++);
                bgcode = (*str++);
                if ((fgcode & 0x80) && (bgcode & 0x80) &&
                    !(fgcode & 0x70) && !(bgcode & 0x70)) {
                    foreground = fgcode & 0x7f;
                    background = bgcode & 0x7f;
                    continue;
                } else {	/* this also makes CHANGECOLOR almost normal */
                    str--;	/* and useable as character in your texts... */
                    str--;	/* treat as non-escape sequence */
                    str--;
                }
                }

            if (*str == RESETCOLOR)
                {
                foreground = fg & 0x7f;
                background = bg & 0x7f;
                str++;
                continue;
                }

#ifdef TAB_TOGGLING	/* made consistent with editor.c - 0.7c */
            if (*str == ('\t' | 0x80) || *str == ('\f' | 0x80))
                *cp1 = ' ' | (clr(foreground, background) << 8);
            else
#endif
                *cp1 = (*str & 255) | (clr(foreground, background) << 8);

            if (ClipString)
                if (!CharInView(wnd, x, y))
                    *cp1 = *(volatile unsigned short*)((char*)video_address + vad(x2,y1));

            cp1++;
            str++;
            x++;
            x2++;
            }

        foreground = fg;
        background = bg;
        len = (int)(cp1-ln);
        if (x1+len > SCREENWIDTH)
            len = SCREENWIDTH-x1;

        if (!ClipString && !TestAttribute(wnd, NOCLIP))
            {
            /* -- clip the line to within ancestor windows -- */
            RECT rc = WindowRect(wnd);
            WINDOW nwnd = GetParent(wnd);

            while (len > 0 && nwnd != NULL)
                {
                if (!isVisible(nwnd))
                    {
                    len = 0;
                    break;
                    }

                rc = subRectangle(rc, ClientRect(nwnd));
                nwnd = GetParent(nwnd);
                }

            while (len > 0 && !InsideRect(x1+off,y1,rc))
                {
                off++;
                --len;
                }

            if (len > 0)
                {
                x2 = x1+len-1;
                while (len && !InsideRect(x2,y1,rc))
                    {
                    --x2;
                    --len;
                    }

                }

            }

        if (len > 0)
            {
            hide_mousecursor();
            movetoscreen(ln+off, vad(x1+off,y1), len*2);
            show_mousecursor();
            }

        }
}

/* --------- get the current video mode -------- */
void get_videomode(void)
{
    /* AMUNOS: 固定 80x25 彩色文本, 线性地址 0xB8000 */
    video_address = 0xB8000;
    snowy = FALSE;
}

/* --------- scroll the window. d: 1 = up, 0 = dn ---------- */
void scroll_window(WINDOW wnd, RECT rc, int d)
{
	if (RectTop(rc) != RectBottom(rc))	{
        int left = RectLeft(rc);
        int top  = RectTop(rc);
        int w    = RectRight(rc)-left+1;
        int h    = RectBottom(rc)-top+1;
        int attr = clr(WndForeground(wnd), WndBackground(wnd));
        char *base = (char*)video_address;
        int y;

        hide_mousecursor();
        if (d)  {                   /* 向上滚: 内容上移, 末行清空 */
            for (y = top; y < top+h-1; y++)
                memmove(base + vad(left,y), base + vad(left,y+1), w*2);
            y = top+h-1;
            for (int x = 0; x < w; x++) {
                *(volatile unsigned short*)(base + vad(left+x,y)) =
                    (unsigned short)(' ' | (attr << 8));
            }
        } else {                    /* 向下滚: 内容下移, 首行清空 */
            for (y = top+h-1; y > top; y--)
                memmove(base + vad(left,y), base + vad(left,y-1), w*2);
            y = top;
            for (int x = 0; x < w; x++) {
                *(volatile unsigned short*)(base + vad(left+x,y)) =
                    (unsigned short)(' ' | (attr << 8));
            }
        }
        show_mousecursor();
	}
}

void movetoscreen(void *bf, int offset, int len)
{
    memcpy((char*)video_address + offset, bf, len);
}

void movefromscreen(void *bf, int offset, int len)
{
    memcpy(bf, (char*)video_address + offset, len);
}
