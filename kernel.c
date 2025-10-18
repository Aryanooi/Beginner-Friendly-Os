#include "kernel.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

UINT16* TERMINAL_BUFFER = (UINT16*)VGA_ADDRESS;
unsigned int VGA_INDEX = 0;
static int Y_INDEX = 0;

// ======= VGA helpers =======
static UINT16 vgaEntry(unsigned char ch) {
    return (UINT16)ch | ((UINT16)WHITE_COLOR << 8);
}

static void scrollIfNeeded(void) {
    if(Y_INDEX >= VGA_HEIGHT) {
        // move lines up
        for(int y=1;y<VGA_HEIGHT;y++){
            for(int x=0;x<VGA_WIDTH;x++){
                TERMINAL_BUFFER[(y-1)*VGA_WIDTH + x] = TERMINAL_BUFFER[y*VGA_WIDTH + x];
            }
        }
        // clear last line
        for(int x=0;x<VGA_WIDTH;x++){
            TERMINAL_BUFFER[(VGA_HEIGHT-1)*VGA_WIDTH + x] = vgaEntry(' ');
        }
        Y_INDEX = VGA_HEIGHT-1;
    }
    VGA_INDEX = Y_INDEX*VGA_WIDTH;
}

static void clearScreen(void) {
    for(int i=0;i<VGA_WIDTH*VGA_HEIGHT;i++){
        TERMINAL_BUFFER[i] = vgaEntry(' ');
    }
    VGA_INDEX = 0;
    Y_INDEX = 0;
}

static void printChar(char c) {
    if(c=='\n'){
        Y_INDEX++;
        scrollIfNeeded();
        return;
    }
    TERMINAL_BUFFER[VGA_INDEX++] = vgaEntry(c);
    if(VGA_INDEX % VGA_WIDTH == 0){
        Y_INDEX++;
        scrollIfNeeded();
    }
}

static void printString(const char* str) {
    for(int i=0;str[i];i++) printChar(str[i]);
}

static void printLine(const char* s){
    printString(s);
    printChar('\n');
}

static void backspace(void){
    if(VGA_INDEX>0){
        VGA_INDEX--;
        TERMINAL_BUFFER[VGA_INDEX]=vgaEntry(' ');
    }
}

// ======= Minimal Text Editor =======
// In-memory single-buffer editor with basic keys: chars, Enter, Backspace, ESC to exit.
// No file I/O; shows buffer and status line. Supports about one screen of text.
// Forward declarations for keyboard helpers used below
static int read_scancode_nonblock(unsigned char* sc);
static int is_release(unsigned char sc);
static char scancode_to_ascii(unsigned char sc);

static void run_editor(void){
    clearScreen();
    printLine("Mini Text Editor (ESC to menu)");
    printLine("");
    // Fixed-size buffer
    char buf[1024];
    int len = 0;
    // Simple rendering: print buffer followed by a status prompt line
    for(;;){
        unsigned char sc;
        if(!read_scancode_nonblock(&sc)) continue;
        if(is_release(sc)) continue;
        if(sc==0x01){ // ESC
            return;
        }
        if(sc==0x0E){ // Backspace
            if(len>0){
                len--; backspace();
            }
            continue;
        }
        if(sc==0x1C){ // Enter
            if(len < (int)sizeof(buf)-1){
                buf[len++]='\n';
                printChar('\n');
            }
            continue;
        }
        char c = scancode_to_ascii(sc);
        if(c){
            if(len < (int)sizeof(buf)-1){
                buf[len++] = c;
                printChar(c);
            }
        }
    }
}

// ======= Keyboard helpers =======
static inline unsigned char inb(unsigned short port){
    unsigned char ret;
    __asm__ volatile("inb %1,%0":"=a"(ret):"Nd"(port));
    return ret;
}

static int read_scancode_nonblock(unsigned char* sc){
    if(inb(0x64)&1){
        *sc = inb(0x60);
        return 1;
    }
    return 0;
}

static int is_release(unsigned char sc){ return (sc&0x80)!=0; }

static char scancode_to_ascii(unsigned char sc){
    switch(sc){
        case 0x0C: return '-'; // main row '-'
        case 0x0D: return '+'; // treat '=' key as '+' to avoid shift handling
        case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3';
        case 0x05: return '4'; case 0x06: return '5'; case 0x07: return '6';
        case 0x08: return '7'; case 0x09: return '8'; case 0x0A: return '9';
        case 0x0B: return '0'; case 0x10: return 'q'; case 0x11: return 'w';
        case 0x12: return 'e'; case 0x13: return 'r'; case 0x14: return 't';
        case 0x15: return 'y'; case 0x16: return 'u'; case 0x17: return 'i';
        case 0x18: return 'o'; case 0x19: return 'p'; case 0x1E: return 'a';
        case 0x1F: return 's'; case 0x20: return 'd'; case 0x21: return 'f';
        case 0x22: return 'g'; case 0x23: return 'h'; case 0x24: return 'j';
        case 0x25: return 'k'; case 0x26: return 'l'; case 0x2C: return 'z';
        case 0x2D: return 'x'; case 0x2E: return 'c'; case 0x2F: return 'v';
        case 0x30: return 'b'; case 0x31: return 'n'; case 0x32: return 'm';
        case 0x39: return ' ';
        case 0x35: return '/'; case 0x4A: return '-'; case 0x4C: return '+'; // keypad variants
        case 0x37: return '*';
        default: return 0;
    }
}

