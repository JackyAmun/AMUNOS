/* --------- message.c ---------- */
/* AMUNOS 移植裁剪:
 *   - 删 DOS 关键错误/ctrl-break 处理器 (harderr/hardretn/ctrlbrk/setcbrk)
 *   - 删 BIOS 时钟事件采集 (time/country/localtime) 与 time_string
 *   - 鼠标事件采集改为内核 SYS_MOUSE 轮询 (v6.7, PS/2 驱动)
 *   - dispatch_message 的 BORED int86 空转调用删除
 * 保留: EventQueue/MsgQueue、PostMessage/SendMessage、dispatch_message、
 *       handshake、定时器 (假 tick, 永不超时)、鼠标捕获状态 */

#include "dflat.h"
#include "syscall.h"

#define BLINKING_CLOCK 0	/* set to 1 to make the clock blink */
			/* blinking is off by default since 0.7c */

static int px = -1, py = -1;
static int pmx = -1, pmy = -1;
static int mx, my;
static int handshaking = 0;
static volatile BOOL CriticalError;
BOOL AllocTesting = FALSE;
jmp_buf AllocError;
BOOL AltDown = FALSE;

/* ---------- event queue ---------- */
static struct events    {
    MESSAGE event;
    int mx;
    int my;
} EventQueue[MAXMESSAGES];

/* ---------- message queue --------- */
static struct msgs {
    WINDOW wnd;
    MESSAGE msg;
    PARAM p1;
    PARAM p2;
} MsgQueue[MAXMESSAGES];

static int EventQueueOnCtr;
static int EventQueueOffCtr;
static int EventQueueCtr;

static int MsgQueueOnCtr;
static int MsgQueueOffCtr;
static int MsgQueueCtr;

static int lagdelay = FIRSTDELAY;

WINDOW CaptureMouse;
WINDOW CaptureKeyboard;
static BOOL NoChildCaptureMouse;
static BOOL NoChildCaptureKeyboard;

static int doubletimer = 0;
static int delaytimer  = 1;
static int clocktimer  = 2;
static char timerused[3] = {0, 0, 0}; /* 0 means off, 2 timed out */
			/* 1 counting */
static long unsigned int timerend[3] = {0, 0, 0};
static long unsigned int timerstart[3] = {0, 0, 0};
/* AMUNOS 无 BIOS 时钟数据区: 用恒定假 tick, 使定时器永不超时
 * (时钟/双击等靠 BIOS tick 的功能停用, 编辑器事件驱动不需要) */
static long unsigned int fake_ticks = 100;
volatile long unsigned int *biostimer = &fake_ticks;

static WINDOW Cwnd;

int timed_out(int timer)		/* was: countdown 0? */
{
    if ((timer > 2) || (timer < 0))
        return -1;			/* invalid -> always elapsed */
    if (timerused[timer] == 0)		/* not active at all? */
        return 0;
    if (timerused[timer] == 2)		/* timeout already known? */
        return 1;
    if (  (biostimer[0] < timerstart[timer])  /* wrapped? */
       || (biostimer[0] >= timerend[timer]) ) /* elapsed? */
    {
        timerused[timer] = 2;		/* countdown elapsed */
        return 1;
    }
    return 0;				/* still waiting */
}

int timer_running(int timer)		/* was: countdown > 0? */
{
    if ((timer > 2) || (timer < 0))
        return 0;			/* invalid -> never running */
    if (timerused[timer] == 1)		/* running? */
    {
        return (1 - timed_out(timer));	/* if not elapsed, running */
    }
    else return 0;			/* certainly not running */
}

int timer_disabled(int timer)		/* was: countdown -1? */
{
    if ((timer > 2) || (timer < 0))
        return 1;			/* invalid -> always disabled */
    return (timerused[timer] == 0);
}

void disable_timer(int timer)		/* was: countdown = -1 */
{
    if ((timer > 2) || (timer < 0))
        return;
    timerused[timer] = 0;
}

void set_timer(int timer, int secs)
{
    if ((timer > 2) || (timer < 0))
        return;
    timerstart[timer] = biostimer[0];
    timerend[timer] = timerstart[timer] + (secs*182UL/10) + 1;
    timerused[timer] = 1;	/* mark as running */
}

