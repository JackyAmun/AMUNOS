/* command.c — AMUNOS v6.5 */

#include "common.h"

static char* fparse(char* a, char* f){f[0]=0;while(*a==' ')a++;if(a[0]=='-'&&a[1]){f[0]=a[1];f[1]=0;a+=2;}while(*a==' ')a++;return a;}
static void upper(char* s){while(*s){if(*s>='a'&&*s<='z')*s-=32;s++;}}

static void p_add(char* n){
    int l=strlen(cwd_path),a=strlen(n);if(l+a+2>126)return;
    if(l&&cwd_path[l-1]!='/')cwd_path[l++]='/';
    for(int i=0;i<a;i++)cwd_path[l++]=n[i];cwd_path[l]=0;
}
static void p_pop(){
    int l=strlen(cwd_path);if(!l)return;
    while(l>0&&cwd_path[l-1]!='/')l--;
    if(l>0)l--;cwd_path[l]=0;
}

/* ── 等待按键: 返回 1=继续, 0=中止 (Ctrl+C) ── */
static int wait_key_or_abort(void){
    int kp = 0;
    while(!kp){ input_poll(); kp = key_pressed; }
    if(kp == 12){              /* Ctrl+C 中止分页 */
        key_pressed = 0; force_kill = 0;
        return 0;
    }
    key_pressed = 0;
    return 1;
}

/* ── DIR ── */
void cmd_dir(char* arg){
    char f[2];arg=fparse(arg,f);
    int wide=(f[0]=='w'||f[0]=='W');
    int page=(f[0]=='p'||f[0]=='P');
    int max=fs_dir_secs(cwd_cluster);
    FAT12Entry b[16];int cnt=0,line=0;
    put_str("\n ");put_char(drive_letter(),0x0E);
    put_str(":/");if(*cwd_path)put_str(cwd_path);
    put_str("\n\n");
    for(int s=0;s<max;s++){
        int lba=fs_dir_lba(cwd_cluster,s);
        if(lba<0)break;
        read_sector_asm(lba,b,current_drive_idx);
        for(int i=0;i<16;i++){
            if(b[i].name[0]==0)goto ed;
            if((unsigned char)b[i].name[0]==0xE5)continue;
            if(b[i].attr==0x0F)continue;
            if(b[i].name[0]=='.'&&(b[i].attr&0x10)){put_str(b[i].name[1]==' '?".  <DIR>\n":".. <DIR>\n");cnt++;line++;continue;}
            for(int j=0;j<8;j++)put_char(b[i].name[j]!=' '?b[i].name[j]:' ',0x07);
            put_char('.',0x07);
            for(int j=0;j<3;j++)put_char(b[i].ext[j]!=' '?b[i].ext[j]:' ',0x07);
            if(!wide){put_str("  ");if(b[i].attr&0x10)put_str("<DIR>         ");else{put_str("      ");put_num(b[i].size);put_str(" B");}}
            put_char('\n',0x07);cnt++;line++;

            if(page && line >= 21){
                put_str("-- Press any key to continue (Ctrl+C to stop) --\n");
                if(!wait_key_or_abort()) goto ed;
                line = 0;
            }
        }
    }
ed: put_str("\n ");put_num(cnt);put_str(" file(s)\n\n");
}

/* ── CD — 多级目录导航 ── */
void cmd_cd(char* arg){
    upper(arg);
    if(!*arg||(*arg=='/'&&!arg[1])){cwd_cluster=0;cwd_path[0]=0;return;}

    char path[64]; strcpy(path, arg);
    char *p = path;
    int cur = cwd_cluster;

    // 绝对路径 /a/b → 从根开始
    if (p[0]=='/') { cur=0; cwd_path[0]=0; p++; }

    while (p && *p) {
        char *sep = p;
        while (*sep && *sep!='/') sep++;
        char save = *sep; if (*sep) *sep = 0;

        if (*p) {
            if ((p[0]=='.'&&p[1]=='.'&&!p[2])||(p[0]=='<'&&!p[1])) {
                // 上一级
                if (cur != 0) {
                    unsigned char d[512];
                    read_sector_asm(fs_cluster_lba(cur),d,current_drive_idx);
                    cur = ((FAT12Entry*)d)[1].start_cluster;
                    p_pop();
                }
            } else if (p[0]=='.'&&!p[1]) {
                // 当前目录，跳过
            } else {
                FAT12Entry e;
                if (fs_find_entry_in_dir(cur, p, &e) < 0 || !(e.attr&0x10)) {
                    put_str("Not found.\n"); return;
                }
                cur = e.start_cluster;
                p_add(p);
            }
        }
        if (save) { *sep = save; p = sep + 1; } else break;
    }
    cwd_cluster = cur;
}