// ======= String helpers =======
static int to_int(const char* s,int* out){
    int sign=1,i=0; long val=0;
    if(s[0]=='-'){sign=-1;i=1;}
    if(s[i]=='\0') return 0;
    for(;s[i];i++){
        if(s[i]<'0'||s[i]>'9') return 0;
        val=val*10+(s[i]-'0');
    }
    *out=(int)(sign*val);
    return 1;
}

static void itoa10(int v,char* buf){
    char tmp[16]; int n=0,neg=0;
    if(v==0){buf[0]='0';buf[1]='\0';return;}
    if(v<0){neg=1;v=-v;}
    while(v>0 && n<16){tmp[n++]='0'+(v%10); v/=10;}
    int i=0; if(neg) buf[i++]='-';
    while(n--) buf[i++]=tmp[n];
    buf[i]='\0';
}

// ======= Calculator =======
static void run_calculator(void){
    clearScreen();
    printLine("Calculator (ESC to menu)");
    printLine("Format: <int><op><int> Example: 12+34");
    printLine("Ops: + - * /");
    printLine("");
    char buf[64]; int len=0;
    printString("> ");
    for(;;){
        unsigned char sc;
        if(!read_scancode_nonblock(&sc)) continue;
        if(is_release(sc)) continue;
        if(sc==0x01) return; // ESC
        if(sc==0x0E){if(len>0){len--;backspace();} continue;} // Backspace
        if(sc==0x1C){
            buf[len]='\0';
            int i=0; while(buf[i]==' ') i++;
            int startA=i;
            // optional leading '-' for A
            if(buf[i]=='-') i++;
            int digitsA=0; while(buf[i]>='0' && buf[i]<='9'){ i++; digitsA++; }
            int endA=i;
            int opPos=i; // operator position
            char op=buf[i];
            if(op) i++; // move past operator if any
            while(buf[i]==' ') i++;
            int startB=i;
            // optional leading '-' for B
            if(buf[i]=='-') i++;
            int digitsB=0; while(buf[i]>='0' && buf[i]<='9'){ i++; digitsB++; }
            int endB=i;
            // Prepare safe C-strings for A and B
            char savedA=buf[endA]; buf[endA]='\0';
            char savedB=buf[endB]; buf[endB]='\0';
            int a,b; int okA=(digitsA>0)&&to_int(&buf[startA],&a);
            int okB=(digitsB>0)&&to_int(&buf[startB],&b);
            // restore
            buf[endA]=savedA; buf[endB]=savedB;
            char res[32];
            if(!okA||!okB||(op!='+'&&op!='-'&&op!='*'&&op!='/')){
                printLine("");
                printLine("Error: parse");
            }else{
                int r=0,err=0;
                if(op=='+') r=a+b;
                else if(op=='-') r=a-b;
                else if(op=='*') r=a*b;
                else if(op=='/'){if(b==0) err=1; else r=a/b;}
                printLine("");
                if(err) printLine("Error: div by 0");
                else {itoa10(r,res); printString("= "); printLine(res);}
            }
            printLine(""); printString("> "); len=0; continue;
        }
        char c=scancode_to_ascii(sc);
        if(c){if(len<63){buf[len++]=c; printChar(c);}}
    }
}

// ======= Main Menu =======
static void show_menu(void){
    clearScreen();
    printLine("Welcome to MiniOS");
    printLine("");
    printLine("Menu:");
    printLine("  [C] Calculator");
    printLine("  [E] Text Editor");
    printLine("  [Esc] Halt");
    printLine("");
    printLine("Press a key...");
}

void KERNEL_MAIN(void){
    TERMINAL_BUFFER=(UINT16*)VGA_ADDRESS;
    VGA_INDEX=0; Y_INDEX=0;

    show_menu();

    for(;;){
        unsigned char sc;
        if(!read_scancode_nonblock(&sc)) continue;
        if(is_release(sc)) continue;

        if(sc==0x01) break; // ESC = halt

        char ch=scancode_to_ascii(sc);
        if(ch=='c'||ch=='C'){
            run_calculator();
            show_menu();
        }
        if(ch=='e'||ch=='E'){
            run_editor();
            show_menu();
        }
    }
}
