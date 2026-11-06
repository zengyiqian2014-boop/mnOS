/* SPDX-License-Identifier: GPL-3.0-or-later
 * Consent/session tests with a deterministic guest I/O double. Real Linux boot
 * and app execution are covered by desktop_vm.cpp, not this double. */
#include "kernel.h"
#include "auth.h"
#include "guest.h"
#include "linux_guest.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool signed_in=true, copy_failure, allocation_failure;
static uint32_t uid=1000;
static uint64_t available=100000;
static unsigned allocations, boots, runs, stop_reason;
static uint8_t *memory;
static char received[4096];
static size_t received_len;
static bool prompt_due;

bool auth_logged_in(void){return signed_in;}
uint32_t auth_uid(void){return uid;}
void auth_wipe(void *p,size_t n){memset(p,0,n);}
void log_write(const char *s){(void)s;}
void log_hex(uint64_t n){(void)n;}
uint64_t pages_free(void){return available;}
void *kernel_alloc(size_t n){
    allocations++;
    if(allocation_failure)return NULL;
    assert(!memory&&n==GUEST_RAM);memory=malloc(n);assert(memory);return memory;
}
static void out(const char *s){assert(s);}
void guest_init(struct guest *g,uint8_t *ram,size_t n){
    memset(g,0,sizeof *g);g->ram=ram;g->ram_size=n;boots++;
    prompt_due=true;received_len=0;received[0]=0;
}
bool guest_copy(struct guest *g,uint64_t p,const void *s,size_t n){
    if(copy_failure)return false;
    assert(p>=GUEST_BASE&&p-GUEST_BASE+n<=g->ram_size);
    memcpy(g->ram+p-GUEST_BASE,s,n);return true;
}
void guest_run(struct guest *g,unsigned budget){
    assert(budget&&g->ram==memory);runs++;g->retired+=budget;
    if(stop_reason){g->stopped=stop_reason;stop_reason=0;return;}
    int c;
    while((c=g->input(g->opaque))>=0){
        assert(received_len+1<sizeof received);
        received[received_len++]=(char)c;received[received_len]=0;
        if(c=='\n')prompt_due=true;
    }
    if(prompt_due){
        const char *p="linux-guest# ";
        while(*p)g->output(g->opaque,(unsigned char)*p++);
        prompt_due=false;
    }
}
static void pump_guest(void){for(unsigned i=0;i<4;i++)linux_guest_tick();}
static void off(void){
    unsigned before=runs;pump_guest();
    assert(!linux_guest_enabled()&&!linux_guest_prompting()&&runs==before);
}
static void wiped(void){for(size_t i=0;i<GUEST_RAM;i++)assert(memory[i]==0);}
static void request(void){
    assert(linux_guest_open("terminal",out));
    assert(linux_guest_prompting()&&!linux_guest_enabled());
}
int main(void){
    off();assert(!allocations&&!boots);
    assert(!linux_guest_open("unknown",out));off();
    signed_in=false;assert(!linux_guest_open("terminal",out));off();signed_in=true;
    assert(linux_guest_command("linux resume",out));
    assert(linux_guest_prompting());pump_guest();assert(!allocations&&!boots);
    assert(!linux_guest_confirm(false,out));off();
    assert(!linux_guest_confirm(true,out));

    /* All off -> start routes require consent, including restart and enable. */
    const char *routes[]={"linux start","linux enable","linux restart","linux open system"};
    for(unsigned i=0;i<ARRAY_SIZE(routes);i++){
        assert(linux_guest_command(routes[i],out));assert(linux_guest_prompting());
        pump_guest();assert(!allocations&&!boots);linux_guest_command("linux disable",out);off();
        assert(!linux_guest_confirm(true,out));
    }
    request();uid++;assert(!linux_guest_confirm(true,out));off();assert(!boots);
    request();signed_in=false;pump_guest();signed_in=true;
    assert(!linux_guest_confirm(true,out));off();

    available=0;request();assert(!linux_guest_confirm(true,out));off();assert(!allocations);
    available=100000;allocation_failure=true;request();
    assert(!linux_guest_confirm(true,out));off();assert(!boots);
    allocation_failure=false;copy_failure=true;request();
    assert(!linux_guest_confirm(true,out));off();wiped();copy_failure=false;

    assert(linux_guest_open("system",out));
    assert(!strcmp(linux_guest_requested_app(),"Linux 系统信息"));
    unsigned before=boots;pump_guest();assert(boots==before);
    assert(linux_guest_confirm(true,out)&&linux_guest_enabled());
    assert(!linux_guest_prompting()&&!received_len);
    /* Typing before a queued app boots must not merge with its command. */
    linux_guest_key('x');pump_guest();assert(!strcmp(received,"uname -a; free -m\n"));
    size_t len=received_len;pump_guest();assert(received_len==len);
    assert(linux_guest_open("files",out)&&!linux_guest_prompting());pump_guest();
    assert(!strcmp(received+len,"ls -la /\n"));
    linux_guest_command("linux pause",out);before=runs;pump_guest();
    assert(runs==before&&linux_guest_enabled());
    linux_guest_command("linux resume",out);pump_guest();assert(runs>before&&!linux_guest_prompting());
    linux_guest_command("linux send echo hello",out);pump_guest();assert(strstr(received,"echo hello\n"));

    memset(memory,0x5a,GUEST_RAM);
    linux_guest_command("linux pause",out);uid++;off();wiped();
    request();assert(linux_guest_confirm(true,out));pump_guest();
    linux_guest_command("linux pause",out);signed_in=false;off();wiped();signed_in=true;
    request();assert(linux_guest_confirm(true,out));pump_guest();
    assert(linux_guest_open("processes",out));
    linux_guest_command("linux disable",out);off();wiped();
    assert(!linux_guest_confirm(true,out));

    for(unsigned reason=1;reason<=2;reason++){
        request();assert(linux_guest_confirm(true,out));stop_reason=reason;pump_guest();off();wiped();
        assert(strstr(linux_guest_status(),reason==1?"powered off":"fault"));
    }
    /* A later session reuses zeroed RAM but still requires fresh consent. */
    request();before=boots;pump_guest();assert(boots==before);
    assert(linux_guest_confirm(true,out));pump_guest();linux_guest_clear();off();wiped();
    free(memory);
    puts("Linux simulation consent/session tests PASS");return 0;
}