/* ── TYPE ── */
void cmd_type(char* arg){
    upper(arg);
    if(!*arg){put_str("Usage: TYPE file\n");return;}
    drive_ctx_t octx; int od=fs_drive_open(arg,&octx);
    int dc=fs_resolve_path(arg);if(dc<0){if(od>=0)fs_drive_restore(octx);put_str("Not found.\n");return;}
    FAT12Entry e;if(fs_find_entry_in_dir(dc,arg,&e)<0){if(od>=0)fs_drive_restore(octx);put_str("Not found.\n");return;}
    char *b=(char*)mem_alloc((unsigned)e.size+1);      /* fs_read_file 写 NUL → size+1 */
    if(!b){if(od>=0)fs_drive_restore(octx);put_str("No memory.\n");return;}
    fs_read_file(&e,b);
    if(od>=0)fs_drive_restore(octx);
    put_str(b);put_char('\n',0x07);
    mem_free(b);
}

/* ── ECHO ── */
void cmd_echo(char* arg){
    if(!*arg){put_str("ECHO is on.\n");return;}
    char* g=arg;while(*g&&*g!='>')g++;
    if(*g=='>'){*g=0;g++;while(*g==' ')g++;upper(g);char*t=arg;int tl=strlen(t);while(tl>0&&t[tl-1]==' ')t[--tl]=0;
        if(!*g||!tl){put_str("Usage: ECHO text > file\n");return;}
        drive_ctx_t octx; int od=fs_drive_open(g,&octx);
        int dc=fs_resolve_path(g);if(dc<0){if(od>=0)fs_drive_restore(octx);put_str("Not found.\n");return;}
        if(fs_create_file_in_dir(dc,g,t,tl)==0)put_str("Written.\n");
        if(od>=0)fs_drive_restore(octx);}
    else{put_str(arg);put_char('\n',0x07);}
}

/* ── SER / LPT — 串口/并口输出 (v6.5) ──
 * 输出走 serial_puts/lpt_puts (会把 \n 翻成 CRLF), 与按键回显 (LF-only) 区分 */
void cmd_ser(char* a1){ if(!*a1){put_str("Usage: SER text\n");return;} serial_puts(a1); serial_puts("\n"); }
void cmd_lpt(char* a1){ if(!*a1){put_str("Usage: LPT text\n");return;} lpt_puts(a1); lpt_puts("\n"); }

/* ── REN ── */
void cmd_ren(char* arg){
    upper(arg);char* sp=arg;while(*sp&&*sp!=' ')sp++;
    if(!*sp||!*(sp+1)){put_str("Usage: REN old new\n");return;}
    *sp++=0;while(*sp==' ')sp++;
    drive_ctx_t octx; int od=fs_drive_open(arg,&octx);
    int dc=fs_resolve_path(arg);if(dc<0){if(od>=0)fs_drive_restore(octx);put_str("Not found.\n");return;}
    char oldnm[12],newnm[12];
    to_fat12_name(arg,oldnm);to_fat12_name(sp,newnm);   /* arg 已被剥成裸名 */
    if(is_cmds_file(oldnm)||is_cmds_file(newnm)){if(od>=0)fs_drive_restore(octx);put_str("CMDS.BIN is protected.\n");return;}
    FAT12Entry e;int idx=fs_find_entry_in_dir(dc,arg,&e);if(idx<0){if(od>=0)fs_drive_restore(octx);put_str("Not found.\n");return;}
    int lba=fs_dir_lba(dc, idx/16);   /* v6.5.1: 统一目录寻址 (FAT16 每簇多扇也正确) */
    FAT12Entry b[16];read_sector_asm(lba,b,current_drive_idx);
    to_fat12_name(sp,b[idx%16].name);write_sector_asm(lba,b,current_drive_idx);
    if(od>=0)fs_drive_restore(octx);
    put_str("Renamed.\n");
}

