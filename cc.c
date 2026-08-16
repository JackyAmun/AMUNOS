/* cc.c — C4 Compiler for AMUNOS (port of rswier/c4) */

#include "common.h"

#define PSZ (16*1024)
static int   mem_txt[PSZ/4], mem_stk[PSZ/4];   // 16KB each (int arrays)
static char  mem_dat[PSZ], mem_sym[PSZ];        // 16KB each
static char  mem_src[8192];                      // 8KB source buffer
static char  *src_p, *src_lp, *dat_ptr;
static int   *emit, *emit_end, *idptr, *sym_tab;
static int   token, ival, expr_ty, loc_off, line_no, src_flag, dbg_flag;

enum { Num=128,Fun,Sys,Glo,Loc,Id,Char,Else,Enum,If,Int,Return,Sizeof,While,
  Assign,Cond,Lor,Lan,Or,Xor,And,Eq,Ne,Lt,Gt,Le,Ge,Shl,Shr,Add,Sub,Mul,Div,Mod,Inc,Dec,Brak };

enum { LEA,IMM,JMP,JSR,BZ,BNZ,ENT,ADJ,LEV,LI,LC,SI,SC,PSH,
  OR,XOR,AND,EQ,NE,LT,GT,LE,GE,SHL,SHR,ADD,SUB,MUL,DIV,MOD,
  OPEN,READ,CLOS,PRTF,MALC,FREE,MSET,MCMP,EXIT };
enum { CHAR_T, INT_T, PTR_T };
enum { Tk,Hash,Name,Class,Type,Val,HClass,HType,HVal,Idsz };

static int *pc_main;
static int cc_ok;

// ── Print helpers ──
static void sput(char* s){while(*s)put_char(*s++,0x07);}
static void sputn(int n){if(n==0){put_char('0',0x07);return;}char b[12];int i=0;if(n<0){put_char('-',0x07);n=-n;}while(n>0){b[i++]='0'+(n%10);n/=10;}while(--i>=0)put_char(b[i],0x07);}
static void serr(char* s){sput("L");sputn(line_no);sput(":");sput(s);cc_ok=-1;}

// ── Memory (replacing malloc/memset/memcmp) ──
static void* mm_clear(void* p, int sz){char* c=p;for(int i=0;i<sz;i++)c[i]=0;return p;}
static int mm_cmp(char* a,char* b,int n){while(n--){if(*a!=*b)return *a-*b;a++;b++;}return 0;}

