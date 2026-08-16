/* x86gen.c — 原生 x86 代码生成器 (v6.2)
 *
 * 支持:
 *   int main(){...}
 *   printf(expr); a = expr; input(); return expr;
 *   if (cond) { ... }      — cond: a>b a<b a>=b a<=b a==b a!=b
 *   while (cond) { ... }
 *   表达式: + - * / % ( ) 常量 变量(a-d) input()
 *   运行时: 0x1000 printnum, 0x1010 getnum, 变量 0x2000-0x200C
 */

static unsigned char *X; static int P;
static void Xb(unsigned char b){X[P++]=b;}
static void Xi(int n){Xb(n&0xFF);Xb((n>>8)&0xFF);Xb((n>>16)&0xFF);Xb((n>>24)&0xFF);}
static int var_addr(char c){return 0x2000+(c-'a')*4;}
static void load_eax_mem(int a){Xb(0xA1);Xi(a);}
static void load_eax_imm(int v){Xb(0xB8);Xi(v);}
static void call_abs(int t){int r=t-(0x100000+P+5);Xb(0xE8);Xi(r);}

static void gen_factor(char **s);
static void gen_term(char **s);
static void gen_expr(char **s);

static void gen_factor(char **s){
    char *p=*s;while(*p==' ')p++;
    if(*p>='0'&&*p<='9'){int v=0;while(*p>='0'&&*p<='9'){v=v*10+(*p-'0');p++;}*s=p;load_eax_imm(v);}
    else if(*p>='a'&&*p<='z'){int a=var_addr(*p);p++;*s=p;load_eax_mem(a);}
    else if(p[0]=='i'&&p[1]=='n'&&p[2]=='p'&&p[3]=='u'&&p[4]=='t'){
        p+=5;while(*p==' ')p++;if(*p=='(')p++;if(*p==')')p++;*s=p;call_abs(0x1010);}
    else if(*p=='('){p++;*s=p;gen_expr(s);p=*s;while(*p==' ')p++;if(*p==')')p++;*s=p;}
    else *s=p;
}
static void gen_term(char **s){
    gen_factor(s);
    while(1){
        char *p=*s;while(*p==' ')p++;
        char op=*p;if(op!='*'&&op!='/'&&op!='%')break;p++;*s=p;
        Xb(0x50);gen_factor(s);Xb(0x89);Xb(0xC3);Xb(0x58);
        if(op=='*'){Xb(0xF7);Xb(0xEB);}
        else if(op=='/'){Xb(0x99);Xb(0xF7);Xb(0xFB);}
        else if(op=='%'){Xb(0x99);Xb(0xF7);Xb(0xFB);Xb(0x89);Xb(0xD8);}
    }
}
static void gen_expr(char **s){
    gen_term(s);
    while(1){
        char *p=*s;while(*p==' ')p++;
        char op=*p;if(op!='+'&&op!='-')break;p++;*s=p;
        Xb(0x50);gen_term(s);Xb(0x89);Xb(0xC3);Xb(0x58);
        if(op=='+'){Xb(0x01);Xb(0xD8);}else{Xb(0x29);Xb(0xD8);}
    }
}

/* 生成条件判断 → EAX = 0/1 */
static void gen_cond(char **s){
    gen_expr(s);                 // EAX = left
    char *p=*s;while(*p==' ')p++;
    char c1=p[0],c2=p[1];
    int op=0; // 0=?, 1==,2!=,3>,4<,5>=,6<=
    if(c1=='='&&c2=='='){op=1;p+=2;}
    else if(c1=='!'&&c2=='='){op=2;p+=2;}
    else if(c1=='>'&&c2=='='){op=5;p+=2;}
    else if(c1=='<'&&c2=='='){op=6;p+=2;}
    else if(c1=='>'){op=3;p+=1;}
    else if(c1=='<'){op=4;p+=1;}
    else { // 无比较 → 表达式真值
        Xb(0x85);Xb(0xC0);   // TEST EAX,EAX
        Xb(0x0F);Xb(0x95);Xb(0xC0);Xb(0x0F);Xb(0xB6);Xb(0xC0);  // SETNZ AL; MOVZX
        *s=p;return;
    }
    Xb(0x50);                 // PUSH EAX (left)
    *s=p;gen_expr(s);         // EAX = right
    Xb(0x89);Xb(0xC3);        // MOV EBX,EAX (right)
    Xb(0x58);                 // POP EAX (left)
    Xb(0x39);Xb(0xD8);        // CMP EAX,EBX
    // SETcc AL
    unsigned char setcc[]={0x0F,0x94,0xC0, 0x0F,0x95,0xC0, 0x0F,0x9F,0xC0, 0x0F,0x9C,0xC0, 0x0F,0x9D,0xC0, 0x0F,0x9E,0xC0};
    // == !=  >  <  >= <=
    for(int k=0;k<3;k++)Xb(setcc[(op-1)*3+k]);
    Xb(0x0F);Xb(0xB6);Xb(0xC0);  // MOVZX EAX,AL
}

/* 提取表达式到 ; } ) 为止 */
static int expr_len(char *s){int n=0;while(s[n]&&s[n]!=';'&&s[n]!=')'&&s[n]!='}')n++;return n;}