/* ── COPY ── */
void cmd_copy(char* arg){
    upper(arg);char* sp=arg;while(*sp&&*sp!=' ')sp++;
    if(!*sp||!*(sp+1)){put_str("Usage: COPY src dst\n");return;}
    *sp++=0;while(*sp==' ')sp++;

    /* 源: 可带盘符/绝对路径; 堆缓冲读全 (fs_read_file 在 size 处写 NUL) */
    drive_ctx_t sctx; int sd=fs_drive_open(arg,&sctx);
    int dc_src=fs_resolve_path(arg);if(dc_src<0){if(sd>=0)fs_drive_restore(sctx);put_str("Src not found.\n");return;}
    FAT12Entry e;if(fs_find_entry_in_dir(dc_src,arg,&e)<0){if(sd>=0)fs_drive_restore(sctx);put_str("Src not found.\n");return;}
    if(e.attr&0x10){if(sd>=0)fs_drive_restore(sctx);put_str("Cannot copy dir.\n");return;}
    char *b=(char*)mem_alloc((unsigned)e.size+1);if(!b){if(sd>=0)fs_drive_restore(sctx);put_str("No memory.\n");return;}
    fs_read_file(&e,b);int sz=e.size;
    if(sd>=0)fs_drive_restore(sctx);

    /* 目标: 独立盘符; 裸盘 "B:" → 去前缀后空串 → 该盘根 + 源文件名 */
    drive_ctx_t dctx; int dd=fs_drive_open(sp,&dctx);
    int dc_dst=cwd_cluster;char *dstname=sp;
    if(!*sp){dc_dst=0;dstname=arg;}
    else{char *sep=sp;int has=0;while(*sep){if(*sep=='/'){has=1;break;}sep++;}
      if(has){dc_dst=fs_resolve_path(sp);if(dc_dst<0){mem_free(b);if(dd>=0)fs_drive_restore(dctx);put_str("Dst not found.\n");return;}}}
    int r=fs_create_file_in_dir(dc_dst,dstname,b,sz);
    if(dd>=0)fs_drive_restore(dctx);
    mem_free(b);
    if(r==0)put_str("Copied.\n");
}

/* ── MOV ── */
void cmd_mov(char* arg){
    upper(arg);char* sp=arg;while(*sp&&*sp!=' ')sp++;
    if(!*sp||!*(sp+1)){put_str("Usage: MOV src dst\n");return;}
    *sp++=0;while(*sp==' ')sp++;

    drive_ctx_t sctx; int sd=fs_drive_open(arg,&sctx);
    int dc_src=fs_resolve_path(arg);if(dc_src<0){if(sd>=0)fs_drive_restore(sctx);put_str("Src not found.\n");return;}
    FAT12Entry e;if(fs_find_entry_in_dir(dc_src,arg,&e)<0){if(sd>=0)fs_drive_restore(sctx);put_str("Src not found.\n");return;}
    if(e.attr&0x10){if(sd>=0)fs_drive_restore(sctx);put_str("Cannot move dir.\n");return;}
    char *b=(char*)mem_alloc((unsigned)e.size+1);if(!b){if(sd>=0)fs_drive_restore(sctx);put_str("No memory.\n");return;}
    fs_read_file(&e,b);int sz=e.size;
    if(sd>=0)fs_drive_restore(sctx);

    drive_ctx_t dctx; int dd=fs_drive_open(sp,&dctx);
    int dc_dst=cwd_cluster;char *dstname=sp;
    if(!*sp){dc_dst=0;dstname=arg;}
    else{char *sep=sp;int has=0;while(*sep){if(*sep=='/'){has=1;break;}sep++;}
      if(has){dc_dst=fs_resolve_path(sp);if(dc_dst<0){mem_free(b);if(dd>=0)fs_drive_restore(dctx);put_str("Dst not found.\n");return;}}}
    int r=fs_create_file_in_dir(dc_dst,dstname,b,sz);
    if(dd>=0)fs_drive_restore(dctx);
    if(r==0){
        drive_ctx_t sc2=fs_drive_enter(sd>=0?sd:current_drive_idx);   /* 回源盘删源 */
        if(fs_delete_file_in_dir(dc_src,arg)==0)put_str("Moved.\n");
        fs_drive_restore(sc2);
    }
    mem_free(b);
}