// ── next() — lexical analyzer ──
static void cc_next() {
  char* pp;
  while(token=*src_p){
    ++src_p;
    if(token=='\n'){
      if(src_flag){sputn(line_no);sput(": ");for(char* x=src_lp;x<src_p-1;x++)put_char(*x,0x07);sput("\n");src_lp=src_p;
        while(emit_end<emit){sput("    ");int op=*++emit_end;
          char* tbl="LEA IMM JMP JSR BZ  BNZ ENT ADJ LEV LI  LC  SI  SC  PSH OR  XOR AND EQ  NE  LT  GT  LE  GE  SHL SHR ADD SUB MUL DIV MOD OPENREADCLOSPRTFMALCFREEMSETMCMPEXIT";
          for(int k=0;k<4;k++)put_char(tbl[op*4+k],0x07);if(op<=ADJ){sput(" ");sputn(*++emit_end);}sput("\n");}}
      ++line_no;
    }else if(token=='#'){while(*src_p&&*src_p!='\n')++src_p;}
    else if((token>='a'&&token<='z')||(token>='A'&&token<='Z')||token=='_'){
      pp=src_p-1;
      while((*src_p>='a'&&*src_p<='z')||(*src_p>='A'&&*src_p<='Z')||(*src_p>='0'&&*src_p<='9')||*src_p=='_')
        token=token*147+*src_p++;
      token=(token<<6)+(src_p-pp);idptr=sym_tab;
      while(idptr[Tk]){if(token==idptr[Hash]&&!mm_cmp((char*)(int)idptr[Name],pp,src_p-pp)){token=idptr[Tk];return;}idptr+=Idsz;}
      idptr[Name]=(int)pp;idptr[Hash]=token;token=idptr[Tk]=Id;return;
    }else if(token>='0'&&token<='9'){
      if(ival=token-'0'){while(*src_p>='0'&&*src_p<='9')ival=ival*10+*src_p++-'0';}
      else if(*src_p=='x'||*src_p=='X'){while((token=*++src_p)&&((token>='0'&&token<='9')||(token>='a'&&token<='f')||(token>='A'&&token<='F')))ival=ival*16+(token&15)+(token>='A'?9:0);}
      else{while(*src_p>='0'&&*src_p<='7')ival=ival*8+*src_p++-'0';}
      token=Num;return;
    }else if(token=='/'){if(*src_p=='/'){++src_p;while(*src_p&&*src_p!='\n')++src_p;}else{token=Div;return;}}
    else if(token=='\''||token=='"'){pp=dat_ptr;while(*src_p&&*src_p!=token){if((ival=*src_p++)=='\\'&&(ival=*src_p++)=='n')ival='\n';if(token=='"')*dat_ptr++=ival;}++src_p;if(token=='"')ival=(int)pp;else token=Num;return;}
    else if(token=='='){if(*src_p=='='){++src_p;token=Eq;}else token=Assign;return;}
    else if(token=='+'){if(*src_p=='+'){++src_p;token=Inc;}else token=Add;return;}
    else if(token=='-'){if(*src_p=='-'){++src_p;token=Dec;}else token=Sub;return;}
    else if(token=='!'){if(*src_p=='='){++src_p;token=Ne;}return;}
    else if(token=='<'){if(*src_p=='='){++src_p;token=Le;}else if(*src_p=='<'){++src_p;token=Shl;}else token=Lt;return;}
    else if(token=='>'){if(*src_p=='='){++src_p;token=Ge;}else if(*src_p=='>'){++src_p;token=Shr;}else token=Gt;return;}
    else if(token=='|'){if(*src_p=='|'){++src_p;token=Lor;}else token=Or;return;}
    else if(token=='&'){if(*src_p=='&'){++src_p;token=Lan;}else token=And;return;}
    else if(token=='^'){token=Xor;return;}else if(token=='%'){token=Mod;return;}
    else if(token=='*'){token=Mul;return;}else if(token=='['){token=Brak;return;}
    else if(token=='?'){token=Cond;return;}
    else if(token=='~'||token==';'||token=='{'||token=='}'||token=='('||token==')'||token==']'||token==','||token==':')return;
  }
}