/* 编译一个语句块 (s 指向 '{' 之后) */
static void gen_block(char **sp){
    char *s=*sp;
    while(*s){
        while(*s==' '||*s=='\t'||*s=='\n'||*s=='\r'||*s==';')s++;
        if(*s=='}'){s++;break;}if(!*s)break;

        // printf(expr)
        if(s[0]=='p'&&s[1]=='r'&&s[2]=='i'&&s[3]=='n'&&s[4]=='t'&&s[5]=='f'){
            s+=6;while(*s==' ')s++;if(*s=='(')s++;
            char *e=s;int n=expr_len(s);char eb[64];int i=0;
            while(i<n&&i<63){eb[i]=e[i];i++;}eb[i]=0;
            char *ep=eb;gen_expr(&ep);
            Xb(0xA3);Xi(0x2000);call_abs(0x1000);
            s=e+n;while(*s==' ')s++;if(*s==')')s++;continue;
        }
        // a = expr
        if(*s>='a'&&*s<='z'&&((s[1]=='=')||(s[1]==' '&&s[2]=='='))){
            int va=var_addr(*s);s+=2;while(*s==' ')s++;if(*s=='=')s++;while(*s==' ')s++;
            char *e=s;int n=expr_len(s);char eb[64];int i=0;
            while(i<n&&i<63){eb[i]=e[i];i++;}eb[i]=0;
            char *ep=eb;gen_expr(&ep);
            Xb(0xA3);Xi(va);s=e+n;continue;
        }
        // if (cond) { }
        if(s[0]=='i'&&s[1]=='f'){
            s+=2;while(*s==' ')s++;if(*s=='(')s++;
            char *e=s;int n=0;int depth=0;
            while(s[n]){if(s[n]=='(')depth++;if(s[n]==')'){if(depth==0)break;depth--;}n++;}
            char eb[64];int i=0;while(i<n&&i<63){eb[i]=e[i];i++;}eb[i]=0;
            char *ep=eb;gen_cond(&ep);
            // TEST EAX,EAX; JZ skip — jzpos = P (offset 字段起点)
            Xb(0x85);Xb(0xC0);Xb(0x0F);Xb(0x84);int jzpos=P;Xi(0);
            s=e+n;while(*s==' ')s++;if(*s==')')s++;while(*s==' ')s++;if(*s=='{')s++;
            gen_block(&s);
            int rel=P-(jzpos+4);
            X[jzpos]=rel&0xFF;X[jzpos+1]=(rel>>8)&0xFF;X[jzpos+2]=(rel>>16)&0xFF;X[jzpos+3]=(rel>>24)&0xFF;
            continue;
        }
        // while (cond) { }
        if(s[0]=='w'&&s[1]=='h'&&s[2]=='i'&&s[3]=='l'&&s[4]=='e'){
            int ls=P;
            s+=5;while(*s==' ')s++;if(*s=='(')s++;
            char *e=s;int n=0;int depth=0;
            while(s[n]){if(s[n]=='(')depth++;if(s[n]==')'){if(depth==0)break;depth--;}n++;}
            char eb[64];int i=0;while(i<n&&i<63){eb[i]=e[i];i++;}eb[i]=0;
            char *ep=eb;gen_cond(&ep);
            Xb(0x85);Xb(0xC0);Xb(0x0F);Xb(0x84);int jzpos=P;Xi(0);
            s=e+n;while(*s==' ')s++;if(*s==')')s++;while(*s==' ')s++;if(*s=='{')s++;
            gen_block(&s);
            Xb(0xE9);Xi(ls-(P+4));   // JMP loop_start
            int rel=P-(jzpos+4);
            X[jzpos]=rel&0xFF;X[jzpos+1]=(rel>>8)&0xFF;X[jzpos+2]=(rel>>16)&0xFF;X[jzpos+3]=(rel>>24)&0xFF;
            continue;
        }
        // return expr
        if(s[0]=='r'&&s[1]=='e'&&s[2]=='t'&&s[3]=='u'&&s[4]=='r'&&s[5]=='n'){
            s+=6;while(*s==' ')s++;
            char *e=s;int n=expr_len(s);char eb[64];int i=0;
            while(i<n&&i<63){eb[i]=e[i];i++;}eb[i]=0;
            char *ep=eb;gen_expr(&ep);
            Xb(0x89);Xb(0xEC);Xb(0x5D);Xb(0xC3);s=e+n;continue;
        }
        s++;
    }
    *sp=s;
}

int x86_compile(char *src, unsigned char *out){
    X=out;P=0;char *s=src;
    while(*s==' '||*s=='\n'||*s=='\r'||*s=='\t')s++;
    if(s[0]!='i'||s[1]!='n'||s[2]!='t')return 0;s+=3;
    while(*s==' ')s++;
    if(s[0]!='m'||s[1]!='a'||s[2]!='i'||s[3]!='n')return 0;s+=4;
    while(*s==' ')s++;if(*s!='(')return 0;s++;while(*s&&*s!=')')s++;if(*s!=')')return 0;s++;
    while(*s==' ')s++;if(*s!='{')return 0;s++;
    Xb(0x55);Xb(0x89);Xb(0xE5);
    gen_block(&s);
    Xb(0xB8);Xi(0);Xb(0x89);Xb(0xEC);Xb(0x5D);Xb(0xC3);
    return P;
}