/* ── CLS/VER/TIME ── */
void cmd_cls(){cls();}
void cmd_ver(){put_str("\nAMUN-DOS 6.5.1 (C)2026 AMUNOS Team\n\n");}
static unsigned char r(unsigned char r){io_out8(0x70,r);return io_in8(0x71);}
static void pb(unsigned char v){put_char('0'+((v>>4)&0x0F),0x07);put_char('0'+(v&0x0F),0x07);}
void cmd_time(){pb(r(0x04));put_char(':',0x07);pb(r(0x02));put_char(':',0x07);pb(r(0x00));put_str(" ");pb(r(0x09));put_char('-',0x07);pb(r(0x08));put_char('-',0x07);pb(r(0x07));put_char('\n',0x07);}

/* ── HELP ── */
void cmd_help(char* arg){
    char f[2];arg=fparse(arg,f);upper(arg);
    int page=(f[0]=='p'||f[0]=='P');
    if(*arg){
        if(!strcmp(arg,"DIR"))put_str("DIR [-w] [-p] — list directory (p=paged)\n");
        else if(!strcmp(arg,"CD"))put_str("CD [dir|..|<|/] — change dir\n");
        else if(!strcmp(arg,"TYPE"))put_str("TYPE file — show file content\n");
        else if(!strcmp(arg,"ECHO"))put_str("ECHO text > file — write file\nECHO text — print text\n");
        else if(!strcmp(arg,"SER"))put_str("SER text — write text to COM1 serial\n");
        else if(!strcmp(arg,"LPT"))put_str("LPT text — write text to LPT1 parallel\n");
        else if(!strcmp(arg,"REN"))put_str("REN old new — rename file\n");
        else if(!strcmp(arg,"COPY"))put_str("COPY src dst — copy file\n");
        else if(!strcmp(arg,"MOV"))put_str("MOV src dst — move file\n");
        else if(!strcmp(arg,"DEL"))put_str("DEL file — delete file\n");
        else if(!strcmp(arg,"MD"))put_str("MD name — create directory\n");
        else if(!strcmp(arg,"EDIT"))put_str("EDIT file — text editor\n");
        else if(!strcmp(arg,"ELF"))put_str("ELF file.elf — load & run ELF executable\n");
        else if(!strcmp(arg,"TCC"))put_str("TCC file.c [-o out] — compile C to ELF (run on A:, uses BIN/TCC.ELF + USR/INCLUDE/LIB)\n");
        else if(!strcmp(arg,"INSTALL"))put_str("INSTALL prog[.ext] [name] — copy to A:/BIN + register in CMDS.BIN\n");
        else if(!strcmp(arg,"HELP"))put_str("HELP [cmd] [-p] — show help (p=paged)\n");
        else put_str("No help for that command.\n");
        return;
    }
    static const char *h[] = {
        " DIR [-w] [-p]    List directory (p=paged)",
        " CD  dir/dir/dir  Change dir (multi-level)",
        " TYPE file        Show file content",
        " ECHO text > file Write file",
        " SER text         Write text to COM1 (serial)",
        " LPT text         Write text to LPT1 (parallel)",
        " REN old new      Rename file",
        " COPY src dst     Copy file (cross-drive: COPY C:/A.TXT B:)",
        " MOV src dst      Move file",
        " DEL file         Delete file",
        " MD  name         Create directory",
        " RMDIR name       Delete empty directory",
        " EDIT file        Text editor (F1-Help F2-Save F3-Open F4-New F5-Quit)",
        " ELF  file.elf    Load & run ELF executable",
        " TCC  file.c      Compile C to ELF (TinyCC, shows elapsed)",
        " INSTALL prg      Copy to A:/BIN + register in CMDS.BIN (any drive)",
        " CLS              Clear screen",
        " TIME             Show time",
        " VER              Show version",
        " HELP [cmd]       This help",
        " CMD -?           Show any command's usage",
        " A: - D:          Switch drive",
        " (file args accept dirs & drives: TYPE A:/BIN/X.ELF, COPY C:/A.TXT B:)",
        " (type name: XXX runs XXX.ELF/.EXE/.COM/.BIN in cwd or any drive root)",
        " (CMDS.BIN holds \"NAME TARGET\" lines, searchable on all drives)",
        " (tree: A:/BOOT A:/BIN A:/USR/INCLUDE A:/USR/LIB A:/USR/SRC B:/USR/SRC)",
    };
    put_str("\n");
    int nlines = (int)(sizeof(h)/sizeof(h[0]));
    int lc = 0;
    for (int i = 0; i < nlines; i++) {
        put_str(h[i]);put_char('\n',0x07);lc++;
        if (page && lc >= 20) {
            put_str("-- Press any key to continue (Ctrl+C to stop) --\n");
            if(!wait_key_or_abort()){put_str("\n");return;}
            lc = 0;
        }
    }
    put_char('\n',0x07);
}