// ── expr() — expression parser ──
static void cc_expr(int lev){
  int t,*d;
  if(!token){serr("eof in expr\n");return;}
  if(token==Num){*++emit=IMM;*++emit=ival;cc_next();expr_ty=INT_T;}else if(token=='"'){*++emit=IMM;*++emit=ival;cc_next();while(token=='"')cc_next();dat_ptr=(char*)(((int)dat_ptr+sizeof(int))&-sizeof(int));expr_ty=PTR_T;}
  else if(token==Sizeof){cc_next();if(token=='(')cc_next();expr_ty=INT_T;if(token==Int)cc_next();else if(token==Char){cc_next();expr_ty=CHAR_T;}while(token==Mul){cc_next();expr_ty+=PTR_T;}if(token==')')cc_next();*++emit=IMM;*++emit=(expr_ty==CHAR_T)?1:4;expr_ty=INT_T;}
  else if(token==Id){d=idptr;cc_next();if(token=='('){cc_next();t=0;while(token!=')'){cc_expr(Assign);*++emit=PSH;++t;if(token==',')cc_next();}cc_next();if(d[Class]==Sys)*++emit=d[Val];else if(d[Class]==Fun){*++emit=JSR;*++emit=d[Val];}else{serr("bad call\n");return;}if(t){*++emit=ADJ;*++emit=t;}expr_ty=d[Type];}
    else if(d[Class]==Num){*++emit=IMM;*++emit=d[Val];expr_ty=INT_T;}else{if(d[Class]==Loc){*++emit=LEA;*++emit=loc_off-d[Val];}else if(d[Class]==Glo){*++emit=IMM;*++emit=d[Val];}else{serr("undef var\n");return;}*++emit=((expr_ty=d[Type])==CHAR_T)?LC:LI;}}
  else if(token=='('){cc_next();if(token==Int||token==Char){t=(token==Int)?INT_T:CHAR_T;cc_next();while(token==Mul){cc_next();t+=PTR_T;}if(token==')')cc_next();cc_expr(Inc);expr_ty=t;}else{cc_expr(Assign);if(token==')')cc_next();}}
  else if(token==Mul){cc_next();cc_expr(Inc);if(expr_ty>INT_T)expr_ty-=PTR_T;*++emit=(expr_ty==CHAR_T)?LC:LI;}
  else if(token==And){cc_next();cc_expr(Inc);if(*emit==LC||*emit==LI)--emit;expr_ty+=PTR_T;}
  else if(token=='!'){cc_next();cc_expr(Inc);*++emit=PSH;*++emit=IMM;*++emit=0;*++emit=EQ;expr_ty=INT_T;}
  else if(token=='~'){cc_next();cc_expr(Inc);*++emit=PSH;*++emit=IMM;*++emit=-1;*++emit=XOR;expr_ty=INT_T;}
  else if(token==Add){cc_next();cc_expr(Inc);expr_ty=INT_T;}
  else if(token==Sub){cc_next();*++emit=IMM;if(token==Num){*++emit=-ival;cc_next();}else{*++emit=-1;*++emit=PSH;cc_expr(Inc);*++emit=MUL;}expr_ty=INT_T;}
  else if(token==Inc||token==Dec){t=token;cc_next();cc_expr(Inc);if(*emit==LC){*emit=PSH;*++emit=LC;}else if(*emit==LI){*emit=PSH;*++emit=LI;}*++emit=PSH;*++emit=IMM;*++emit=(expr_ty>PTR_T)?4:1;*++emit=(t==Inc)?ADD:SUB;*++emit=(expr_ty==CHAR_T)?SC:SI;}
  else{serr("bad expr\n");return;}
  while(token>=lev){
    t=expr_ty;
    if(token==Assign){cc_next();if(*emit==LC||*emit==LI)*emit=PSH;cc_expr(Assign);*++emit=((expr_ty=t)==CHAR_T)?SC:SI;}
    else if(token==Cond){cc_next();*++emit=BZ;d=++emit;cc_expr(Assign);if(token==':')cc_next();*d=(int)(emit+3);*++emit=JMP;d=++emit;cc_expr(Cond);*d=(int)(emit+1);}
    else if(token==Lor){cc_next();*++emit=BNZ;d=++emit;cc_expr(Lan);*d=(int)(emit+1);expr_ty=INT_T;}
    else if(token==Lan){cc_next();*++emit=BZ;d=++emit;cc_expr(Or);*d=(int)(emit+1);expr_ty=INT_T;}
    else if(token==Or){cc_next();*++emit=PSH;cc_expr(Xor);*++emit=OR;expr_ty=INT_T;}
    else if(token==Xor){cc_next();*++emit=PSH;cc_expr(And);*++emit=XOR;expr_ty=INT_T;}
    else if(token==And){cc_next();*++emit=PSH;cc_expr(Eq);*++emit=AND;expr_ty=INT_T;}
    else if(token==Eq){cc_next();*++emit=PSH;cc_expr(Lt);*++emit=EQ;expr_ty=INT_T;}
    else if(token==Ne){cc_next();*++emit=PSH;cc_expr(Lt);*++emit=NE;expr_ty=INT_T;}
    else if(token==Lt){cc_next();*++emit=PSH;cc_expr(Shl);*++emit=LT;expr_ty=INT_T;}
    else if(token==Gt){cc_next();*++emit=PSH;cc_expr(Shl);*++emit=GT;expr_ty=INT_T;}
    else if(token==Le){cc_next();*++emit=PSH;cc_expr(Shl);*++emit=LE;expr_ty=INT_T;}
    else if(token==Ge){cc_next();*++emit=PSH;cc_expr(Shl);*++emit=GE;expr_ty=INT_T;}
    else if(token==Shl){cc_next();*++emit=PSH;cc_expr(Add);*++emit=SHL;expr_ty=INT_T;}
    else if(token==Shr){cc_next();*++emit=PSH;cc_expr(Add);*++emit=SHR;expr_ty=INT_T;}
    else if(token==Add){cc_next();*++emit=PSH;cc_expr(Mul);if((expr_ty=t)>PTR_T){*++emit=PSH;*++emit=IMM;*++emit=4;*++emit=MUL;}*++emit=ADD;}
    else if(token==Sub){cc_next();*++emit=PSH;cc_expr(Mul);if(t>PTR_T&&t==expr_ty){*++emit=SUB;*++emit=PSH;*++emit=IMM;*++emit=4;*++emit=DIV;expr_ty=INT_T;}else if((expr_ty=t)>PTR_T){*++emit=PSH;*++emit=IMM;*++emit=4;*++emit=MUL;*++emit=SUB;}else *++emit=SUB;}
    else if(token==Mul){cc_next();*++emit=PSH;cc_expr(Inc);*++emit=MUL;expr_ty=INT_T;}
    else if(token==Div){cc_next();*++emit=PSH;cc_expr(Inc);*++emit=DIV;expr_ty=INT_T;}
    else if(token==Mod){cc_next();*++emit=PSH;cc_expr(Inc);*++emit=MOD;expr_ty=INT_T;}
    else if(token==Inc||token==Dec){if(*emit==LC){*emit=PSH;*++emit=LC;}else if(*emit==LI){*emit=PSH;*++emit=LI;}*++emit=PSH;*++emit=IMM;*++emit=(expr_ty>PTR_T)?4:1;*++emit=(token==Inc)?ADD:SUB;*++emit=(expr_ty==CHAR_T)?SC:SI;*++emit=PSH;*++emit=IMM;*++emit=(expr_ty>PTR_T)?4:1;*++emit=(token==Inc)?SUB:ADD;cc_next();}
    else if(token==Brak){cc_next();*++emit=PSH;cc_expr(Assign);if(token==']')cc_next();if(t>PTR_T){*++emit=PSH;*++emit=IMM;*++emit=4;*++emit=MUL;}*++emit=ADD;*++emit=((expr_ty=t-PTR_T)==CHAR_T)?LC:LI;}
    else{serr("compiler error\n");return;}
  }
}

