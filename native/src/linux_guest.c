/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
#include "auth.h"
#include "guest.h"
#include "linux_guest.h"
extern const unsigned char linux_image_start[],linux_image_end[],linux_dtb_start[],linux_dtb_end[];
static struct guest vm;
static uint8_t *ram;
static bool running, paused;
static uint32_t owner;
static char screen[128][81];
static unsigned line_count=1,column,escape;
static unsigned char keys[256];
static unsigned key_read,key_write;
static const char *state="Stopped / temporary Linux guest";
static void newline(void) {
    if(line_count<128)line_count++;
    else {for(unsigned i=1;i<128;i++)memcpy(screen[i-1],screen[i],81);}
    memset(screen[line_count-1],0,81);column=0;
}
static void output(void *unused,unsigned char c) {
    (void)unused;
    char text[2]={(char)c,0};log_write(text);
    /* TERM=dumb guest. Strip control sequences, never interpret guest text as
     * host commands or feed it to the account/installer input path. */
    if(escape){if(c>=0x40&&c<=0x7e&&c!='[')escape=0;return;}
    if(c==27){escape=1;return;}
    if(c=='\n'){newline();return;}
    if(c=='\r'){column=0;return;}
    if(c=='\b'||c==127){if(column)screen[line_count-1][--column]=0;return;}
    if(c=='\t'){do {output(NULL,' ');}while(column%8);return;}
    if(c<32||c>126)return;
    if(column>=80)newline();
    screen[line_count-1][column++]=(char)c;
}
static int input(void *unused) {
    (void)unused;if(key_read==key_write)return -1;
    unsigned char c=keys[key_read];key_read=(key_read+1)%sizeof keys;return c;
}
void linux_guest_key(unsigned char c) {
    if(!running||paused||!auth_logged_in()||owner!=auth_uid())return;
    if(c=='\r')c='\n';
    if(c>=128)return;
    unsigned next=(key_write+1)%sizeof keys;
    if(next!=key_read){keys[key_write]=c;key_write=next;}
}
void linux_guest_clear(void) {
    running=paused=false;key_read=key_write=0;
    memset(keys,0,sizeof keys);memset(screen,0,sizeof screen);line_count=1;column=escape=0;
    /* Keep one bounded allocation for reuse; wipe across EVERY host account
     * boundary. No MNFS credentials, host files or devices are shared. */
    if(ram)auth_wipe(ram,GUEST_RAM);
    memset(&vm,0,sizeof vm);state="Stopped / guest RAM cleared";
}
static bool start(void (*out)(const char *)) {
    size_t image_size=(size_t)(linux_image_end-linux_image_start);
    size_t dtb_size=(size_t)(linux_dtb_end-linux_dtb_start);
    if(!image_size){out("Linux guest payload is not included in this build.");return false;}
    if(image_size>GUEST_DTB-GUEST_LOAD||dtb_size>65536){out("Invalid bundled Linux image extent.");return false;}
    if(!ram){
        if(pages_free()<GUEST_RAM/PAGE+4096){out("At least 80 MiB free host RAM is required.");return false;}
        ram=kernel_alloc(GUEST_RAM);
    }
    linux_guest_clear();guest_init(&vm,ram,GUEST_RAM);vm.input=input;vm.output=output;
    if(!guest_copy(&vm,GUEST_BASE+GUEST_LOAD,linux_image_start,image_size)||
       !guest_copy(&vm,GUEST_BASE+GUEST_DTB,linux_dtb_start,dtb_size))return false;
    owner=auth_uid();running=true;state="Running / Linux in original RV64 interpreter";
    out("Starting Linux guest: 64 MiB private RAM, one software CPU.");
    out("Guest files are temporary. No host disks, network or GPU are exposed.");
    log_write("LINUX GUEST: start / original mnOS host / " MNOS_ARCH "\n");return true;
}
bool linux_guest_command(const char *cmd,void (*out)(const char *)) {
    if(strcmp(cmd,"linux")&&strncmp(cmd,"linux ",6))return false;
    if(!auth_logged_in()){out("Sign in to manage your Linux guest.");return true;}
    const char *p=cmd[5]?cmd+6:"status";
    if(!strcmp(p,"status")){out(state);out("RV64IMAC / Sv39 / Linux SBI / 64 MiB / volatile files");
        out("No Linux desktop apps, floating point, network or host sharing yet.");}
    else if(!strcmp(p,"start")){if(running)out("Guest already exists. Use resume or restart.");else start(out);}
    else if(!strcmp(p,"restart")){start(out);}
    else if(!strcmp(p,"pause")){if(running){paused=true;state="Paused / guest RAM retained";}out(state);}
    else if(!strcmp(p,"resume")){if(running){paused=false;state="Running / Linux in original RV64 interpreter";}out(state);}
    else if(!strcmp(p,"stop")){linux_guest_clear();out(state);}
    else if(!strncmp(p,"send ",5)){for(p+=5;*p;p++)linux_guest_key((unsigned char)*p);linux_guest_key('\n');}
    else out("Usage: linux start|status|pause|resume|stop|restart|send COMMAND");
    return true;
}
void linux_guest_tick(void) {
    if(!running||paused)return;
    if(!auth_logged_in()||owner!=auth_uid()){linux_guest_clear();return;}
    guest_run(&vm,100000);
    if(vm.stopped){running=false;state=vm.stopped==1?"Guest powered off / RAM cleared on next start":"Guest fault / stopped";
        log_write("LINUX GUEST: stopped, cause=");log_hex(vm.cause);log_write(" pc=");log_hex(vm.pc);log_write("\n");}
}
const char *linux_guest_status(void){return state;}
const char *linux_guest_line(unsigned n){return n<line_count?screen[n]:"";}
unsigned linux_guest_lines(void){return line_count;}
uint64_t linux_guest_instructions(void){return vm.retired;}
