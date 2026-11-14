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
static uint8_t *memory[1024];
static unsigned live_pages;
static char received[4096];
static size_t received_len;
static bool prompt_due;
static struct cpu host_cpu;
struct cpu *cpu_this(void){return &host_cpu;}
static bool idle_next, exhaust_host;

bool auth_logged_in(void){return signed_in;}
uint32_t auth_uid(void){return uid;}
void auth_wipe(void *p,size_t n){memset(p,0,n);}
void log_write(const char *s){(void)s;}
void log_hex(uint64_t n){(void)n;}
uint64_t pages_free(void){return available;}
void number(char *out,uint64_t value){snprintf(out,32,"%llu",(unsigned long long)value);}
uint64_t page_try_alloc(void){
    allocations++;if(allocation_failure||!available)return 0;
    for(unsigned i=0;i<1024;++i)if(!memory[i]){memory[i]=malloc(4096);assert(memory[i]);memset(memory[i],0x5a,4096);++live_pages;--available;return (uint64_t)(i+1)*4096;}
    assert(!"test page pool exhausted");return 0;
}
void *phys_pointer(uint64_t p){assert(p&&!(p%4096)&&p/4096<=1024);assert(memory[p/4096-1]);return memory[p/4096-1];}
void page_free(uint64_t p){uint8_t *bytes=phys_pointer(p);for(unsigned i=0;i<4096;++i)assert(!bytes[i]);free(bytes);memory[p/4096-1]=NULL;--live_pages;++available;}
static void out(const char *s){assert(s);}
void guest_init(struct guest *g,uint8_t *ram,size_t n){
    memset(g,0,sizeof *g);g->ram=ram;g->ram_size=n;boots++;
    prompt_due=true;received_len=0;received[0]=0;
}
bool guest_copy(struct guest *g,uint64_t p,const void *s,size_t n){
    if(copy_failure)return false;
    assert(p>=GUEST_BASE&&p-GUEST_BASE+n<=g->ram_size);
    const uint8_t *source=s;uint64_t offset=p-GUEST_BASE;
    while(n){size_t size=4096-(size_t)(offset&4095);if(size>n)size=n;uint8_t *page=g->ram_page(g->ram_context,offset);if(!page)return false;memcpy(page+(offset&4095),source,size);source+=size;offset+=size;n-=size;}return true;
}
void guest_run(struct guest *g,unsigned budget){
    assert(budget&&!g->ram&&g->ram_page&&g->ram_size>=128u*1024u*1024u);runs++;g->retired+=budget;
    g->waiting=0;
    if(exhaust_host){exhaust_host=false;assert(!g->ram_page(g->ram_context,100u*1024u*1024u));return;}
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
    if(idle_next) {g->waiting=1;g->deadline=g->time+10000000;idle_next=false;}
}
static void pump_guest(void){for(unsigned i=0;i<4;i++){host_cpu.clock_ticks++;linux_guest_tick();}}
static void off(void){
    unsigned before=runs;pump_guest();
    assert(!linux_guest_enabled()&&!linux_guest_prompting()&&runs==before);
}
static void wiped(void){assert(!live_pages);for(unsigned i=0;i<1024;++i)assert(!memory[i]);}
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
    assert(live_pages<20); /* Sparse 128+ MiB guest did not reserve it all. */
    /* Typing before a queued app boots must not merge with its command. */
    linux_guest_key('x');pump_guest();assert(!strcmp(received,"uname -a; free -m\n"));
    size_t len=received_len;pump_guest();assert(received_len==len);
    assert(linux_guest_open("files",out)&&!linux_guest_prompting());pump_guest();
    assert(!strcmp(received+len,"ls -la /\n"));
    linux_guest_command("linux pause",out);before=runs;pump_guest();
    assert(runs==before&&linux_guest_enabled());
    linux_guest_command("linux resume",out);pump_guest();assert(runs>before&&!linux_guest_prompting());
    linux_guest_command("linux send echo hello",out);pump_guest();assert(strstr(received,"echo hello\n"));
    idle_next=true;linux_guest_tick();before=runs;
    uint64_t retired=linux_guest_instructions();pump_guest();
    assert(runs==before&&linux_guest_instructions()==retired&&strstr(linux_guest_status(),"Idle"));
    linux_guest_key('x');pump_guest();assert(runs>before&&strchr(received,'x'));
    idle_next=true;linux_guest_tick();before=runs;
    host_cpu.clock_ticks+=PIT_HZ+1;linux_guest_tick();assert(runs>before);

    /* Allocations began with nonzero data; every free checks all 4096 bytes. */
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
    /* A later session allocates fresh zeroed pages and requires fresh consent. */
    request();before=boots;pump_guest();assert(boots==before);
    assert(linux_guest_confirm(true,out));pump_guest();linux_guest_clear();off();wiped();

    linux_guest_command("linux memory 512",out);request();assert(!linux_guest_confirm(true,out));off();wiped();
    linux_guest_command("linux memory 128",out);request();assert(linux_guest_confirm(true,out));linux_guest_clear();wiped();
    linux_guest_command("linux memory 18446744073709551615",out);request();assert(linux_guest_confirm(true,out));linux_guest_clear();wiped();
    linux_guest_command("linux memory auto",out);
    request();assert(linux_guest_confirm(true,out));
    uint64_t saved=available;available=(32u*1024u*1024u)/PAGE;exhaust_host=true;
    before=live_pages;linux_guest_tick();off();wiped();
    assert(available==(32u*1024u*1024u)/PAGE+before);
    assert(strstr(linux_guest_status(),"memory exhausted"));available=saved+before;
    puts("Linux simulation consent, sparse RAM limits, private page wiping/reclamation and session tests PASS");return 0;
}