// ── stmt() — statement parser ──
static void cc_stmt(){
  int *a,*b;
  if(token==If){cc_next();if(token=='(')cc_next();cc_expr(Assign);if(token==')')cc_next();*++emit=BZ;b=++emit;cc_stmt();
    if(token==Else){*b=(int)(emit+3);*++emit=JMP;b=++emit;cc_next();cc_stmt();}*b=(int)(emit+1);}
  else if(token==While){cc_next();a=emit+1;if(token=='(')cc_next();cc_expr(Assign);if(token==')')cc_next();*++emit=BZ;b=++emit;cc_stmt();*++emit=JMP;*++emit=(int)a;*b=(int)(emit+1);}
  else if(token==Return){cc_next();if(token!=';')cc_expr(Assign);*++emit=LEV;if(token==';')cc_next();}
  else if(token=='{'){cc_next();while(token!='}')cc_stmt();cc_next();}
  else if(token==';'){cc_next();}else{cc_expr(Assign);if(token==';')cc_next();}
}

// ── cc_compile() — main driver (was main() in c4.c) ──
int cc_compile(char* src_buf, int src_len) {
  int bt,ty,*idmain;
  cc_ok=0;

  mm_clear(mem_sym,PSZ); mm_clear(mem_txt,PSZ);
  mm_clear(mem_dat,PSZ); mm_clear(mem_stk,PSZ);
  sym_tab=(int*)mem_sym; emit=(int*)mem_txt; emit_end=emit;
  dat_ptr=mem_dat; src_p=src_buf;

  // keywords
  src_p=(char*)"char else enum if int return sizeof while open read close printf malloc free memset memcmp exit void main";
  int i=Char;while(i<=While){cc_next();idptr[Tk]=i++;}
  i=OPEN;while(i<=EXIT){cc_next();idptr[Class]=Sys;idptr[Type]=INT_T;idptr[Val]=i++;}
  cc_next();idptr[Tk]=Char;cc_next();idmain=idptr;

  src_p=src_buf;src_lp=src_p;line_no=1;cc_next();

  // parse globals
  while(token){
    bt=INT_T;
    if(token==Int)cc_next();else if(token==Char){cc_next();bt=CHAR_T;}else if(token==Enum){cc_next();if(token!='{')cc_next();if(token=='{'){cc_next();i=0;while(token!='}'){if(token!=Id)return-1;cc_next();if(token==Assign){cc_next();if(token!=Num)return-1;i=ival;cc_next();}idptr[Class]=Num;idptr[Type]=INT_T;idptr[Val]=i++;if(token==',')cc_next();}cc_next();}}
    while(token!=';'&&token!='}'){
      ty=bt;while(token==Mul){cc_next();ty+=PTR_T;}
      if(token!=Id)return-1;if(idptr[Class])return-1;
      cc_next();sym_tab[Type]=ty;
      if(token=='('){idptr[Class]=Fun;idptr[Val]=(int)(emit+1);cc_next();i=0;
        while(token!=')'){ty=INT_T;if(token==Int)cc_next();else if(token==Char){cc_next();ty=CHAR_T;}while(token==Mul){cc_next();ty+=PTR_T;}if(token!=Id)return-1;if(idptr[Class]==Loc)return-1;idptr[HClass]=idptr[Class];idptr[Class]=Loc;idptr[HType]=idptr[Type];idptr[Type]=ty;idptr[HVal]=idptr[Val];idptr[Val]=i++;cc_next();if(token==',')cc_next();}
        cc_next();if(token!='{')return-1;loc_off=++i;cc_next();
        while(token==Int||token==Char){bt=(token==Int)?INT_T:CHAR_T;cc_next();while(token!=';'){ty=bt;while(token==Mul){cc_next();ty+=PTR_T;}if(token!=Id)return-1;if(idptr[Class]==Loc)return-1;idptr[HClass]=idptr[Class];sym_tab[Class]=Loc;idptr[HType]=idptr[Type];idptr[Type]=ty;idptr[HVal]=idptr[Val];sym_tab[Val]=++i;cc_next();if(token==',')cc_next();}cc_next();}*++emit=ENT;*++emit=i-loc_off;while(token!='}')cc_stmt();*++emit=LEV;sym_tab=(int*)mem_sym;while(sym_tab[Tk]){if(sym_tab[Class]==Loc){sym_tab[Class]=sym_tab[HClass];sym_tab[Type]=sym_tab[HType];sym_tab[Val]=sym_tab[HVal];}sym_tab+=Idsz;}
      }else{idptr[Class]=Glo;idptr[Val]=(int)dat_ptr;dat_ptr+=4;}
      if(token==',')cc_next();
    }cc_next();
  }
  pc_main=(int*)idmain[Val];
  return cc_ok;
}