void set_timer_ticks(int timer, int ticks)
{
    if ((timer > 2) || (timer < 0))
        return;
    timerstart[timer] = biostimer[0];
    timerend[timer] = timerstart[timer] + ticks;
    timerused[timer] = 1;	/* mark as running */
}

static char ermsg[] = "Error accessing drive";

/* -------- test for critical errors ---------
 * AMUNOS 无 DOS critical error, CriticalError 恒 FALSE, 恒返回 0。
 * 保留以便 edit.c 的保存失败处理代码不经改动即可编译。 */
int TestCriticalError(void)
{
    int rtn = 0;
    if (CriticalError)    {
    	beep();
        rtn = 1;
        CriticalError = FALSE;
        if (TestErrorMessage(ermsg) == FALSE)
            rtn = 2;
    }
    return rtn;
}

/* ---------- 进程退出清理 (原版还原中断向量, AMUNOS 无 BIOS 钩子) ---------- */
static void StopMsg(void)
{
    ClearClipboard();
    ClearDialogBoxes();
    restorecursor();
    unhidecursor();
    hide_mousecursor();
}

/* ------------ initialize the message system --------- */
BOOL init_messages(void)
{
	AllocTesting = TRUE;
	if (setjmp(AllocError) != 0)	{
		StopMsg();
		return FALSE;
	}
    resetmouse();
	set_mousetravel(0, SCREENWIDTH-1, 0, SCREENHEIGHT-1);
	savecursor();
	hidecursor();
    px = py = -1;
    pmx = pmy = -1;
    mx = my = 0;
    CaptureMouse = CaptureKeyboard = NULL;
    NoChildCaptureMouse = FALSE;
    NoChildCaptureKeyboard = FALSE;
    MsgQueueOnCtr = MsgQueueOffCtr = MsgQueueCtr = 0;
    EventQueueOnCtr = EventQueueOffCtr = EventQueueCtr = 0;

    PostMessage(NULL,START,0,0);
    lagdelay = FIRSTDELAY;
	return TRUE;
}

/* ----- post an event and parameters to event queue ---- */
static void PostEvent(MESSAGE event, int p1, int p2)
{
    if (EventQueueCtr != MAXMESSAGES)    {
        EventQueue[EventQueueOnCtr].event = event;
        EventQueue[EventQueueOnCtr].mx = p1;
        EventQueue[EventQueueOnCtr].my = p2;
        if (++EventQueueOnCtr == MAXMESSAGES)
            EventQueueOnCtr = 0;
        EventQueueCtr++;
    }
}

/* ------ collect mouse, clock, and keyboard events -----
 * AMUNOS 版: 键盘 + shift 状态变化 (编辑块选) + 鼠标轮询。
 * 鼠标后端 = 内核 SYS_MOUSE (PS/2 IRQ12); 定时器假 tick (永不超时),
 * 双击/长按窗口与原版一致 (countdown 永不结束, 双击窗口内仍触发)。 */
static void collect_events(void)
{
    static int ShiftKeys = 0;
	int sk;

    /* --------- keyboard events ---------- */
    if ((sk = getshift()) != ShiftKeys)    {
        ShiftKeys = sk;
        /* ---- the shift status changed ---- */
        PostEvent(SHIFT_CHANGED, sk, 0);
    	if (sk & ALTKEY)
			AltDown = TRUE;
    }

    /* ----------- test for keystroke -------
       keyhit() = sys_keyhit() 非阻塞查询 (有键才 getkey, 无键继续轮询鼠标)。 */
    if (keyhit())    {
        int c = getkey(); /* ASCII, or (FKEY | scancode) */

		AltDown = FALSE;
        /* ------ post the keyboard event ------ */
        PostEvent(KEYBOARD, c, sk);
    }

    /* ------------ test for mouse events --------- */
    if (button_releases())    {
        /* ------- the button was released -------- */
		AltDown = FALSE;
        set_timer_ticks(doubletimer, DOUBLETICKS);
        PostEvent(BUTTON_RELEASED, mx, my);
        disable_timer(delaytimer);
    }
    get_mouseposition(&mx, &my);
    if (mx != px || my != py)  {
        px = mx;
        py = my;
        PostEvent(MOUSE_MOVED, mx, my);
    }
    if (rightbutton())	{
		AltDown = FALSE;
        PostEvent(RIGHT_BUTTON, mx, my);
	}
    if (leftbutton())    {
		AltDown = FALSE;
        if (mx == pmx && my == pmy)    {
            /* ---- same position as last left button ---- */
            if (timer_running(doubletimer))    {
                /* -- second click before double timeout -- */
                disable_timer(doubletimer);
                PostEvent(DOUBLE_CLICK, mx, my);
            }
            else if (!timer_running(delaytimer))    {
                /* ---- button held down a while ---- */
                set_timer_ticks(delaytimer, lagdelay);
                lagdelay = DELAYTICKS;
                /* ---- post a typematic-like button ---- */
                PostEvent(LEFT_BUTTON, mx, my);
            }
        }
        else    {
            /* --------- new button press ------- */
            disable_timer(doubletimer);
            set_timer_ticks(delaytimer, FIRSTDELAY);
            lagdelay = DELAYTICKS;
            PostEvent(LEFT_BUTTON, mx, my);
            pmx = mx;
            pmy = my;
        }
    }
    else
        lagdelay = FIRSTDELAY;

    /* 本轮若无事件, 短睡 1 tick (~55ms) 让出 CPU,
     * 防止空转占满 → MOUSE_MOVED 风暴 → 重绘闪烁。 */
    if (EventQueueCtr == 0)
        syscall1(SYS_SLEEP, 1);
}