/* ── ELF: 加载并运行静态 ELF 可执行文件 (v6.5) ── */
static void put_hex(unsigned n){
    char d[]="0123456789ABCDEF";
    char b[9]; int i=0;
    do{ b[i++]=d[n&0xF]; n>>=4; }while(n);
    put_str("0x");
    while(i>0) put_char(b[--i],0x07);
}

/* ── argv 块 (供 crt0.S 的 _start 读取) ──
 * 布局: [ARGV_BASE]=argc, [ARGV_BASE+4]=argv[] 指针数组(NULL 终止),
 * 字符串数据在 ARGV_DATA。地址在 ELF 载入区之后、用户堆 0x200000 之前。 */
#define ARGV_BASE  0x1F0000
#define ARGV_PTRS  (ARGV_BASE + 4)
#define ARGV_DATA  (ARGV_BASE + 0x200)
#define ARGV_MAX   16

static void build_argv(const char *prog, char *args){
    int *argc = (int*)ARGV_BASE;
    char **ap = (char**)ARGV_PTRS;
    char *dp = (char*)ARGV_DATA;
    int n = 0;

    /* argv[0] = 程序名 */
    ap[n] = dp;
    while(*prog) *dp++ = *prog++;
    *dp++ = 0;
    n++;

    /* 空白切分剩余参数 (保持大小写, TCC 选项大小写敏感) */
    char *p = args;
    while(*p && n < ARGV_MAX - 1){
        while(*p==' ' || *p=='\t') p++;
        if(!*p) break;
        ap[n] = dp;
        while(*p && *p!=' ' && *p!='\t') *dp++ = *p++;
        *dp++ = 0;
        n++;
    }
    ap[n] = 0;
    *argc = n;
}

#define PROG_STACK_SIZE 0x10000   /* 前台程序任务栈 (64KB, 内核堆分配) */

void cmd_elf(char* arg){
    if(!*arg){put_str("Usage: ELF file [args]\n");return;}

    /* 提取程序名 (首 token), 仅文件名转大写; 参数保持原大小写
     * 缓冲足够容纳路径 (如 BIN\TCC.ELF, \BIN\EDIT.ELF) */
    char prog[64];
    int i = 0;
    while(arg[i] && arg[i]!=' ' && arg[i]!='\t' && i<62){ prog[i]=arg[i]; i++; }
    prog[i] = 0;
    upper(prog);
    char *args = arg + i;

    /* 跨盘: 盘符限定路径 (如 A:\BIN\TCC.ELF) 切盘解析, 读毕还原 */
    drive_ctx_t octx; int od=fs_drive_open(prog,&octx);
    FAT12Entry e;
    int dc=fs_resolve_path(prog);if(dc<0){if(od>=0)fs_drive_restore(octx);put_str("Not found.\n");return;}
    if(fs_find_entry_in_dir(dc,prog,&e)<0){if(od>=0)fs_drive_restore(octx);put_str("Not found.\n");return;}

    /* 从内核堆暂存 (去掉 32KB 限制, 支持 ~300KB 的 tcc.elf) */
    char *ebuf = (char*)mem_alloc((unsigned)e.size + 1);
    if(!ebuf){if(od>=0)fs_drive_restore(octx);put_str("No memory.\n");return;}
    fs_read_file(&e, ebuf);
    if(od>=0)fs_drive_restore(octx);     /* 还原: 让程序跑在用户当前盘 */

    int entry = elf_load((unsigned char*)ebuf, e.size);
    mem_free(ebuf);
    if(entry <= 0){ put_str("Invalid ELF format\n"); return; }

    put_str("Running ");put_str(prog);put_str(" (entry=");
    put_hex((unsigned)entry);
    put_str(")\n");

    /* argv 块 (0x1F0000) + 程序作为独立调度任务运行 */
    build_argv(prog, args);
    prog_active = 1;
    prog_killed = 0;
    prog_exit_status = 0;

    struct task *t = task_create((void (*)(void))(unsigned)entry, PROG_STACK_SIZE);
    if(!t){
        put_str("No task slot.\n");
        prog_active = 0;
        return;
    }
    task_set_prog(t);
    task_wait(t);              /* 阻塞等待程序任务退出 (EXITED) */
    task_set_prog(0);
    prog_active = 0;

    if(prog_killed){
        key_pressed = 0;       /* 消费 Ctrl+C 事件, 避免 REPL 重复清行 */
        put_str("^C terminated\n");
    } else {
        put_str("exit="); put_num((unsigned)prog_exit_status); put_char('\n', 0x07);
    }
}

