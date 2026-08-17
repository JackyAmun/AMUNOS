#include "common.h"

int cmd_len=0,cmd_pos=0,cur_x=0,cur_y=0,current_drive_idx=0,prompt_len=5;
char cmd_buf[128],cwd_path[128]="";

extern volatile int is_shift,caps_lock,key_pressed;
extern volatile char current_char;
extern unsigned char last_scancode,kmap[],kmap_s[];

int strlen(const char*s){int i=0;while(s[i])i++;return i;}
int strcmp(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return*(unsigned char*)a-*(unsigned char*)b;}
void strcpy(char*d,const char*s){while((*d++=*s++));}
void put_num(unsigned n){if(!n){put_char('0',0x07);return;}char b[12];int i=0;while(n){b[i++]='0'+n%10;n/=10;}while(--i>=0)put_char(b[i],0x07);}
char to_upper(char c){return(c>='a'&&c<='z')?c-32:c;}
char drive_letter(void){return (char)('A'+current_drive_idx);}

void print_prompt(){
    cur_x=0;
    put_char(drive_letter(),0x0E);
    put_str(":\\");if(*cwd_path)put_str(cwd_path);
    put_str("> ");
    prompt_len=cur_x;   // "A:\PATH> " 的总宽度
    update_cursor();
}

/* 从 cmd_pos 位置开始重绘 */
static void redraw(int pos){
    cur_x=prompt_len+pos;
    for(int i=pos;i<cmd_len;i++){
        put_char(cmd_buf[i],0x0F);
    }
    /* 末尾清空格直接写 VRAM, 不镜像到串口 (否则回显变成 "s e r" 错乱) */
    int ec = prompt_len + cmd_len;
    if (ec < 80) {
        char *v = (char*)0xB8000 + (cur_y * 80 + ec) * 2;
        v[0] = ' '; v[1] = 0x07;
    }
    cur_x=prompt_len+pos;update_cursor();
}

/* 后台演示任务: 每 0.5 秒在屏幕右上角更新计数, 证明多任务运行 (GUI 可见) */
static void demo_clock_task() {
    unsigned n = 0;
    char *v = (char*)0xB8000 + 158;   /* 右上角 (row 0, col 79) */
    while (1) {
        v[0] = '0' + (n % 10);
        v[1] = 0x4A;                  /* 红底 */
        task_sleep(50);               /* 0.5 秒 */
        n++;
    }
}

void kmain(){
    serial_init();
    cls();
    put_str("AMUNOS Kernel v6.5.1 (Multi-Drive)\n");
    serial_puts("AMUNOS v6.5.1 serial ready\n");   /* 冒烟标记: serial_puts 把 \n 翻成 CRLF */
    init_idt();
    mem_init();
    timer_init();
    fs_init();
    task_init();
    task_create(demo_clock_task, 2048);
    __asm__ volatile("sti");

    print_prompt();

    while(1){
        input_poll();

        if(key_pressed==1){  // 字符插入
            char c=current_char;
            if(cmd_len<120){
                for(int i=cmd_len;i>cmd_pos;i--)cmd_buf[i]=cmd_buf[i-1];
                cmd_buf[cmd_pos]=c;cmd_len++;cmd_pos++;cmd_buf[cmd_len]=0;
                redraw(cmd_pos-1);
            }
            key_pressed=0;
        }
        else if(key_pressed==2){  // 回车
            put_char('\n',0x07);
            key_pressed=0;  /* 先清残留回车, 避免程序首个 kbd_read_char 误读空行 */
            if(cmd_len>0)exec_cmd(cmd_buf);
            for(int i=0;i<128;i++)cmd_buf[i]=0;
            cmd_len=0;cmd_pos=0;
            print_prompt();
        }
        else if(key_pressed==3){  // BS
            if(cmd_pos>0&&cmd_len>0){
                for(int i=cmd_pos-1;i<cmd_len-1;i++)cmd_buf[i]=cmd_buf[i+1];
                cmd_len--;cmd_pos--;cmd_buf[cmd_len]=0;
                redraw(cmd_pos);
            }
            key_pressed=0;
        }
        else if(key_pressed==9){  // DEL
            if(cmd_pos<cmd_len){
                for(int i=cmd_pos;i<cmd_len-1;i++)cmd_buf[i]=cmd_buf[i+1];
                cmd_len--;cmd_buf[cmd_len]=0;
                redraw(cmd_pos);
            }
            key_pressed=0;
        }
        else if(key_pressed==10){cmd_pos=0;redraw(0);key_pressed=0;}          // HOME
        else if(key_pressed==11){cmd_pos=cmd_len;redraw(cmd_len);key_pressed=0;} // END
        else if(key_pressed==4){if(cmd_pos>0){cmd_pos--;cur_x=prompt_len+cmd_pos;update_cursor();}key_pressed=0;}  // ←
        else if(key_pressed==5){if(cmd_pos<cmd_len){cmd_pos++;cur_x=prompt_len+cmd_pos;update_cursor();}key_pressed=0;}  // →
        else if(key_pressed==12){  // Ctrl+C — 清行 (无前台程序时)
            for(int i=0;i<128;i++)cmd_buf[i]=0;
            cmd_len=0;cmd_pos=0;force_kill=0;key_pressed=0;
            put_char('^',0x0C);put_char('C',0x0C);put_char('\n',0x07);
            print_prompt();
        }
        else if(key_pressed>0)key_pressed=0;
    }
}