/* ----- post a message and parameters to msg queue ---- */
void PostMessage(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    if (MsgQueueCtr != MAXMESSAGES)    {
        MsgQueue[MsgQueueOnCtr].wnd = wnd;
        MsgQueue[MsgQueueOnCtr].msg = msg;
        MsgQueue[MsgQueueOnCtr].p1 = p1;
        MsgQueue[MsgQueueOnCtr].p2 = p2;
        if (++MsgQueueOnCtr == MAXMESSAGES)
            MsgQueueOnCtr = 0;
        MsgQueueCtr++;
    }
}

/* --------- send a message to a window ----------- */
int SendMessage(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    int rtn = TRUE, x, y;

#ifdef INCLUDE_LOGGING
	LogMessages(wnd, msg, p1, p2);
#endif
    if (wnd != NULL)
        switch (msg)    {
            case PAINT:
            case BORDER:
                /* ------- don't send these messages unless the
                    window is visible -------- */
                if (isVisible(wnd))
		                rtn = (*wnd->wndproc)(wnd, msg, p1, p2);
                break;
            case RIGHT_BUTTON:
            case LEFT_BUTTON:
            case DOUBLE_CLICK:
            case BUTTON_RELEASED:
                /* --- don't send these messages unless the
                    window is visible or has captured the mouse -- */
                if (isVisible(wnd) || wnd == CaptureMouse)
		                rtn = (*wnd->wndproc)(wnd, msg, p1, p2);
                break;
            case KEYBOARD:
            case SHIFT_CHANGED:
                /* ------- don't send these messages unless the
                    window is visible or has captured the keyboard -- */
                if (!(isVisible(wnd) || wnd == CaptureKeyboard))
		                break;
            default:
                rtn = (*wnd->wndproc)(wnd, msg, p1, p2);
                break;
        }
    /* ----- window processor returned true or the message was sent
        to no window at all (NULL) ----- */
    if (rtn != FALSE)    {
        /* --------- process messages that a window sends to the
            system itself ---------- */
        switch (msg)    {
            case STOP:
				StopMsg();
                break;
            /* ------- clock messages --------- */
            case CAPTURE_CLOCK:
				if (Cwnd == NULL)
	                set_timer(clocktimer, 0);
				wnd->PrevClock = Cwnd;
                Cwnd = wnd;
                break;
            case RELEASE_CLOCK:
                Cwnd = wnd->PrevClock;
				if (Cwnd == NULL)
	                disable_timer(clocktimer);
                break;
            /* -------- keyboard messages ------- */
            case KEYBOARD_CURSOR:
                if (wnd == NULL)
                    cursor((int)p1, (int)p2);
                else if (wnd == inFocus)
                    cursor(GetClientLeft(wnd)+(int)p1,
                                GetClientTop(wnd)+(int)p2);
                break;
            case CAPTURE_KEYBOARD:
                if (p2)
                    ((WINDOW)p2)->PrevKeyboard=CaptureKeyboard;
                else
                    wnd->PrevKeyboard = CaptureKeyboard;
                CaptureKeyboard = wnd;
                NoChildCaptureKeyboard = (int)p1;
                break;
            case RELEASE_KEYBOARD:
				if (wnd != NULL)	{
					if (CaptureKeyboard == wnd || (int)p1)
	                	CaptureKeyboard = wnd->PrevKeyboard;
					else	{
						WINDOW twnd = CaptureKeyboard;
						while (twnd != NULL)	{
							if (twnd->PrevKeyboard == wnd)	{
								twnd->PrevKeyboard = wnd->PrevKeyboard;
								break;
							}
							twnd = twnd->PrevKeyboard;
						}
						if (twnd == NULL)
							CaptureKeyboard = NULL;
					}
                	wnd->PrevKeyboard = NULL;
				}
				else
					CaptureKeyboard = NULL;
                NoChildCaptureKeyboard = FALSE;
                break;
            case CURRENT_KEYBOARD_CURSOR:
                curr_cursor(&x, &y);
                *(int*)p1 = x;
                *(int*)p2 = y;
                break;
            case SAVE_CURSOR:
                savecursor();
                break;
            case RESTORE_CURSOR:
                restorecursor();
                break;
            case HIDE_CURSOR:
                normalcursor();
                hidecursor();
                break;
            case SHOW_CURSOR:
                if (p1)
                    set_cursor_type(0x0607);
                else
                    set_cursor_type(0x0106);

                unhidecursor();
                break;
            /* -------- mouse messages (AMUNOS stub 后端) -------- */
			case RESET_MOUSE:
				resetmouse();
				set_mousetravel(0, SCREENWIDTH-1, 0, SCREENHEIGHT-1);
				break;
            case MOUSE_INSTALLED:
                rtn = mouse_installed();
                break;
			case MOUSE_TRAVEL:	{
				RECT rc;
				if (!p1)	{
	        		rc.lf = rc.tp = 0;
	        		rc.rt = SCREENWIDTH-1;
	        		rc.bt = SCREENHEIGHT-1;
				}
				else
					rc = *(RECT *)p1;
				set_mousetravel(rc.lf, rc.rt, rc.tp, rc.bt);
				break;
			}
            case SHOW_MOUSE:
                show_mousecursor();
                break;
            case HIDE_MOUSE:
                hide_mousecursor();
                break;
            case MOUSE_CURSOR:
                set_mouseposition((int)p1, (int)p2);
                break;
            case CURRENT_MOUSE_CURSOR:
                get_mouseposition((int*)p1,(int*)p2);
                break;
            case WAITMOUSE:
                waitformouse();
                break;
            case TESTMOUSE:
                rtn = mousebuttons();
                break;
            case CAPTURE_MOUSE:
                if (p2)
                    ((WINDOW)p2)->PrevMouse = CaptureMouse;
                else
                    wnd->PrevMouse = CaptureMouse;
                CaptureMouse = wnd;
                NoChildCaptureMouse = (int)p1;
                break;
            case RELEASE_MOUSE:
				if (wnd != NULL)	{
					if (CaptureMouse == wnd || (int)p1)
	                	CaptureMouse = wnd->PrevMouse;
					else	{
						WINDOW twnd = CaptureMouse;
						while (twnd != NULL)	{
							if (twnd->PrevMouse == wnd)	{
								twnd->PrevMouse = wnd->PrevMouse;
								break;
							}
							twnd = twnd->PrevMouse;
						}
						if (twnd == NULL)
							CaptureMouse = NULL;
					}
                	wnd->PrevMouse = NULL;
				}
				else
					CaptureMouse = NULL;
                NoChildCaptureMouse = FALSE;
                break;
            default:
                break;
        }
    }
    return rtn;
}