/* ── TCC: 编译 C 源码为 ELF (复用 cmd_elf 的加载/argv/任务运行) ──
 * v6.5.1 修复 "Not found": 前缀全改盘符限定绝对路径 (A: 根 → 任何盘/目录可用)。
 * v6.5 目录树: TCC.ELF 在 A:\BIN\; 头/库在 A:\USR\INCLUDE|LIB → 注入 -I/-L/-B */
void cmd_tcc(char* arg){
    if(!*arg){put_str("Usage: TCC file.c [-o out]\n");return;}
    unsigned t0 = task_ticks();
    put_str("Compiling ");put_str(arg);put_str(" ...\n");
    char full[128];
    int n = 0;
    const char *pre = "A:/BIN/TCC.ELF -static -I A:/USR/INCLUDE -L A:/USR/LIB -B A:/USR/LIB ";
    while(*pre && n < 126) full[n++] = *pre++;
    while(*arg && n < 126) full[n++] = *arg++;
    full[n] = 0;
    cmd_elf(full);
    unsigned dt = task_ticks() - t0;
    put_str("TCC done (");put_num(dt/100);put_char('.',0x07);put_num((dt/10)%10);put_str("s)\n");
}

/* ── INSTALL (v6.5.1): 复制程序到 A:\BIN + 注册到 A:\CMDS.BIN ──
 * 用法: INSTALL prog[.ext] [cmdname]
 *   cmdname 默认 = 去扩展的源文件名; 注册后任何盘/目录敲 cmdname 即运行。 */
void cmd_install(char* arg){
    upper(arg);
    if(!*arg){put_str("Usage: INSTALL prog[.ext] [name]\n");return;}
    char *sp=arg;while(*sp&&*sp!=' ')sp++;
    char *name=0;
    if(*sp){*sp++=0;while(*sp==' ')sp++;name=sp;}

    /* 源: 可带盘符/绝对路径; 堆读全 (fs_read_file 在 size 处写 NUL) */
    drive_ctx_t sctx; int sd=fs_drive_open(arg,&sctx);
    int dc=fs_resolve_path(arg);if(dc<0){if(sd>=0)fs_drive_restore(sctx);put_str("Not found.\n");return;}
    FAT12Entry e;if(fs_find_entry_in_dir(dc,arg,&e)<0){if(sd>=0)fs_drive_restore(sctx);put_str("Not found.\n");return;}
    if(e.attr&0x10){if(sd>=0)fs_drive_restore(sctx);put_str("Cannot install dir.\n");return;}
    char *b=(char*)mem_alloc((unsigned)e.size+1);if(!b){if(sd>=0)fs_drive_restore(sctx);put_str("No memory.\n");return;}
    fs_read_file(&e,b);int sz=e.size;
    if(sd>=0)fs_drive_restore(sctx);

    /* base = 源文件名含扩展 (arg 已被 fs_resolve_path 剥成裸名) */
    char base[13];int ci=0;
    while(arg[ci]&&ci<11)base[ci]=arg[ci],ci++;base[ci]=0;
    char defname[13];
    if(!name){int di=0;while(base[di]&&base[di]!='.'&&di<11)defname[di]=base[di],di++;defname[di]=0;name=defname;}

    /* 切 A: → 写 A:\BIN\<base> (覆盖旧) → 追加 A:\CMDS.BIN */
    drive_ctx_t actx=fs_drive_enter(0);
    int dbin=0;FAT12Entry be;
    if(fs_find_entry_in_dir(0,"BIN",&be)>=0&&(be.attr&0x10))dbin=be.start_cluster;
    fs_delete_file_in_dir(dbin,base);
    if(fs_create_file_in_dir(dbin,base,b,sz)!=0){
        fs_drive_restore(actx);mem_free(b);put_str("Install failed.\n");return;
    }
    char *cbuf;int csize=0;FAT12Entry ce;
    if(fs_find_entry_in_dir(0,"CMDS.BIN",&ce)>=0){
        cbuf=(char*)mem_alloc((unsigned)ce.size+1);
        if(!cbuf){fs_drive_restore(actx);mem_free(b);put_str("No memory.\n");return;}
        fs_read_file(&ce,cbuf);csize=ce.size;
    }else{
        cbuf=(char*)mem_alloc(1);
        if(!cbuf){fs_drive_restore(actx);mem_free(b);put_str("No memory.\n");return;}
        cbuf[0]=0;
    }
    char add[88];int an=0,j=0;
    while(name[j]&&an<70)add[an++]=name[j++];
    add[an++]=' ';add[an++]='A';add[an++]=':';add[an++]='/';
    add[an++]='B';add[an++]='I';add[an++]='N';add[an++]='/';
    j=0;while(base[j]&&an<82)add[an++]=base[j++];
    add[an++]='\n';
    char *nb=(char*)mem_alloc((unsigned)(csize+an+1));
    if(!nb){fs_drive_restore(actx);mem_free(cbuf);mem_free(b);put_str("No memory.\n");return;}
    for(int i=0;i<csize;i++)nb[i]=cbuf[i];
    for(int i=0;i<an;i++)nb[csize+i]=add[i];
    nb[csize+an]=0;
    fs_write_file_in_dir(0,"CMDS.BIN",nb,csize+an);   /* 走 inner: INSTALL 需改写 CMDS.BIN */
    mem_free(nb);mem_free(cbuf);mem_free(b);
    fs_drive_restore(actx);
    put_str("Installed: ");put_str(name);put_str(" -> A:/BIN/");put_str(base);put_char('\n',0x07);
}

