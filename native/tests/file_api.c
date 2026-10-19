/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production user-copy, file API, volume routing, MNFS and account policy.
 * Only hardware/physical memory and the unused console exec hooks are mocked. */
#include "kernel.h"
#include "block.h"
#include "auth.h"
#include "mnfs.h"
#include "file_api.h"
#include "usercopy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
static uint8_t disk_image[16u << 20], memory[3][4096];
static unsigned writes, salt, pools_used;
static bool io_fail;
static void *pools[2];
static struct task task = {.code=4096, .data=8192, .stack=12288};
#define STACK (USER_STACK - PAGE)
__asm__(".data\nnative_hello_start: .zero 16\nnative_hello_end:\n"
        "native_spin_start: .zero 16\nnative_spin_end:\n"
        ".global native_hello_start,native_hello_end,native_spin_start,native_spin_end\n.text");
void *phys_pointer(uint64_t p) {
    assert(p >= 4096 && p < 16384); return memory[(p - 4096) / 4096] + p % 4096;
}
void *kernel_alloc(size_t n) {
    assert(pools_used < 2); void *p = calloc(1, n); assert(p); pools[pools_used++] = p; return p;
}
uint64_t pages_free(void) { return 65536; }
bool streq(const char *a, const char *b) { return !strcmp(a,b); }
void number(char *s,uint64_t n) { sprintf(s,"%" PRIu64,n); }
void log_write(const char *s) { (void)s; }
_Noreturn void panic(const char *s) { fprintf(stderr,"panic: %s\n",s); abort(); }
bool arch_random(uint8_t *p,size_t n) { while(n--) *p++ = (uint8_t)++salt; return true; }
bool process_command(const char *s,void (*print)(const char *)) { (void)s; (void)print; return false; }
int process_start(const void *p,size_t n,const char *s) { (void)p; (void)n; (void)s; abort(); }
unsigned block_discover(struct block_device *d,unsigned count) {
    assert(count); *d = (struct block_device){.sectors=sizeof disk_image / 512}; return 1;
}
bool block_read(struct block_device *d,uint64_t sector,unsigned count,void *p) {
    (void)d; if (io_fail || sector > sizeof disk_image / 512 || count > sizeof disk_image / 512 - sector) return false;
    memcpy(p,disk_image + sector * 512,(size_t)count * 512); return true;
}
bool block_write(struct block_device *d,uint64_t sector,unsigned count,const void *p) {
    (void)d; if (io_fail || sector > sizeof disk_image / 512 || count > sizeof disk_image / 512 - sector) return false;
    ++writes; memcpy(disk_image + sector * 512,p,(size_t)count * 512); return true;
}
bool block_flush(struct block_device *d) { (void)d; return !io_fail; }
static bool read_block(void *c,uint64_t b,void *p) { return block_read(c,b*8,8,p); }
static bool write_block(void *c,uint64_t b,const void *p) { return block_write(c,b*8,8,p); }
static bool flush(void *c) { return block_flush(c); }
static void print(const char *s) { (void)s; }
static void input(const char *s) { while(*s)auth_key(*s++);auth_key('\n'); }
static void finish(void) {
    for(unsigned i=0;i<700&&strstr(auth_prompt(),"Processing");++i)auth_tick(10);
    assert(!strstr(auth_prompt(),"Processing"));
}
static void password(const char *s) { input(s);input(s);finish(); }
static int64_t request(struct native_file_request r,uint32_t uid) {
    memcpy(memory[2],&r,sizeof r); return file_request(&task,uid,STACK,sizeof r);
}
static int64_t op(unsigned code,const char *path,uint64_t buffer,uint64_t size,uint64_t offset) {
    assert(strlen(path) < sizeof memory[0]); strcpy((char *)memory[0],path);
    return request((struct native_file_request){code,USER_BASE,buffer,size,offset},auth_uid());
}
static void boundaries(void) {
    struct native_file_request r={NATIVE_FILE_CREATE,USER_BASE,0,0,0};
    strcpy((char *)memory[0],"/disk/home/alice/rejected");
    unsigned before=writes;
    assert(file_request(&task,1000,UINT64_MAX-1,sizeof r)==-14);
    assert(file_request(&task,1000,STACK+4095,sizeof r)==-14);
    assert(file_request(&task,1000,STACK,sizeof r-1)==-22);
    r.operation=99;assert(request(r,1000)==-22);r.operation=NATIVE_FILE_CREATE;
    r.path=USER_BASE+2*PAGE;assert(request(r,1000)==-14);r.path=USER_BASE;
    r.buffer=USER_DATA;assert(request(r,1000)==-22);r.buffer=0;
    r.offset=1;assert(request(r,1000)==-22);r.offset=0;
    assert(request(r,0)==-13&&request(r,1001)==-13);
    memset(memory[0],'x',sizeof memory[0]);memory[0][0]='/';assert(request(r,1000)==-36);
    assert(op(NATIVE_FILE_CREATE,"relative",0,0,0)==-22);
    assert(writes==before);
    puts("PASS: malformed ABI, overflow/guard pointers, bounded paths, unused fields and captured UID");
}
static void files(void) {
    const char *path="/disk/home/alice/note";
    assert(!op(NATIVE_FILE_CREATE,path,0,0,0));
    assert(op(NATIVE_FILE_CREATE,path,0,0,0)==-17);
    unsigned before=writes;
    assert(op(NATIVE_FILE_WRITE,path,USER_DATA+4095,2,0)==-14);
    assert(op(NATIVE_FILE_WRITE,path,UINT64_MAX,1,0)==-14);
    assert(op(NATIVE_FILE_WRITE,path,USER_DATA,4097,0)==-22);
    assert(op(NATIVE_FILE_WRITE,path,USER_DATA,1,INT64_MAX)==-22);
    assert(op(NATIVE_FILE_TRUNCATE,path,0,0,UINT64_MAX)==-22);
    assert(writes==before);
    for(unsigned i=0;i<4096;++i)memory[1][i]=(uint8_t)(i*37);
    assert(op(NATIVE_FILE_WRITE,path,USER_DATA,4096,4094)==4096);
    memset(memory[1],0,4096);
    assert(op(NATIVE_FILE_READ,path,USER_DATA,4096,4094)==4096);
    for(unsigned i=0;i<4096;++i)assert(memory[1][i]==(uint8_t)(i*37));
    assert(op(NATIVE_FILE_READ,path,USER_BASE+2048,4,0)==-14); /* RX code is never an output buffer. */
    assert(op(NATIVE_FILE_STAT,path,USER_BASE+2048,24,0)==-14);
    assert(op(NATIVE_FILE_STAT,path,USER_DATA,23,0)==-22);
    uint64_t high=(1ULL<<45)+7;memcpy(memory[1],"native",6);
    assert(op(NATIVE_FILE_WRITE,path,USER_DATA,6,high)==6);
    assert(!op(NATIVE_FILE_STAT,path,USER_DATA,24,0));
    struct native_file_stat st;memcpy(&st,memory[1],sizeof st);assert(st.size==high+6&&!st.directory);
    memset(memory[1],0xa5,64);
    assert(op(NATIVE_FILE_READ,path,USER_DATA,8,high-2)==8);
    assert(!memory[1][0]&&!memory[1][1]&&!memcmp(memory[1]+2,"native",6));
    assert(op(NATIVE_FILE_READ,path,USER_DATA+32,1,high+6)==0&&memory[1][32]==0xa5);
    assert(op(NATIVE_FILE_READ,path,UINT64_MAX,0,0)==0);
    assert(!op(NATIVE_FILE_TRUNCATE,path,0,0,3));
    assert(op(NATIVE_FILE_READ,path,USER_DATA,16,0)==3);
    assert(!op(NATIVE_FILE_MKDIR,"/ram/home/alice/work",0,0,0));
    assert(op(NATIVE_FILE_READ,"/ram/home/alice/work",USER_DATA,1,0)==-21);
    assert(!op(NATIVE_FILE_CREATE,"/ram/home/alice/work/a",0,0,0));
    assert(op(NATIVE_FILE_REMOVE,"/ram/home/alice/work",0,0,0)==-39);
    assert(!op(NATIVE_FILE_REMOVE,"/ram/home/alice/work/a",0,0,0));
    assert(!op(NATIVE_FILE_REMOVE,"/ram/home/alice/work",0,0,0));
    assert(op(NATIVE_FILE_READ,"/ram/home/alice/missing",USER_DATA,1,0)==-2);
    puts("PASS: full-page binary I/O across blocks, sparse offsets above 32 TiB, EOF, truncate, directories and remove");
}
static void permissions(void) {
    const char *bad[]={"/disk/system/accounts.bin","/ram/root/file","/disk/home/bob/note",
        "/disk/home/alice/../bob/note","/disk/home/alice//note","/disk/home/alice/./note",
        "/disk/home/alice/note/","/disk/home/alice2/note"};
    unsigned before=writes;
    for(unsigned i=0;i<ARRAY_SIZE(bad);++i) {
        assert(op(NATIVE_FILE_READ,bad[i],USER_DATA,4,0)==-13);
        assert(op(NATIVE_FILE_CREATE,bad[i],0,0,0)==-13);
    }
    assert(op(NATIVE_FILE_CREATE,"/bin/test",0,0,0)==-13);
    assert(op(NATIVE_FILE_REMOVE,"/home/alice",0,0,0)==-13);
    assert(writes==before);
    assert(request((struct native_file_request){NATIVE_FILE_HOME,0,USER_DATA,64,1},1000)==16);
    assert(!strcmp((char *)memory[1],"/disk/home/alice")); /* length includes actual name only */
    assert(request((struct native_file_request){NATIVE_FILE_HOME,0,USER_BASE,64,0},1000)==-14);
    assert(request((struct native_file_request){NATIVE_FILE_HOME,0,USER_DATA,2,0},1000)==-28);
    auth_lock();assert(op(NATIVE_FILE_READ,"/home/readme.txt",USER_DATA,4,0)==-13);
    input("root");input("Root-admin-test-785");finish();assert(auth_admin());
    assert(op(NATIVE_FILE_READ,"/system/accounts.bin",USER_DATA,4,0)==-13);
    assert(!op(NATIVE_FILE_CREATE,"/disk/root/owned",0,0,0));
    assert(files_command("disk lock",print));before=writes;
    assert(op(NATIVE_FILE_WRITE,"/disk/root/owned",USER_DATA,4,0)==-30);
    assert(op(NATIVE_FILE_CREATE,"/disk/root/blocked",0,0,0)==-30);
    assert(writes==before);assert(files_command("disk unlock",print));
    memset(memory[1],0x5a,64);io_fail=true;
    assert(op(NATIVE_FILE_READ,"/disk/home/alice/note",USER_DATA,64,0)==-5);
    for(unsigned i=0;i<64;++i)assert(memory[1][i]==0x5a);
    io_fail=false;
    puts("PASS: canonical home isolation, protected credentials, stale-session denial, volume lock and I/O errors");
}
int main(void) {
    struct mnfs f;struct mnfs_io io={NULL,sizeof disk_image/MNFS_BLOCK,read_block,write_block,flush,malloc,free};
    assert(!mnfs_format(&f,&io));mnfs_close(&f);files_init();
    input("alice");password("Ordinary-test-491");password("Root-admin-test-785");
    assert(auth_logged_in()&&auth_uid()==1000);files_session();
    boundaries();files();permissions();
    /* Keep pool-backed volume state alive until process exit (same as kernel). */
    puts("PASS: production native file ABI with real MNFS/auth under ASan/UBSan");
}
