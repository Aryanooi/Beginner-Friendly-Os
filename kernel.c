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

// ======= Simple GUI helpers (text-mode) =======
static void setCursor(int row,int col){
    if(row<0) row=0; if(col<0) col=0;
    if(row>=VGA_HEIGHT) row=VGA_HEIGHT-1;
    if(col>=VGA_WIDTH) col=VGA_WIDTH-1;
    Y_INDEX = row;
    VGA_INDEX = row*VGA_WIDTH + col;
}

static void putCharAt(int row,int col,char c){
    if(row<0||row>=VGA_HEIGHT||col<0||col>=VGA_WIDTH) return;
    TERMINAL_BUFFER[row*VGA_WIDTH + col] = vgaEntry(c);
}

static void writeAt(int row,int col,const char* s){
    for(int i=0;s[i] && (col+i)<VGA_WIDTH;i++){
        putCharAt(row,col+i,s[i]);
    }
}

static void fillAt(int row,int col,int len,char ch){
    for(int i=0;i<len && (col+i)<VGA_WIDTH;i++){
        putCharAt(row,col+i,ch);
    }
}

static void drawBox(int top,int left,int bottom,int right,const char* title){
    if(top<0) top=0; if(left<0) left=0;
    if(bottom>=VGA_HEIGHT) bottom=VGA_HEIGHT-1;
    if(right>=VGA_WIDTH) right=VGA_WIDTH-1;
    for(int c=left;c<=right;c++){ putCharAt(top,c,'-'); putCharAt(bottom,c,'-'); }
    for(int r=top;r<=bottom;r++){ putCharAt(r,left,'|'); putCharAt(r,right,'|'); }
    putCharAt(top,left,'+'); putCharAt(top,right,'+');
    putCharAt(bottom,left,'+'); putCharAt(bottom,right,'+');
    if(title){
        int tlen=0; while(title[tlen]) tlen++;
        int pos = left+2;
        for(int i=0;i<tlen && pos+i<right;i++){
            putCharAt(top,pos+i,title[i]);
        }
    }
}

static void drawButton(int row,int col,const char* label,int width,int selected){
    // simple button: [ label ] with optional highlight using angle brackets
    char left = selected ? '<' : '[';
    char right = selected ? '>' : ']';
    putCharAt(row,col,left);
    int lablen=0; while(label[lablen]) lablen++;
    int pad = width-2;
    int start = col+1;
    // center label
    int leftpad = (pad - lablen)/2; if(leftpad<0) leftpad=0;
    int i=0;
    for(int p=0;p<pad;p++){
        char ch=' ';
        if(p>=leftpad && i<lablen){ ch=label[i++]; }
        putCharAt(row,start+p,ch);
    }
    putCharAt(row,col+width-1,right);
}

// ======= In-memory file store (volatile) =======
typedef struct {
    char name[16];
    int length;
    char data[1024];
    int used;
} FileEntry;

static FileEntry g_files[4];

static int memfs_find(const char* name){
    for(int i=0;i<4;i++){
        if(g_files[i].used){
            int j=0; int ok=1;
            for(; j<15 && name[j] && g_files[i].name[j]; j++){
                if(name[j]!=g_files[i].name[j]){ ok=0; break; }
            }
            if(ok && name[j]=='\0' && g_files[i].name[j]=='\0') return i;
        }
    }
    return -1;
}

static int memfs_save(const char* name,const char* buf,int len){
    if(len<0) len=0; if(len>1024) len=1024;
    int idx = memfs_find(name);
    if(idx<0){
        for(int i=0;i<4;i++){ if(!g_files[i].used){ idx=i; break; } }
        if(idx<0) return 0;
        for(int i=0;i<15;i++){ g_files[idx].name[i]=name[i]; if(name[i]=='\0'){ break; } }
        g_files[idx].name[15]='\0';
        g_files[idx].used=1;
    }
    for(int i=0;i<len;i++) g_files[idx].data[i]=buf[i];
    g_files[idx].length=len;
    return 1;
}