/* ═══════════════ DISPATCH ═══════════════ */

/* ── 自定义命令 (v6.5.1): 返回 1=已处理
 *   1) CMDS.BIN 命令→ELF 对照表 (全盘 A:-D: 搜索, 跳过缺盘; 行格式 "NAME TARGET",
 *      可被 EDIT CMDS.BIN 编辑 / INSTALL 追加; ;/# 开头为注释)
 *   2) 扩展名自动补全: cwd 下 / 当前盘根下 XXX.ELF/.EXE/.COM/.BIN → 输入 XXX 即运行 */
static int cmd_custom(char* cmd, char* a1) {
    char line[112];
    int n = 0, j = 0;

    /* 1) 全盘 CMDS.BIN */
    for (int d = 0; d < 4; d++) {
        if (d != current_drive_idx && !fs_drive_present(d)) continue;
        drive_ctx_t octx = fs_drive_enter(d);
        FAT12Entry ce;
        if (fs_find_entry_in_dir(0, "CMDS.BIN", &ce) >= 0) {
            char *cbuf = (char*)mem_alloc((unsigned)ce.size + 1);
            if (cbuf) {
                fs_read_file(&ce, cbuf);
                char* p = cbuf;
                while (*p) {
                    char* ln = p;
                    while (*p && *p != '\n') p++;
                    if (*p == '\n') { *p = 0; p++; }
                    char* q = ln;
                    while (*q == ' ' || *q == '\t') q++;
                    if (*q && *q != ';' && *q != '#') {
                        char cn[12]; int ci = 0;
                        while (*q && *q != ' ' && *q != '\t' && ci < 11) cn[ci++] = to_upper(*q++);
                        cn[ci] = 0;
                        if (!strcmp(cn, cmd)) {
                            while (*q == ' ' || *q == '\t') q++;
                            char tgt[80]; int ti = 0;
                            while (*q && *q != '\r' && *q != '\n' && ti < 78) tgt[ti++] = *q++;
                            tgt[ti] = 0;
                            n = 0;
                            /* 目标已带盘符 (如 INSTALL 生成的 "A:\BIN\HW.EXE") → 原样用;
                             * 否则补来源盘盘符; 补盘符后仍相对 → 再补 \ */
                            int dq = (tgt[0]>='A'&&tgt[0]<='D'&&tgt[1]==':') ||
                                     (tgt[0]>='a'&&tgt[0]<='d'&&tgt[1]==':');
                            if (!dq) {
                                line[n++] = (char)('A'+d), line[n++] = ':';
                                if (tgt[0] != '/') line[n++] = '/';
                            }
                            for (ti = 0; tgt[ti] && n < 60; ti++) line[n++] = tgt[ti];
                            line[n++] = ' ';
                            j = 0; while (a1[j] && n < 108) line[n++] = a1[j++];
                            line[n] = 0;
                            mem_free(cbuf);
                            fs_drive_restore(octx);
                            cmd_elf(line);
                            return 1;
                        }
                    }
                }
                mem_free(cbuf);
            }
        }
        fs_drive_restore(octx);
    }

    /* 2) 扩展名自动补全: 先 cwd, 再当前盘根 */
    static const char *exts[] = { ".ELF", ".EXE", ".COM", ".BIN" };
    int tries[2] = { cwd_cluster, 0 };
    for (int t = 0; t < 2; t++) {
        for (int x = 0; x < 4; x++) {
            char fn[13];
            int ci = 0;
            while (cmd[ci] && ci < 8) fn[ci] = cmd[ci], ci++;
            for (int k = 0; k < 4; k++) fn[ci + k] = exts[x][k];
            fn[ci + 4] = 0;
            FAT12Entry e2;
            if (fs_find_entry_in_dir(tries[t], fn, &e2) >= 0 && !(e2.attr & 0x10)) {
                n = 0;
                if (tries[t] == 0 && cwd_cluster != 0) line[n++] = '/';   /* 根命中且非根 cwd → 补 / */
                j = 0; while (fn[j] && n < 60) line[n++] = fn[j];
                line[n++] = ' ';
                j = 0; while (a1[j] && n < 108) line[n++] = a1[j++];
                line[n] = 0;
                cmd_elf(line);
                return 1;
            }
        }
    }
    return 0;
}

