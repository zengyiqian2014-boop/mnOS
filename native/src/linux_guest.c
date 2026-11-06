/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
#include "auth.h"
#include "guest.h"
#include "linux_guest.h"
extern const unsigned char linux_image_start[],linux_image_end[],linux_dtb_start[],linux_dtb_end[];
static struct guest vm;
static uint8_t *ram;
static bool running, paused, confirming, shell_ready;
static uint32_t owner=UINT32_MAX;
static unsigned requested_app, prompt_match;
static const char *queued_command;
static const struct { const char *id, *name, *command; } apps[] = {
    {"terminal", "Linux 终端", NULL},
    {"files", "Linux 文件列表", "ls -la /"},
    {"processes", "Linux 进程", "ps"},
    {"system", "Linux 系统信息", "uname -a; free -m"},
    {"commands", "Linux 命令目录", "busybox --list"}
};
static char screen[128][321];
static unsigned line_count=1,column,used,escape,utf_count,utf_need;
static unsigned char utf[4];
static unsigned char keys[256];
static unsigned key_read,key_write;
static const char *state="Disabled / Linux simulation is off by default";
static void newline(void) {
    if(line_count<128)line_count++;
    else {for(unsigned i=1;i<128;i++)memcpy(screen[i-1],screen[i],sizeof screen[0]);}
    memset(screen[line_count-1],0,sizeof screen[0]);column=used=0;
}
static void append(const unsigned char *s,unsigned n,unsigned cells) {
    if(column+cells>72||used+n>=sizeof screen[0])newline();
    memcpy(screen[line_count-1]+used,s,n);used+=n;column+=cells;
    screen[line_count-1][used]=0;
}
static void output(void *unused,unsigned char c) {
    (void)unused;
    char text[2]={(char)c,0};log_write(text);
    /* Queue an app only after the real guest shell is ready. This marker is
     * guest I/O, never authorization to start a host or guest environment. */
    static const char prompt[]="linux-guest# ";
    if(c==(unsigned char)prompt[prompt_match]) {
        if(++prompt_match==sizeof prompt-1){shell_ready=true;prompt_match=0;}
    } else prompt_match=c==(unsigned char)prompt[0]?1:0;
    /* TERM=dumb guest. Strip control sequences, never interpret guest text as
     * host commands or feed it to the account/installer input path. */
    if(escape){if(c>=0x40&&c<=0x7e&&c!='[')escape=0;return;}
    if(c==27){escape=1;return;}
    if(utf_need) {
        if((c&0xc0)==0x80) {
            utf[utf_count++]=c;
            if(utf_count==utf_need) {
                uint32_t cp=utf[0] & (utf_need==2?31:utf_need==3?15:7);
                for(unsigned i=1;i<utf_need;i++)cp=(cp<<6)|(utf[i]&63);
                bool valid=(utf_need!=2||cp>=128)&&(utf_need!=3||cp>=2048)&&(utf_need!=4||cp>=65536)&&cp<=0x10ffff&&!(cp>=0xd800&&cp<=0xdfff);
                if(valid)append(utf,utf_need,cp>=0x1100?2:1);
                else append((const unsigned char *)"?",1,1);
                utf_need=utf_count=0;
            }
            return;
        }
        utf_need=utf_count=0;append((const unsigned char *)"?",1,1);
    }
    if(c>=0xc2&&c<=0xf4){utf[0]=c;utf_count=1;utf_need=c<0xe0?2:c<0xf0?3:4;return;}
    if(c=='\n'){newline();return;}
    if(c=='\r'){column=used=0;return;}
    if(c=='\b'||c==127){if(used){do{used--;}while(used&&((unsigned char)screen[line_count-1][used]&0xc0)==0x80);screen[line_count-1][used]=0;if(column)column--;}return;}
    if(c=='\t'){do {append((const unsigned char *)" ",1,1);}while(column%8);return;}
    if(c<32||c>126)return;
    append(&c,1,1);
}
static int input(void *unused) {
    (void)unused;if(key_read==key_write)return -1;
    unsigned char c=keys[key_read];key_read=(key_read+1)%sizeof keys;return c;
}
void linux_guest_key(unsigned char c) {
    if(!running||paused||queued_command||!auth_logged_in()||owner!=auth_uid())return;
    if(c=='\r')c='\n';
    if(c>=128)return;
    shell_ready=false;
    unsigned next=(key_write+1)%sizeof keys;
    if(next!=key_read){keys[key_write]=c;key_write=next;}
}
void linux_guest_clear(void) {
    running=paused=confirming=shell_ready=false;key_read=key_write=prompt_match=0;
    queued_command=NULL;requested_app=0;owner=UINT32_MAX;
    memset(keys,0,sizeof keys);memset(screen,0,sizeof screen);line_count=1;column=used=escape=utf_count=utf_need=0;
    /* Keep one bounded allocation for reuse; wipe across EVERY host account
     * boundary. No MNFS credentials, host files or devices are shared. */
    if(ram)auth_wipe(ram,GUEST_RAM);
    memset(&vm,0,sizeof vm);state="Stopped / guest RAM cleared";
}
static bool session(void) {
    if(owner!=UINT32_MAX && (!auth_logged_in()||owner!=auth_uid()))linux_guest_clear();
    return auth_logged_in();
}
bool linux_guest_enabled(void){session();return running;}
bool linux_guest_prompting(void){session();return confirming;}
const char *linux_guest_requested_app(void){return apps[requested_app].name;}
static bool start(void (*out)(const char *)) {
    size_t image_size=(size_t)(linux_image_end-linux_image_start);
    size_t dtb_size=(size_t)(linux_dtb_end-linux_dtb_start);
    if(!image_size){out("Linux guest payload is not included in this build.");return false;}
    if(image_size>GUEST_DTB-GUEST_LOAD||dtb_size>65536){out("Invalid bundled Linux image extent.");return false;}
    if(!ram){
        if(pages_free()<GUEST_RAM/PAGE+4096){out("At least 80 MiB free host RAM is required.");return false;}
        ram=kernel_alloc(GUEST_RAM);
        if(!ram){out("Unable to allocate Linux guest memory.");return false;}
    }
    linux_guest_clear();guest_init(&vm,ram,GUEST_RAM);vm.input=input;vm.output=output;
    if(!guest_copy(&vm,GUEST_BASE+GUEST_LOAD,linux_image_start,image_size)||
       !guest_copy(&vm,GUEST_BASE+GUEST_DTB,linux_dtb_start,dtb_size)) {
        linux_guest_clear();out("Unable to load Linux guest; memory cleared.");return false;
    }
    owner=auth_uid();running=true;state="Running / Linux in original RV64 interpreter";
    out("Starting Linux guest: 64 MiB private RAM, one software CPU.");
    out("Guest files are temporary. No host disks, network or GPU are exposed.");
    log_write("LINUX GUEST: start / original mnOS host / " MNOS_ARCH "\n");return true;
}
static void queue_app(unsigned app,void (*out)(const char *)) {
    queued_command=apps[app].command;
    if(queued_command)out("Linux app queued; waiting for the guest shell.");
}
bool linux_guest_open(const char *app,void (*out)(const char *)) {
    if(!session()){out("Sign in to manage your Linux guest.");return false;}
    unsigned i=0;while(i<ARRAY_SIZE(apps)&&strcmp(app,apps[i].id))i++;
    if(i==ARRAY_SIZE(apps)){out("Unknown Linux app. Use linux apps.");return false;}
    if(confirming){out("Confirm or cancel the current Linux simulation request first.");return false;}
    if(running) {
        if(queued_command){out("A Linux app is already queued. Finish the current command or stop the guest.");return false;}
        paused=false;state="Running / Linux in original RV64 interpreter";
        queue_app(i,out);return true;
    }
    owner=auth_uid();requested_app=i;confirming=true;
    state="Disabled / awaiting your confirmation";
    out("是否开启 Linux 模拟？ / Enable Linux simulation?");
    out(apps[i].name);out("Enter: enable and open / Esc: cancel. Settings can turn it off anytime.");
    log_write("LINUX SIMULATION: confirmation requested\n");return true;
}
bool linux_guest_confirm(bool enable,void (*out)(const char *)) {
    if(!session()||!confirming)return false;
    unsigned app=requested_app;confirming=false;
    if(!enable){state="Disabled / Linux simulation request cancelled";requested_app=0;
        out(state);log_write("LINUX SIMULATION: cancelled\n");return false;}
    state="Disabled / Linux simulation has not started";
    if(!start(out))return false;
    queue_app(app,out);return true;
}
bool linux_guest_command(const char *cmd,void (*out)(const char *)) {
    if(strcmp(cmd,"linux")&&strncmp(cmd,"linux ",6))return false;
    if(!session()){out("Sign in to manage your Linux guest.");return true;}
    const char *p=cmd[5]?cmd+6:"status";
    if(!strcmp(p,"status")){out(state);out("RV64IMAC / Sv39 / Linux SBI / 64 MiB / volatile files");
        out("No Linux desktop apps, floating point, network or host sharing yet.");}
    else if(!strcmp(p,"apps")){for(unsigned i=0;i<ARRAY_SIZE(apps);i++){out(apps[i].id);out(apps[i].name);}}
    else if(!strncmp(p,"open ",5)){linux_guest_open(p+5,out);}
    else if(!strcmp(p,"start")||!strcmp(p,"enable")){linux_guest_open("terminal",out);}
    else if(!strcmp(p,"restart")){if(running)start(out);else linux_guest_open("terminal",out);}
    else if(!strcmp(p,"pause")){if(running){paused=true;state="Paused / guest RAM retained";}out(state);}
    else if(!strcmp(p,"resume")){if(running){paused=false;state="Running / Linux in original RV64 interpreter";out(state);}else linux_guest_open("terminal",out);}
    else if(!strcmp(p,"stop")||!strcmp(p,"disable")){linux_guest_clear();out(state);log_write("LINUX SIMULATION: disabled\n");}
    else if(!strncmp(p,"send ",5)){
        if(!running||paused||confirming)out("Linux simulation is off or paused. Open a Linux app or resume first.");
        else if(queued_command)out("Wait for the queued Linux app before sending another command.");
        else if(strlen(p+5)+1>=sizeof keys-(key_write+sizeof keys-key_read)%sizeof keys)out("Linux input is too long; it was not sent.");
        else {for(p+=5;*p;p++)linux_guest_key((unsigned char)*p);linux_guest_key('\n');}}
    else out("Usage: linux apps|open APP|start|status|pause|resume|stop|restart|enable|disable|send COMMAND");
    return true;
}
void linux_guest_tick(void) {
    if(!session()||!running||paused)return;
    guest_run(&vm,100000);
    if(vm.stopped){
        bool fault=vm.stopped!=1;
        log_write("LINUX GUEST: stopped, cause=");log_hex(vm.cause);log_write(" pc=");log_hex(vm.pc);log_write("\n");
        linux_guest_clear();state=fault?"Guest fault / simulation off / RAM cleared":"Guest powered off / simulation off / RAM cleared";
    } else if(shell_ready&&queued_command&&key_read==key_write) {
        const char *p=queued_command;queued_command=NULL;
        for(;*p;p++)linux_guest_key((unsigned char)*p);linux_guest_key('\n');
        log_write("LINUX SIMULATION: app sent to guest shell\n");
    }
}
const char *linux_guest_status(void){return state;}
const char *linux_guest_line(unsigned n){return n<line_count?screen[n]:"";}
unsigned linux_guest_lines(void){return line_count;}
uint64_t linux_guest_instructions(void){return vm.retired;}