static int memfs_load(const char* name,char* out,int* outLen){
    int idx = memfs_find(name);
    if(idx<0) return 0;
    int len = g_files[idx].length;
    for(int i=0;i<len;i++) out[i]=g_files[idx].data[i];
    if(outLen) *outLen=len;
    return 1;
}

// ======= Minimal Text Editor =======
// In-memory single-buffer editor with basic keys: chars, Enter, Backspace, ESC to exit.
// No file I/O; shows buffer and status line. Supports about one screen of text.
// Forward declarations for keyboard helpers used below
static int read_scancode_nonblock(unsigned char* sc);
static int is_release(unsigned char sc);
static char scancode_to_ascii(unsigned char sc);

// Read a short ASCII line at a fixed screen row/col (uses our scancode map)
static int read_line_gui(int row,int col,char* out,int maxlen){
    int len=0;
    for(;;){
        unsigned char sc;
        if(!read_scancode_nonblock(&sc)) continue;
        if(is_release(sc)) continue;
        if(sc==0x01){ // ESC cancel
            return 0;
        }
        if(sc==0x0E){ // backspace
            if(len>0){
                len--;
                putCharAt(row,col+len,' ');
            }
            continue;
        }
        if(sc==0x1C){ // Enter
            out[len]='\0';
            return 1;
        }
        char c=scancode_to_ascii(sc);
        if(c && len<maxlen-1){
            out[len++]=c;
            putCharAt(row,col+len-1,c);
        }
    }
}