void exec_cmd(char* line){
    char cmd[16],a1[48];int i=0,j=0;
    while(line[i]==' ')i++;if(!line[i])return;
    if(line[i+1]==':'){
        char d=to_upper(line[i]);
        if(d>='A'&&d<='D'){current_drive_idx=d-'A';cwd_path[0]=0;cwd_cluster=0;fs_init();}
        return;
    }
    while(line[i]&&line[i]!=' '&&j<15)cmd[j++]=to_upper(line[i++]);cmd[j]=0;
    while(line[i]==' ')i++;j=0;while(line[i]&&j<47)a1[j++]=line[i++];a1[j]=0;

    /* 命令辅助参数: CMD /? 或 CMD -? → 显示该命令用法 (v6.5; ECHO 的 /? 是文本) */
    if (strcmp(cmd,"ECHO") && strcmp(cmd,"HELP") &&
        a1[0]=='-' && a1[1]=='?' && !a1[2]) {
        cmd_help(cmd); return;
    }

    if(!strcmp(cmd,"DIR"))cmd_dir(a1);else if(!strcmp(cmd,"CD"))cmd_cd(a1);
    else if(!strcmp(cmd,"CLS"))cls();else if(!strcmp(cmd,"VER"))cmd_ver();
    else if(!strcmp(cmd,"HELP"))cmd_help(a1);else if(!strcmp(cmd,"ECHO"))cmd_echo(a1);
    else if(!strcmp(cmd,"SER"))cmd_ser(a1);else if(!strcmp(cmd,"LPT"))cmd_lpt(a1);
    else if(!strcmp(cmd,"TIME"))cmd_time();else if(!strcmp(cmd,"TYPE"))cmd_type(a1);
    else if(!strcmp(cmd,"REN"))cmd_ren(a1);else if(!strcmp(cmd,"COPY"))cmd_copy(a1);
    else if(!strcmp(cmd,"MOV"))cmd_mov(a1);
    else if(!strcmp(cmd,"ELF"))cmd_elf(a1);
    else if(!strcmp(cmd,"TCC"))cmd_tcc(a1);
    else if(!strcmp(cmd,"INSTALL"))cmd_install(a1);
    else if(!strcmp(cmd,"MD")){if(*a1)fs_create_directory(a1);else put_str("Usage: MD name\n");}
    else if(!strcmp(cmd,"DEL")){if(*a1){upper(a1);fs_delete_file(a1);}else put_str("Usage: DEL file\n");}
    else if(!strcmp(cmd,"RMDIR")){if(*a1){upper(a1);fs_delete_directory(a1);}else put_str("Usage: RMDIR dir\n");}
    /* EDIT 不再是内置命令: 走 CMDS.BIN 映射或 cwd 下 EDIT.ELF */
    else if(!cmd_custom(cmd, a1)) put_str("Bad command\n");
}
