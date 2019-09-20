#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>
uint16_t memeory[UINT16_MAX];//内存
uint16_t running;
enum { 
    R_0=0,
    R_1,
    R_2,
    R_3,
    R_4,
    R_5,
    R_6,
    R_7,
    R_PC,
    R_cond,
    R_counter
};
uint16_t reg[R_counter];//寄存器

enum{
    OP_BR=0,
    OP_ADD,
    OP_LD,
    OP_ST,
    OP_JSR,
    OP_AND,
    OP_LDR,
    OP_STR,
    OP_RTI,
    OP_NOT,
    OP_LDI,
    OP_STI,
    OP_JMP,
    OP_RES,
    OP_LEA,
    OP_TRAP
};
//condition flags
enum{
    FL_POS=1<<0,
    FL_ZRO=1<<1,
    FL_NEG=1<<2,
};
//trap code
enum{
    TRAP_GETC=0X20,
    TRAP_OUT=0X21,
    TRAP_PUTS=0X22,
    TRAP_IN=0X23,
    TRAP_PUTSP=0X24,
    TRAP_HALT=0X25        
};
//Memory mapped register
enum{
    MR_KBSR=0XFE00,
    MR_KBDR=0XFE02
};
struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}
void memWrite(uint16_t address,uint16_t val){
    memeory[address]=val;
}
uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}
uint16_t memRead(uint16_t address){
     if(address==MR_KBSR){
         if(check_key()){
             memeory[MR_KBSR]=(1<<15);
             memeory[MR_KBDR]=getchar();
         }
         else {
             memeory[MR_KBSR]=0;
         }
     }
     return memeory[address];
}
void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

//获取立即数.
uint16_t signExcend(uint16_t val,uint16_t num){
    if((val>>(num-1)&0x1)){
         val|=(0xFFFF<<num);
    }
     return val;
}
//更新条件寄存器
void updateCond(uint16_t r){
    if(reg[r]==0){
       reg[R_cond]=FL_ZRO;
    }
    else if((reg[r]>>15)&0x1){
       reg[R_cond]=FL_NEG;
    }
    else {
        reg[R_cond]=FL_POS;
    }
}
uint16_t swap16(uint16_t x)
{
    return (x<<8)|(x>>8);
}
void read_image_file(FILE* file){
    uint16_t origin;
    fread(&origin,sizeof(origin),1,file);
    origin=swap16(origin);
    uint16_t maxRead=UINT16_MAX-origin;

    uint16_t* p=memeory+origin;
    size_t read=fread(p,sizeof(uint16_t),maxRead,file);
    while(read-- >0){
        *p=swap16(*p);
        ++p;
    }
}
int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };
    read_image_file(file);
    fclose(file);
    return 1;
}

//Puts
void Puts(){
    uint16_t* c=memeory+reg[R_0];
    while(*c){
        putc((char)*c,stdout);
        ++c;
    }
    fflush(stdout);
}
//GETC
void Getc(){
    reg[R_0]=(uint16_t)getchar();
}
//OUTC
void Out(){
    putc((char)reg[R_0],stdout);
    fflush(stdout);
}
//IN
void In(){
    printf("enter a character: ");
    char c=getchar();
    putc(c,stdout);
    reg[R_0]=(uint16_t)c;
}
//OUTS
void Putsp(){
    uint16_t* c=memeory+reg[R_0];
    while(*c){
        char char1=(*c)&0xFF;
        putc(char1,stdout);
        char char2=(*c)>>8;
        if(char2)putc(char2,stdout);
        ++c;
    }
    fflush(stdout);
}
//HALT
void Halt(){
    puts("HALT");
    fflush(stdout);
    running=0;
}