static RECT VisibleRect(WINDOW wnd)
{
	RECT rc = WindowRect(wnd);
	if (!TestAttribute(wnd, NOCLIP))	{
		WINDOW pwnd = GetParent(wnd);
		RECT prc;
		prc = ClientRect(pwnd);
		while (pwnd != NULL)	{
			if (TestAttribute(pwnd, NOCLIP))
				break;
			rc = subRectangle(rc, prc);
			if (!ValidRect(rc))
				break;
			if ((pwnd = GetParent(pwnd)) != NULL)
				prc = ClientRect(pwnd);
		}
	}
	return rc;
}

/* ----- find window that mouse coordinates are in --- */
static WINDOW inWindow(WINDOW wnd, int x, int y)
{
	WINDOW Hit = NULL;
	while (wnd != NULL)	{
		if (isVisible(wnd))	{
			WINDOW wnd1;
			RECT rc = VisibleRect(wnd);
			if (InsideRect(x, y, rc))
				Hit = wnd;
			if ((wnd1 = inWindow(LastWindow(wnd), x, y)) != NULL)
				Hit = wnd1;
			if (Hit != NULL)
				break;
		}
		wnd = PrevWindow(wnd);
	}
	return Hit;
}

static WINDOW MouseWindow(int x, int y)
{
    /* ------ get the window in which a
                    mouse event occurred ------ */
    WINDOW Mwnd = inWindow(ApplicationWindow, x, y);
    /* ---- process mouse captures ----- */
    if (CaptureMouse != NULL)	{
        if (NoChildCaptureMouse ||
				Mwnd == NULL 	||
					!isAncestor(Mwnd, CaptureMouse))
            Mwnd = CaptureMouse;
	}
	return Mwnd;
}