static void run_editor(void){
    clearScreen();
    drawBox(0,0,24,79," Editor ");
    writeAt(24,2,"ESC:Menu  F2:Save  F3:Open");
    // Text area inside box from row 2..22, col 2..77
    char buf[1024];
    int len = 0;
    int row=2,col=2;
    setCursor(row,col);
    for(;;){
        unsigned char sc;
        if(!read_scancode_nonblock(&sc)) continue;
        if(is_release(sc)) continue;
        if(sc==0x01){ // ESC
            return;
        }
        if(sc==0x3C){ // F2 Save (make code)
            // Prompt filename
            writeAt(1,2,"Save as:             ");
            fillAt(1,11,16,' ');
            char name[16];
            read_line_gui(1,11,name,16);
            // Clear message line
            fillAt(1,2,76,' ');
            if(name[0]){
                if(memfs_save(name,buf,len)){
                    writeAt(1,2,"Saved.");
                }else{
                    writeAt(1,2,"Save failed (store full).");
                }
            }else{
                writeAt(1,2,"Cancelled.");
            }
            continue;
        }
        if(sc==0x3D){ // F3 Open
            writeAt(1,2,"Open:                ");
            fillAt(1,8,16,' ');
            char name[16];
            if(read_line_gui(1,8,name,16)){
                int nlen=0;
                if(memfs_load(name,buf,&nlen)){
                    // Clear text area
                    for(int r=2;r<=22;r++) fillAt(r,2,76,' ');
                    len=nlen;
                    row=2; col=2;
                    int i=0;
                    for(; i<len && row<=22;i++){
                        char c=buf[i];
                        if(c=='\n'){ row++; col=2; continue; }
                        putCharAt(row,col,c);
                        col++; if(col>77){ row++; col=2; }
                    }
                    setCursor(row,col);
                    writeAt(1,2,"Opened.");
                }else{
                    writeAt(1,2,"Not found.");
                }
            }
            continue;
        }
        if(sc==0x0E){ // Backspace
            if(len>0){
                // remove last char from buffer until a visible char is removed
                char last = buf[len-1];
                len--;
                if(last=='\n'){
                    // move cursor up to previous line end
                    if(row>2){
                        row--;
                        // recompute col by scanning previous line from start
                        int ccol=2; int rr=2; int i=0;
                        while(i<len && rr<row){
                            if(buf[i++]=='\n') rr++;
                        }
                        while(i<len && buf[i]!='\n' && ccol<=77){ i++; ccol++; }
                        col=ccol;
                    }
                }else{
                    if(col>2){ col--; putCharAt(row,col,' '); }
                    else if(row>2){ row--; col=77; putCharAt(row,col,' '); }
                }
                setCursor(row,col);
            }
            continue;
        }
        if(sc==0x1C){ // Enter
            if(len < (int)sizeof(buf)-1 && row<22){
                buf[len++]='\n';
                row++; col=2;
                setCursor(row,col);
            }
            continue;
        }
        char c = scancode_to_ascii(sc);
        if(c){
            if(len < (int)sizeof(buf)-1 && row<=22){
                buf[len++] = c;
                putCharAt(row,col,c);
                if(col<77){ col++; } else { row++; col=2; }
                setCursor(row,col);
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
    drawBox(0,0,24,79," Calculator ");
    // Display box
    drawBox(1,2,5,77," Display ");
    writeAt(3,4,"Input: ");
    writeAt(4,4,"Result: ");
    // Help
    writeAt(6,4,"Type or use keypad. W/A/S/D move, Enter/Space press. ESC:Menu, 'c':clear");
    writeAt(7,4,"Example: 12+34");
    char buf[64]; int len=0;
    char lastRes[32]; lastRes[0]='\0';

    // Unified 4x4 keypad including operators, CLR and ENT
    const char* keys[4][4]={{"7","8","9","/"},
                            {"4","5","6","*"},
                            {"1","2","3","-"},
                            {"0","+","BKSP","ENT"}};
    int selR=0, selC=0; // selection (rows 0..3, cols 0..3)

    // draw keypad box
    drawBox(9,10,21,69," Keypad ");
    // render function
    void render(void){
        // refresh display Input/Result
        fillAt(3,11,66,' ');
        for(int i=0;i<len;i++) putCharAt(3,11+i,buf[i]);
        fillAt(4,12,65,' ');
        for(int i=0; lastRes[i] && i<60; i++) putCharAt(4,12+i,lastRes[i]);
        // draw keypad 4x4
        int baseR=11, baseC=14;
        for(int r=0;r<4;r++){
            for(int c=0;c<4;c++){
                int sel = (selR==r && selC==c);
                int isWide = (keys[r][c][0]=='B' || keys[r][c][0]=='E');
                int w = isWide ? 7 : 5;
                int step = isWide ? 9 : 7;
                drawButton(baseR + r*2, baseC + c*step, keys[r][c], w, sel);
            }
        }
    } render();

    for(;;){
        unsigned char sc;
        if(!read_scancode_nonblock(&sc)) continue;
        if(is_release(sc)) continue;
        if(sc==0x01) return; // ESC
        if(sc==0x0E){ // Backspace
            if(len>0){
                len--;
                putCharAt(3,11+len,' ');
            }
            render();
            continue;
        }

        // Navigation with WASD
        char nav=scancode_to_ascii(sc);
        if(nav=='a' || nav=='d' || nav=='w' || nav=='s'){
            if(nav=='a' && selC>0) selC--;
            if(nav=='d' && selC<3) selC++;
            if(nav=='w' && selR>0) selR--;
            if(nav=='s' && selR<3) selR++;
            render();
            continue;
        }

        // Activate selection with Enter/Space
        if(sc==0x1C || sc==0x39){
            const char* lab = keys[selR][selC];
            if(lab[0]=='B'){ // BKSP
                if(len>0){ len--; putCharAt(3,11+len,' '); }
                render();
                continue;
            }
            if(lab[0]=='E'){ // ENT
                // evaluate expression
                buf[len]='\0';
                int i=0; while(buf[i]==' ') i++;
                int startA=i;
                if(buf[i]=='-') i++;
                int digitsA=0; while(buf[i]>='0' && buf[i]<='9'){ i++; digitsA++; }
                int endA=i;
                char op=buf[i];
                if(op) i++;
                while(buf[i]==' ') i++;
                int startB=i;
                if(buf[i]=='-') i++;
                int digitsB=0; while(buf[i]>='0' && buf[i]<='9'){ i++; digitsB++; }
                int endB=i;
                char savedA=buf[endA]; buf[endA]='\0';
                char savedB=buf[endB]; buf[endB]='\0';
                int a,b; int okA=(digitsA>0)&&to_int(&buf[startA],&a);
                int okB=(digitsB>0)&&to_int(&buf[startB],&b);
                buf[endA]=savedA; buf[endB]=savedB;
                char res[32];
                if(!okA||!okB||(op!='+'&&op!='-'&&op!='*'&&op!='/')){
                    lastRes[0]='\0';
                    writeAt(4,12,"Error: parse");
                }else{
                    int r=0,err=0;
                    if(op=='+') r=a+b;
                    else if(op=='-') r=a-b;
                    else if(op=='*') r=a*b;
                    else if(op=='/'){if(b==0) err=1; else r=a/b;}
                    if(err){
                        lastRes[0]='\0';
                        writeAt(4,12,"Error: div by 0");
                    } else {
                        itoa10(r,res);
                        // store and show result
                        int k=0; while(res[k] && k<31){ lastRes[k]=res[k]; k++; } lastRes[k]='\0';
                        fillAt(4,12,65,' ');
                        writeAt(4,12,res);
                    }
                }
                fillAt(3,11,60,' ');
                len=0;
                render();
                continue;
            }
            // Otherwise it's a single char button: digit or operator
            if(len<63){
                buf[len++]=keys[selR][selC][0];
            }
            render();
            continue;
        }

        if(sc==0x1C){
            buf[len]='\0';
            int i=0; while(buf[i]==' ') i++;
            int startA=i;
            // optional leading '-' for A
            if(buf[i]=='-') i++;
            int digitsA=0; while(buf[i]>='0' && buf[i]<='9'){ i++; digitsA++; }
            int endA=i;
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
            // Clear result area
            fillAt(4,12,65,' ');
            if(!okA||!okB||(op!='+'&&op!='-'&&op!='*'&&op!='/')){
                writeAt(4,12,"Error: parse");
            }else{
                int r=0,err=0;
                if(op=='+') r=a+b;
                else if(op=='-') r=a-b;
                else if(op=='*') r=a*b;
                else if(op=='/'){if(b==0) err=1; else r=a/b;}
                if(err) writeAt(4,12,"Error: div by 0");
                else {itoa10(r,res); writeAt(4,12,res);}
            }
            // Reset input line
            fillAt(3,11,60,' ');
            len=0; continue;
        }
        char c=scancode_to_ascii(sc);
        if(c){
            if(c=='c'){ // quick clear
                fillAt(3,11,60,' ');
                len=0; render(); continue;
            }
            if(len<63){
                buf[len++]=c;
                putCharAt(3,11+len-1,c);
            }
            render();
        }
    }
}

// ======= Word Guessing Game (Hangman-like, without graphics) =======
static void run_word_game(void){
    static int word_index=0;
    const char* words[]={
        "hello","world","friend","family","home","coffee","water","phone","music","movie",
        "school","work","pizza","bread","happy","sad","love","time","today","night",
        "morning","evening","summer","winter","spring","rain","sun","cloud","car","bus",
        "train","apple","banana","orange","grape","milk","tea","sugar","chair","table",
        "window","door","river","mountain","city","street","house","garden","computer",
        "keyboard","mouse","screen","light","dark","smile","sleep","dream","game","play"
    };
    int num_words = sizeof(words)/sizeof(words[0]);

    for(;;){ // outer loop to allow "Next word" without exiting
        const char* secret = words[word_index % num_words];
        word_index++;

        // state
        char guessed[26]; int gcount=0;
        int attempts_left=6;

        clearScreen();
        drawBox(0,0,24,79," Word Guess ");
        writeAt(2,4,"Guess letters (a-z). H: next, V: vowel (-1). R: retry on loss. ESC:Menu");
        writeAt(3,4,"Length: ");
        // print length
        int slen=0; while(secret[slen]) slen++;
        char d1='0'+(slen/10); char d2='0'+(slen%10);
        if(slen>=10){ putCharAt(3,12,d1); putCharAt(3,13,d2); } else { putCharAt(3,12,d2); }

        int in_guessed(char ch){
            for(int i=0;i<gcount;i++) if(guessed[i]==ch) return 1;
            return 0;
        }
        int all_revealed(void){
            for(int i=0;secret[i];i++){
                char ch=secret[i];
                int found=0;
                for(int j=0;j<gcount;j++) if(guessed[j]==ch){ found=1; break; }
                if(!found) return 0;
            }
            return 1;
        }
        void render(void){
            // masked word
            fillAt(4,4,70,' ');
            int col=4;
            for(int i=0;secret[i];i++){
                char ch=secret[i];
                int show=in_guessed(ch);
                putCharAt(4,col, show? ch : '_'); col+=2;
            }
            // attempts
            fillAt(6,4,30,' ');
            writeAt(6,4,"Attempts left: ");
            int t=attempts_left; if(t<0)t=0;
            if(t>=10){ putCharAt(6,19,'1'); putCharAt(6,20,'0'); } else { putCharAt(6,19,'0'+t); }
            // guessed letters
            fillAt(8,4,70,' ');
            writeAt(8,4,"Guessed: ");
            for(int i=0;i<gcount;i++){ putCharAt(8,14+i*2,guessed[i]); }
        } render();

        for(;;){
            if(all_revealed()){
                writeAt(10,4,"You win! N: Next word, ESC: Exit.");
            }
            if(attempts_left==0 && !all_revealed()){
                writeAt(10,4,"You lose! R: Retry, ESC: Exit.");
                // Do not reveal the secret word
                fillAt(12,4,72,' ');
            }
            unsigned char sc;
            if(!read_scancode_nonblock(&sc)) continue;
            if(is_release(sc)) continue;
            if(sc==0x01) return; // ESC
            char c=scancode_to_ascii(sc);
            // Retry same word after loss
            if((c=='r'||c=='R') && attempts_left==0 && !all_revealed()){
                gcount=0;
                attempts_left=6;
                // clear message lines
                fillAt(10,4,72,' ');
                fillAt(12,4,72,' ');
                render();
                continue;
            }
            // Next word if already won
            if((c=='n'||c=='N') && all_revealed()){
                break; // break inner loop, outer loop continues to next word
            }
            // Hint: reveal next unrevealed letter (left-to-right), cost 1 attempt
            if(c=='h' && attempts_left>0 && !all_revealed()){
                for(int i=0;secret[i];i++){
                    char ch=secret[i];
                    if(!in_guessed(ch)){
                        guessed[gcount++]=ch;
                        attempts_left--;
                        break;
                    }
                }
                render();
                continue;
            }
            // Vowel hint
            if(c=='v' && attempts_left>0 && !all_revealed()){
                const char* vowels="aeiou";
                int revealed=0;
                for(int i=0;vowels[i] && !revealed;i++){
                    char vw=vowels[i];
                    for(int j=0;secret[j];j++){
                        if(secret[j]==vw && !in_guessed(vw)){
                            guessed[gcount++]=vw;
                            attempts_left--;
                            revealed=1;
                            break;
                        }
                    }
                }
                // If no vowel left, fall back to next unrevealed
                if(!revealed){
                    for(int i=0;secret[i];i++){
                        char ch=secret[i];
                        if(!in_guessed(ch)){
                            guessed[gcount++]=ch;
                            attempts_left--;
                            break;
                        }
                    }
                }
                render();
                continue;
            }
            if(c>='a'&&c<='z' && attempts_left>0 && !all_revealed()){
                if(!in_guessed(c)){
                    guessed[gcount++]=c;
                    // if guess not in secret, consume attempt
                    int hit=0; for(int i=0;secret[i];i++) if(secret[i]==c){ hit=1; break; }
                    if(!hit) attempts_left--;
                }
                render();
            }
        } // end inner loop
    } // continue with next word
}

// ======= Main Menu =======
static void show_menu(void){
    clearScreen();
    drawBox(5,10,19,69," MiniOS ");
    writeAt(7,14,"Welcome to MiniOS");
    writeAt(10,14,"[C] Calculator");
    writeAt(12,14,"[E] Text Editor");
    writeAt(14,14,"[G] Word Guess");
    writeAt(16,14,"[Esc] Halt");
    writeAt(17,14,"Press a key...");
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
        if(ch=='g'||ch=='G'){
            run_word_game();
            show_menu();
        }
    }
}