//add
void Add(uint16_t instr)
{ 
    uint16_t regDr=(instr>>9)&0x7;//获取目的寄存器
    uint16_t addOneReg=(instr>>6)&0x7;//存有第一个加数的寄存器.
    uint16_t addOneVal=reg[addOneReg];//获取第一个加数.
    uint16_t flag=(instr>>5)&0x1;//获取标志位用来判断是不是立即数.
    if(flag){
           uint16_t addSecondVal=signExcend(instr&0x1F,5);
           reg[regDr]=addOneVal+addSecondVal;
    }
    else{
           uint16_t addSecondVal=reg[instr&0x7];
           reg[regDr]=addSecondVal+addOneVal;
    }
    updateCond(regDr);
}
//ldi
void Ldi(uint16_t instr){
    uint16_t regDr=(instr>>9)&0x7;//
    uint16_t offSet=signExcend(instr&0x1FF,5);
    uint16_t addressOne=reg[R_PC]+offSet;
    reg[regDr]=memRead(memRead(addressOne));
    updateCond(regDr);
}
//and
void And(uint16_t instr){
    uint16_t regDr=(instr>>9)&0x7;
    uint16_t SR1=(instr>>6)&0x7;
    uint16_t flag=(instr>>5)&0x1;
    if(flag){
       uint16_t imm5=signExcend(instr&0x1F,5);
       reg[regDr]=imm5&reg[SR1];
    }
    else {
        uint16_t SR2=instr&0x7;
        reg[regDr]=reg[SR1]&reg[SR2];
    }
    updateCond(regDr);
}
//br
void Br(uint16_t instr){
    uint16_t brCode=(instr>>9)&0x7;
    if(brCode&reg[R_cond]){
        reg[R_PC]=reg[R_PC]+signExcend((instr&0x1FF),9);
    }
}
//jump
void Jump(uint16_t instr){
    uint16_t Dr=(instr>>6)&0x7;
    reg[R_PC]=reg[Dr];
}
//JSR
void Jsr(uint16_t instr){
    reg[R_7]=reg[R_PC];
    uint16_t flag=(instr>>11)&0x1;
    if(flag){
        reg[R_PC]=reg[R_PC]+signExcend(instr&0x7FF,11);
    }
    else{
        reg[R_PC]=reg[(instr>>6)&0x7];
    }
}
//LD
void Ld(uint16_t instr){
    uint16_t Dr=(instr>>9)&0x7;
    uint16_t offSet=signExcend(instr&0x1ff,9);
    reg[Dr]=memRead(reg[R_PC]+offSet);
    updateCond(Dr);
}
//LDR
void Ldr(uint16_t instr){
    uint16_t Dr=(instr>>9)&0x7;
    uint16_t BaseR=(instr>>6)&0x7;
    uint16_t offset=signExcend(instr&0x3F,6);
    reg[Dr]=memRead(reg[BaseR]+offset);
    updateCond(Dr);
}
//LEA
void Lea(uint16_t instr){
    uint16_t Dr=(instr>>9)&0x7;
    uint16_t offSet=signExcend(instr&0x1FF,9);
    reg[Dr]=reg[R_PC]+offSet;
    updateCond(Dr);
}
//NOT
void Not(uint16_t instr){
    uint16_t Dr=(instr>>9)&0x7;
    uint16_t Sr=(instr>>6)&0x7;
    reg[Dr]=~reg[Sr];
    updateCond(Dr);
}
//RET
void Ret(uint16_t instr){
    reg[R_PC]=reg[R_7];
}
//ST
void St(uint16_t instr){
    uint16_t SR=(instr>>9)&0x7;
    uint16_t offSet=signExcend(instr&0x1FF,9);
    memWrite(reg[R_PC]+offSet,reg[SR]);
}
//STI
void Sti(uint16_t instr){
    uint16_t SR=(instr>>9)&0x7;
    uint16_t offSet=signExcend(instr&0x1FF,9);
    memWrite(memRead(reg[R_PC]+offSet),reg[SR]);
}
//STR
void Str(uint16_t instr){
    uint16_t SR=(instr>>9)&0x7;
    uint16_t offSet=signExcend(instr&0x3F,6);
    uint16_t BaseR=(instr>>6)&0x7;
    memWrite(reg[BaseR]+offSet,reg[SR]);
}



int main(int argc, const char* argv[])
{
    if(argc<2){
        printf("lc3 [image-file] ...\n");
        exit(2);
    }
    for(int i=1;i<argc;i++){
        if(!read_image(argv[i])){
            printf("failed to load image: %s\n",argv[i]);
            exit(1);
        }
    }
    signal(SIGINT,handle_interrupt);
    disable_input_buffering();

    /* set the PC to starting position */
    /* 0x3000 is the default */
    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    int running = 1;
    while (running)
    {
        /* FETCH */
        uint16_t instr = memRead(reg[R_PC]++);
        //printf("%d\n",instr);
        //printf("%d\n",reg[R_6]);
        uint16_t op = instr >> 12;

        switch (op)
        {
            case OP_ADD:
                Add(instr);
                break;
            case OP_AND:
                And(instr);
                break;
            case OP_NOT:
                Not(instr);
                break;
            case OP_BR:
                Br(instr);
                break;
            case OP_JMP:
                Jump(instr);
                break;
            case OP_JSR:
                Jsr(instr);
                break;
            case OP_LD:
                Ld(instr);
                break;
            case OP_LDI:
                Ldi(instr);
                break;
            case OP_LDR:
                Ldr(instr);
                break;
            case OP_LEA:
                Lea(instr);
                break;
            case OP_ST:
                St(instr);
                break;
            case OP_STI:
                Sti(instr);
                break;
            case OP_STR:
                Str(instr);
                break;
            case OP_TRAP:
                switch (instr&0xFF)
                {
                case TRAP_GETC:
                    Getc();
                    break;
                case TRAP_OUT:
                    Out();
                    break;
                case TRAP_PUTS:
                     Puts();
                     break;
                case TRAP_IN:
                     In();
                     break;
                case TRAP_PUTSP:
                     Putsp();
                     break;
                case TRAP_HALT:
                     Halt();
                     break;
                default:
                    break;
                }
                break;
            case OP_RES:
            case OP_RTI:
            default:
                
                break;
        }
    }
    restore_input_buffering();
}