void handshake(void)
{
	handshaking++;
	dispatch_message();
	--handshaking;
}

/* ---- dispatch messages to the message proc function ---- */
BOOL dispatch_message(void)
{
    WINDOW Mwnd, Kwnd;

    /* ---- 先清空已投递消息队列 (AMUNOS 修正) ----
     * 原版: 先 collect_events() 收键盘再清 MsgQueue。AMUNOS 无
     * 非阻塞按键查询 (keyhit 恒真, getkey 阻塞), 若仍先收键,
     * 排队的 INITIATE_DIALOG / ENDDIALOG 等投递消息会在等键时
     * 被饿死 → 文件对话框的 InitDlgBox 永不执行 (列表/路径全空)。
     * 清队列本身不阻塞, 移到前面先处理, 再收键事件, 行为等价。 */
    while (MsgQueueCtr > 0)  {
        struct msgs mq;

        mq = MsgQueue[MsgQueueOffCtr];
        if (++MsgQueueOffCtr == MAXMESSAGES)
            MsgQueueOffCtr = 0;
        --MsgQueueCtr;
        SendMessage(mq.wnd, mq.msg, mq.p1, mq.p2);
        if (mq.msg == ENDDIALOG)
            return FALSE;
        if (mq.msg == STOP) {
            PostMessage(NULL, STOP, 0, 0);
            return FALSE;
        }
    }

    /* -------- collect keyboard events ------- */
    collect_events();

    /* --------- dequeue and process events -------- */
    while (EventQueueCtr > 0)  {
        struct events ev;

		ev = EventQueue[EventQueueOffCtr];
        if (++EventQueueOffCtr == MAXMESSAGES)
            EventQueueOffCtr = 0;
        --EventQueueCtr;

        /* ------ get the window in which a
                        keyboard event occurred ------ */
        Kwnd = inFocus;

        /* ---- process keyboard captures ----- */
        if (CaptureKeyboard != NULL)
            if (Kwnd == NULL ||
                    NoChildCaptureKeyboard ||
						!isAncestor(Kwnd, CaptureKeyboard))
                Kwnd = CaptureKeyboard;

        /* -------- send mouse and keyboard messages to the
            window that should get them -------- */
        switch (ev.event)    {
            case SHIFT_CHANGED:
            case KEYBOARD:
				if (!handshaking)
	                SendMessage(Kwnd, ev.event, ev.mx, ev.my);
                break;
            case LEFT_BUTTON:
				if (!handshaking)	{
		        	Mwnd = MouseWindow(ev.mx, ev.my);
                	if (!CaptureMouse ||
                        	(!NoChildCaptureMouse &&
								isAncestor(Mwnd, CaptureMouse)))
                    	if (Mwnd != inFocus)
                        	SendMessage(Mwnd, SETFOCUS, TRUE, 0);
                	SendMessage(Mwnd, LEFT_BUTTON, ev.mx, ev.my);
				}
                break;
            case BUTTON_RELEASED:
            case DOUBLE_CLICK:
            case RIGHT_BUTTON:
				if (handshaking)
					break;
            case MOUSE_MOVED:
		        Mwnd = MouseWindow(ev.mx, ev.my);
                SendMessage(Mwnd, ev.event, ev.mx, ev.my);
                break;
            default:
                break;
        }
    }
    return TRUE;
}
