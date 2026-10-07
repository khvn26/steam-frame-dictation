#include "app.h"
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int emit_ev(int fd, uint16_t type, uint16_t code, int32_t value) {
    struct input_event ev; memset(&ev,0,sizeof(ev)); ev.type=type; ev.code=code; ev.value=value;
    return write(fd,&ev,sizeof(ev)) == sizeof(ev) ? 0 : -1;
}
static void key_ev(int fd, int code, bool down) { emit_ev(fd,EV_KEY,code,down?1:0); emit_ev(fd,EV_SYN,SYN_REPORT,0); usleep(8000); }
static void tap(int fd, int code, bool shift) { if(shift) key_ev(fd,KEY_LEFTSHIFT,true); key_ev(fd,code,true); key_ev(fd,code,false); if(shift) key_ev(fd,KEY_LEFTSHIFT,false); }

typedef struct { char ch; int key; bool shift; } keymap;
static keymap maps[] = {
 {'a',KEY_A,false},{'b',KEY_B,false},{'c',KEY_C,false},{'d',KEY_D,false},{'e',KEY_E,false},{'f',KEY_F,false},{'g',KEY_G,false},{'h',KEY_H,false},{'i',KEY_I,false},{'j',KEY_J,false},{'k',KEY_K,false},{'l',KEY_L,false},{'m',KEY_M,false},{'n',KEY_N,false},{'o',KEY_O,false},{'p',KEY_P,false},{'q',KEY_Q,false},{'r',KEY_R,false},{'s',KEY_S,false},{'t',KEY_T,false},{'u',KEY_U,false},{'v',KEY_V,false},{'w',KEY_W,false},{'x',KEY_X,false},{'y',KEY_Y,false},{'z',KEY_Z,false},
 {'A',KEY_A,true},{'B',KEY_B,true},{'C',KEY_C,true},{'D',KEY_D,true},{'E',KEY_E,true},{'F',KEY_F,true},{'G',KEY_G,true},{'H',KEY_H,true},{'I',KEY_I,true},{'J',KEY_J,true},{'K',KEY_K,true},{'L',KEY_L,true},{'M',KEY_M,true},{'N',KEY_N,true},{'O',KEY_O,true},{'P',KEY_P,true},{'Q',KEY_Q,true},{'R',KEY_R,true},{'S',KEY_S,true},{'T',KEY_T,true},{'U',KEY_U,true},{'V',KEY_V,true},{'W',KEY_W,true},{'X',KEY_X,true},{'Y',KEY_Y,true},{'Z',KEY_Z,true},
 {'1',KEY_1,false},{'2',KEY_2,false},{'3',KEY_3,false},{'4',KEY_4,false},{'5',KEY_5,false},{'6',KEY_6,false},{'7',KEY_7,false},{'8',KEY_8,false},{'9',KEY_9,false},{'0',KEY_0,false},
 {'!',KEY_1,true},{'@',KEY_2,true},{'#',KEY_3,true},{'$',KEY_4,true},{'%',KEY_5,true},{'^',KEY_6,true},{'&',KEY_7,true},{'*',KEY_8,true},{'(',KEY_9,true},{')',KEY_0,true},
 {' ',KEY_SPACE,false},{'\n',KEY_ENTER,false},{'-',KEY_MINUS,false},{'_',KEY_MINUS,true},{'=',KEY_EQUAL,false},{'+',KEY_EQUAL,true},{'[',KEY_LEFTBRACE,false},{'{',KEY_LEFTBRACE,true},{']',KEY_RIGHTBRACE,false},{'}',KEY_RIGHTBRACE,true},{'\\',KEY_BACKSLASH,false},{'|',KEY_BACKSLASH,true},{';',KEY_SEMICOLON,false},{':',KEY_SEMICOLON,true},{'\'',KEY_APOSTROPHE,false},{'"',KEY_APOSTROPHE,true},{'`',KEY_GRAVE,false},{'~',KEY_GRAVE,true},{',',KEY_COMMA,false},{'<',KEY_COMMA,true},{'.',KEY_DOT,false},{'>',KEY_DOT,true},{'/',KEY_SLASH,false},{'?',KEY_SLASH,true}
};
void type_text(int fd, const char *s) {
    for(const unsigned char *p=(const unsigned char*)s; *p; p++) {
        bool found=false;
        for(size_t i=0;i<sizeof(maps)/sizeof(maps[0]);i++) if(maps[i].ch==(char)*p) { tap(fd,maps[i].key,maps[i].shift); found=true; break; }
        if(!found) fprintf(stderr,"skip char 0x%02x\n", *p);
    }
}
void *type_thread_main(void *vp) {
    app *a=vp; char *text=NULL;
    while(textq_pop(&a->textq, &text)) {
        fprintf(stderr,"type: '%s'\n", text);
        if(!a->dry_run && a->ufd>=0) type_text(a->ufd, text);
        free(text);
    }
    return NULL;
}
int setup_uinput(const char *path) {
    int fd=open(path,O_WRONLY|O_NONBLOCK); if(fd<0) return -1;
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    int keys[] = {KEY_LEFTSHIFT,KEY_SPACE,KEY_ENTER,KEY_MINUS,KEY_EQUAL,KEY_LEFTBRACE,KEY_RIGHTBRACE,KEY_BACKSLASH,KEY_SEMICOLON,KEY_APOSTROPHE,KEY_GRAVE,KEY_COMMA,KEY_DOT,KEY_SLASH,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9,KEY_0,KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
    for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    struct uinput_setup us; memset(&us,0,sizeof(us)); snprintf(us.name,sizeof(us.name),"frame-dictate-native"); us.id.bustype=BUS_USB; us.id.vendor=0x28de; us.id.product=0xd1c7; us.id.version=1;
    if(ioctl(fd, UI_DEV_SETUP, &us)<0 || ioctl(fd, UI_DEV_CREATE)<0) { close(fd); return -1; }
    msleep(200); return fd;
}