// ── cc_run() — bytecode VM executor ──
int cc_run() {
  int *pc,*sp,*bp,a,cycle=0,*t;
  sp=(int*)((int)mem_stk+PSZ);bp=sp;
  *--sp=EXIT;*--sp=PSH;t=sp;*--sp=0;*--sp=0;*--sp=(int)t;
  pc=pc_main;if(!pc){sput("No program compiled.\n");return-1;}
  while(1){int i=*pc++;++cycle;
    if(i==LEA)a=(int)(bp+*pc++);else if(i==IMM)a=*pc++;else if(i==JMP)pc=(int*)*pc;
    else if(i==JSR){*--sp=(int)(pc+1);pc=(int*)*pc;}else if(i==BZ)pc=a?pc+1:(int*)*pc;
    else if(i==BNZ)pc=a?(int*)*pc:pc+1;else if(i==ENT){*--sp=(int)bp;bp=sp;sp-=*pc++;}
    else if(i==ADJ)sp+=*pc++;else if(i==LEV){sp=bp;bp=(int*)*sp++;pc=(int*)*sp++;}
    else if(i==LI)a=*(int*)a;else if(i==LC)a=*(char*)a;
    else if(i==SI)*(int*)*sp++=a;else if(i==SC)a=*(char*)*sp++=a;
    else if(i==PSH)*--sp=a;else if(i==OR)a=*sp++|a;else if(i==XOR)a=*sp++^a;
    else if(i==AND)a=*sp++&a;else if(i==EQ)a=*sp++==a;else if(i==NE)a=*sp++!=a;
    else if(i==LT)a=*sp++<a;else if(i==GT)a=*sp++>a;else if(i==LE)a=*sp++<=a;
    else if(i==GE)a=*sp++>=a;else if(i==SHL)a=*sp++<<a;else if(i==SHR)a=*sp++>>a;
    else if(i==ADD)a=*sp+++a;else if(i==SUB)a=*sp++-a;else if(i==MUL)a=*sp++*a;
    else if(i==DIV)a=*sp++/a;else if(i==MOD)a=*sp++%a;
    else if(i==PRTF){int n=pc[1];
      for(int j=0;j<n;j++){sputn(sp[j]);if(j<n-1)sput(" ");}
      sp+=n;}
    else if(i==EXIT){sput("exit(");sputn(*sp);sput(") cycles=");sputn(cycle);sput("\n");return*sp;}
    else{sput("bad op ");sputn(i);sput("\n");return-1;}
  }
}
