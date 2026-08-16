/* task.c — AMUNOS 协作式任务调度
 *
 * 原理: PIT 定时器 (100Hz) → 中断 → 保存当前任务上下文到任务表
 *       → 选下一个 READY 任务 → 恢复其寄存器并切栈 → iret 恢复新任务
 *
 * 任务上下文布局 (与 head.asm 的 T_* 偏移一致):
 *   0=eax 4=ecx 8=edx 12=ebx 16=esp 20=ebp 24=esi 28=edi
 *
 * 注意: eip/cs/eflags 不存这里 — 它们作为 iret 帧保存在每个任务
 *       自己的栈上 (task_create 时搭好, 中断时由 CPU 压入), 切栈后
 *       由 iret 直接弹出。
 */

#include "common.h"

#define MAX_TASKS  8

#define TASK_READY   0
#define TASK_RUNNING 1
#define TASK_SLEEP   2
#define TASK_EXITED  3

struct task_ctx {
    unsigned eax, ecx, edx, ebx, esp, ebp, esi, edi;
};

struct task {
    int state;
    int sleep_left;
    struct task_ctx ctx;
    unsigned char *stack;   /* 堆分配栈基址 (task 0 无, 用内核栈 0x90000) */
    unsigned stack_size;
};

static struct task tasks[MAX_TASKS];
static int task_count = 0;
static struct task *running_task = 0;
static struct task *prog_task = 0;   /* 前台程序任务 (Ctrl+C 强杀目标) */
static unsigned system_ticks = 0;

/* ── 初始化: 内核主循环注册为任务 0 ── */
void task_init() {
    tasks[0].state = TASK_RUNNING;
    tasks[0].sleep_left = 0;
    running_task = &tasks[0];
    task_count = 1;
}

/* ── PIT 定时器初始化 (100Hz) ── */
void timer_init() {
    /* 通道 0, 模式 2 (rate generator), 16位 */
    io_out8(0x43, 0x34);
    int divisor = 1193182 / 100;   /* ~100Hz */
    io_out8(0x40, divisor & 0xFF);
    io_out8(0x40, (divisor >> 8) & 0xFF);
}

/* ── 创建任务 (fn 为入口, 永不返回; 栈从内核堆分配) ──
 * 返回任务指针 (满/无内存返回 0)。EXITED 的任务槽会被复用并释放旧栈。 */
struct task *task_create(void (*fn)(void), unsigned stack_size) {
    struct task *t = 0;

    /* 1. 优先复用已退出 (EXITED) 的任务槽, 释放其旧栈 */
    for (int i = 1; i < task_count; i++) {
        if (tasks[i].state == TASK_EXITED) {
            t = &tasks[i];
            if (t->stack) { mem_free(t->stack); t->stack = 0; }
            break;
        }
    }
    /* 2. 否则分配新槽 */
    if (!t) {
        if (task_count >= MAX_TASKS) return 0;
        t = &tasks[task_count++];
    }

    /* 3. 分配栈 */
    unsigned char *st = (unsigned char*)mem_alloc(stack_size);
    if (!st) return 0;

    /* 4. 在栈顶搭 iret 帧: [eip][cs][eflags] */
    unsigned *sp = (unsigned*)(st + stack_size);
    *--sp = 0x202;              /* eflags: IF 置位 */
    *--sp = 0x08;               /* cs: 内核代码段 */
    *--sp = (unsigned)fn;       /* eip: 任务入口 */

    t->ctx.eax = t->ctx.ecx = t->ctx.edx = t->ctx.ebx = 0;
    t->ctx.ebp = t->ctx.esi = t->ctx.edi = 0;
    t->ctx.esp = (unsigned)sp;  /* 切栈到此 → iret 弹出 [eip][cs][eflags] */
    t->state = TASK_READY;
    t->sleep_left = 0;
    t->stack = st;
    t->stack_size = stack_size;
    return t;
}

/* ── 结束当前任务 (标记 EXITED, 永不返回; 等定时器切回 shell) ── */
void task_exit_current(void) {
    if (running_task) running_task->state = TASK_EXITED;
    for (;;) __asm__ volatile("hlt");
}

/* ── 等待任务 t 退出 (EXITED); 期间 hlt 让出 CPU ── */
void task_wait(struct task *t) {
    while (t && t->state != TASK_EXITED)
        __asm__ volatile("hlt");
}

/* ── 记录前台程序任务 (Ctrl+C 强杀目标) ── */
void task_set_prog(struct task *t) { prog_task = t; }

/* ── 定时器中断处理 ──
 * 被 sched.asm 调用, 传栈帧指针 frame
 * frame 布局: [0..3]=seg, [16..44]=pushad, [48..56]=iret帧
 * 返回下一个任务 ctx* (或 0 不切换) */
void *timer_schedule(unsigned *frame) {
    system_ticks++;

    /* Ctrl+C 强杀前台 CPU 密集程序: 当前运行的是前台程序任务时, 把其返回
     * EIP (iret 帧 [12]) 重定向到 force_terminate (标记 EXITED 并让出 CPU)。
     * 若当前跑的是后台任务 (如 demo_clock), 等下次轮到前台任务再杀。 */
    if (force_kill && prog_active && prog_task && running_task == prog_task) {
        force_kill = 0;
        frame[12] = (unsigned)force_terminate;
    }

    /* 唤醒睡眠任务 */
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].state == TASK_SLEEP) {
            if (--tasks[i].sleep_left <= 0) tasks[i].state = TASK_READY;
        }
    }
    if (task_count < 2) return 0;

    /* 保存当前任务上下文 */
    /* frame: [0..3]=seg, [4..11]=pushad(edi,esi,ebp,esp,ebx,edx,ecx,eax), [12..14]=eip,cs,eflags(iret帧,留栈上) */
    if (running_task) {
        struct task_ctx *c = &running_task->ctx;
        c->edi = frame[4];
        c->esi = frame[5];
        c->ebp = frame[6];
        c->esp = frame[7];
        c->ebx = frame[8];
        c->edx = frame[9];
        c->ecx = frame[10];
        c->eax = frame[11];
        /* 仅 RUNNING → READY; SLEEP 保持 SLEEP, EXITED 保持 EXITED (不再调度) */
        if (running_task->state == TASK_RUNNING)
            running_task->state = TASK_READY;
    }

    /* 选下一个 READY 任务 (从当前之后轮转) */
    int start = 0;
    for (int i = 0; i < task_count; i++)
        if (&tasks[i] == running_task) { start = i; break; }

    for (int k = 1; k <= task_count; k++) {
        int idx = (start + k) % task_count;
        if (tasks[idx].state == TASK_READY) {
            tasks[idx].state = TASK_RUNNING;
            running_task = &tasks[idx];
            return &tasks[idx].ctx;
        }
    }
    return 0;   /* 无 READY 任务, 不切换 */
}

/* ── 任务睡眠 ── */
void task_sleep(int ticks) {
    if (!running_task || task_count < 2) return;
    running_task->state = TASK_SLEEP;
    running_task->sleep_left = ticks;
    /* 等待被 timer 唤醒 (hlt 让出 CPU, 中断会唤醒并切换) */
    while (running_task->state == TASK_SLEEP)
        __asm__ volatile("hlt");
}

/* ── 系统滴答 (供 TIME 等使用) ── */
unsigned task_ticks() { return system_ticks; }
