/* SPDX-License-Identifier: GPL-3.0-or-later
 * Integration harness for the same native CPU and SBI device boundary.
 * libslirp and POSIX file/network APIs are TEST HOST ONLY, never native code.
 */
#define _POSIX_C_SOURCE 200809L
#include "guest.h"
#include "guest_memory.h"
#include "guest_devices.h"
#include <slirp/libslirp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <poll.h>
#include <time.h>
#include <errno.h>

static struct guest vm;
static Slirp *slirp;
static int disk,keyboard=STDIN_FILENO;
static unsigned head,tail;
static struct {uint8_t bytes[2048];size_t size;} packets[256];
static struct pollfd polls[256];static unsigned poll_count;
static unsigned prompt;static bool ready;static const char *commands;
static int events=-1;static char event_line[128];static unsigned event_size;
static uint64_t monotonic_ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))abort();return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;}
static void input_events(struct guest_devices *devices){
    char c;while(events>=0&&read(events,&c,1)==1){
        if(c!='\n'){if(event_size<sizeof event_line-1)event_line[event_size++]=c;continue;}
        event_line[event_size]=0;unsigned key,x,y,button;
        if(sscanf(event_line,"key %u",&key)==1&&key<256){guest_device_input(devices,1,key,1);guest_device_input(devices,0,0,0);guest_device_input(devices,1,key,0);}
        else if(sscanf(event_line,"pointer %u %u %u",&x,&y,&button)==3&&x<1024&&y<768&&button<2){guest_device_input(devices,3,0,x);guest_device_input(devices,3,1,y);guest_device_input(devices,1,272,button);}
        guest_device_input(devices,0,0,0);event_size=0;
    }
}
static int input(void *p){(void)p;if(ready&&commands&&*commands){unsigned char c=(unsigned char)*commands++;if(c=='\n')ready=false;return c;}unsigned char c;return read(keyboard,&c,1)==1?c:-1;}
static void output(void *p,unsigned char c){(void)p;fputc(c,stdout);const char *s="linux-guest# ";if(c==(unsigned char)s[prompt]){if(!s[++prompt]){prompt=0;ready=true;}}else prompt=c==(unsigned char)s[0];}
static bool read_disk(void *p,uint64_t off,void *out,size_t n){(void)p;return pread(disk,out,n,(off_t)off)==(ssize_t)n;}
static bool write_disk(void *p,uint64_t off,const void *in,size_t n){(void)p;return pwrite(disk,in,n,(off_t)off)==(ssize_t)n;}
static bool sync_disk(void *p){(void)p;return !fdatasync(disk);}
static bool random_bytes(uint8_t *p,size_t n){return getrandom(p,n,0)==(ssize_t)n;}
static bool wall_clock(uint64_t *out){*out=(uint64_t)time(NULL);return true;}
static int rx(void *p,void *out,size_t n){(void)p;if(head==tail)return 0;size_t size=packets[tail].size;int result=0;if(size<=n){memcpy(out,packets[tail].bytes,size);result=(int)size;}tail=(tail+1)%256;return result;}
static int tx(void *p,const void *in,size_t n){(void)p;slirp_input(slirp,in,(int)n);return (int)n;}
static ssize_t packet(const void *p,size_t n,void *unused){(void)unused;unsigned next=(head+1)%256;if(n<=2048&&next!=tail){memcpy(packets[head].bytes,p,n);packets[head].size=n;head=next;}return (ssize_t)n;}
static int64_t now(void *p){(void)p;return (int64_t)(vm.time*100);}
static void error(const char *s,void *p){(void)p;fprintf(stderr,"slirp: %s\n",s);}
static void noop(void *p){(void)p;}
static void poll_noop(int fd,void *p){(void)fd;(void)p;}
struct timer {SlirpTimerCb cb;void *p;int64_t expires;struct timer *next;};
static struct timer *timers;
static void *timer_new(SlirpTimerCb cb,void *p,void *o){(void)o;struct timer *t=calloc(1,sizeof *t);if(!t)abort();*t=(struct timer){cb,p,-1,timers};timers=t;return t;}
static void timer_free(void *p,void *o){(void)o;struct timer **link=&timers;while(*link&&*link!=p)link=&(*link)->next;if(*link){*link=(*link)->next;free(p);}}
static void timer_mod(void *p,int64_t expires,void *o){(void)o;((struct timer *)p)->expires=expires;}
static int poll_add(int fd,int events,void *p){(void)p;if(poll_count==256)abort();int e=0;if(events&SLIRP_POLL_IN)e|=POLLIN;if(events&SLIRP_POLL_OUT)e|=POLLOUT;if(events&SLIRP_POLL_PRI)e|=POLLPRI;polls[poll_count]=(struct pollfd){fd,e,0};return (int)poll_count++;}
static int poll_get(int index,void *p){(void)p;int e=polls[index].revents,r=0;if(e&POLLIN)r|=SLIRP_POLL_IN;if(e&POLLOUT)r|=SLIRP_POLL_OUT;if(e&POLLPRI)r|=SLIRP_POLL_PRI;if(e&POLLERR)r|=SLIRP_POLL_ERR;if(e&POLLHUP)r|=SLIRP_POLL_HUP;return r;}
static void network(void){uint32_t timeout=0;poll_count=0;slirp_pollfds_fill(slirp,&timeout,poll_add,NULL);int r=poll(polls,poll_count,0);slirp_pollfds_poll(slirp,r<0,poll_get,NULL);for(struct timer *t=timers;t;t=t->next)if(t->expires>=0&&t->expires<=now(NULL)/1000000){t->expires=-1;t->cb(t->p);}}
static void *file(const char *name,size_t *size){int fd=open(name,O_RDONLY);if(fd<0){perror(name);exit(1);}struct stat st;if(fstat(fd,&st)||!st.st_size)exit(1);*size=(size_t)st.st_size;void *p=mmap(NULL,*size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);if(p==MAP_FAILED)exit(1);return p;}
static void screenshot(const char *path){FILE *f=fopen(path,"wb");if(!f)return;fprintf(f,"P6\n%u %u\n255\n",GUEST_WIDTH,GUEST_HEIGHT);uint32_t pixels[1024];uint8_t row[3072];for(unsigned y=0;y<GUEST_HEIGHT;y++){if(!guest_read(&vm,GUEST_FB+y*4096,pixels,sizeof pixels))break;for(unsigned x=0;x<GUEST_WIDTH;x++){row[x*3]=(uint8_t)(pixels[x]>>16);row[x*3+1]=(uint8_t)(pixels[x]>>8);row[x*3+2]=(uint8_t)pixels[x];}fwrite(row,1,sizeof row,f);}fclose(f);}
int main(int argc,char **argv){
    if(argc!=6){fprintf(stderr,"Usage: %s Image DTB rootfs.squashfs data.img framebuffer.ppm\n",argv[0]);return 2;}
    size_t image_size,dtb_size,root_size;void *image=file(argv[1],&image_size),*source_dtb=file(argv[2],&dtb_size),*root=file(argv[3],&root_size);
    const size_t ram_size=1024ULL*1024*1024;uint8_t *ram=calloc(1,ram_size);void *dtb=malloc(dtb_size);if(!ram||!dtb)return 1;
    memcpy(dtb,source_dtb,dtb_size);if(!guest_dtb_memory(dtb,dtb_size,ram_size))return 1;
    guest_init(&vm,ram,ram_size);vm.input=input;vm.output=output;vm.external_time=1;vm.yield_on_idle=1;
    if(!guest_copy(&vm,GUEST_BASE+GUEST_LOAD,image,image_size)||!guest_copy(&vm,GUEST_BASE+GUEST_DTB,dtb,dtb_size))return 1;
    disk=open(argv[4],O_RDWR|O_CREAT,0600);struct flock lock={.l_type=F_WRLCK,.l_whence=SEEK_SET};
    if(disk<0||fcntl(disk,F_SETLK,&lock)||ftruncate(disk,GUEST_DISK_BYTES)){perror("private test disk");return 1;}
    struct guest_devices devices={.root=root,.root_size=root_size,.disk_read=read_disk,.disk_write=write_disk,.disk_sync=sync_disk,.receive=rx,.transmit=tx,.random=random_bytes,.clock=wall_clock,.mac={0x52,0x54,0,0x12,0x34,0x56}};
    vm.device=guest_device_call;vm.opaque=&devices;
    SlirpConfig conf={.version=5,.in_enabled=true,.disable_host_loopback=true};
    inet_pton(AF_INET,"10.0.2.0",&conf.vnetwork);inet_pton(AF_INET,"255.255.255.0",&conf.vnetmask);inet_pton(AF_INET,"10.0.2.2",&conf.vhost);inet_pton(AF_INET,"10.0.2.3",&conf.vnameserver);inet_pton(AF_INET,"10.0.2.15",&conf.vdhcp_start);
    SlirpCb cb={.send_packet=packet,.guest_error=error,.clock_get_ns=now,.timer_new=timer_new,.timer_free=timer_free,.timer_mod=timer_mod,.register_poll_fd=poll_noop,.unregister_poll_fd=poll_noop,.notify=noop};
    slirp=slirp_new(&conf,&cb,NULL);if(!slirp)return 1;
    const char *fifo=getenv("MNOS_GUEST_INPUT");
    if(fifo){struct stat st;if(mkfifo(fifo,0600)&&errno!=EEXIST)return 1;
        if(lstat(fifo,&st)||!S_ISFIFO(st.st_mode))return 1;
        keyboard=open(fifo,O_RDWR|O_NONBLOCK|O_CLOEXEC);if(keyboard<0)return 1;}
    fifo=getenv("MNOS_GUEST_EVENTS");if(fifo){struct stat st;if(mkfifo(fifo,0600)&&errno!=EEXIST)return 1;
        if(lstat(fifo,&st)||!S_ISFIFO(st.st_mode))return 1;
        events=open(fifo,O_RDWR|O_NONBLOCK|O_CLOEXEC);if(events<0)return 1;}
    commands=getenv("MNOS_GUEST_COMMANDS");setvbuf(stdout,NULL,_IONBF,0);fcntl(keyboard,F_SETFL,fcntl(keyboard,F_GETFL)|O_NONBLOCK);
    time_t end=time(NULL)+1800,last=0;unsigned steps=0;uint64_t started=monotonic_ns();
    while(!vm.stopped&&time(NULL)<end){vm.time=(monotonic_ns()-started)/100;guest_run(&vm,100000);network();input_events(&devices);
        if(!(++steps%100)&&time(NULL)-last>=5){screenshot(argv[5]);last=time(NULL);}
        if(vm.waiting){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}}
    screenshot(argv[5]);fprintf(stderr,"\nGUEST state=%u pc=%llx cause=%llx value=%llx instructions=%llu\n",vm.stopped,(unsigned long long)vm.pc,(unsigned long long)vm.cause,(unsigned long long)vm.tval,(unsigned long long)vm.retired);
    slirp_cleanup(slirp);close(disk);free(ram);free(dtb);munmap(image,image_size);munmap(source_dtb,dtb_size);munmap(root,root_size);return vm.stopped==1?0:1;
}
