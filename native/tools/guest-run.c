/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Hosted harness for the SAME freestanding core used by the native desktop. */
#define _POSIX_C_SOURCE 200809L
#include "guest.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
static int input(void *opaque) { (void)opaque;unsigned char c;return read(0,&c,1)==1?c:-1; }
static void output(void *opaque,unsigned char c) { (void)opaque;fputc(c,stdout); }
static void load(struct guest *g,const char *path,uint64_t address) {
    FILE *f=fopen(path,"rb");if(!f){perror(path);exit(1);}
    unsigned char b[4096];size_t n;
    while((n=fread(b,1,sizeof b,f))) {if(!guest_copy(g,address,b,n)){fputs("Image exceeds guest RAM\n",stderr);exit(1);}address+=n;}
    if(ferror(f)){perror(path);exit(1);}fclose(f);
}
int main(int argc,char **argv) {
    if(argc!=3){fprintf(stderr,"Usage: %s Image machine.dtb\n",argv[0]);return 2;}
    struct guest g;unsigned char *ram=calloc(1,GUEST_RAM);if(!ram)return 1;
    guest_init(&g,ram,GUEST_RAM);g.input=input;g.output=output;
    load(&g,argv[1],GUEST_BASE+GUEST_LOAD);load(&g,argv[2],GUEST_BASE+GUEST_DTB);
    setvbuf(stdout,NULL,_IONBF,0);int old=fcntl(0,F_GETFL);if(old>=0)fcntl(0,F_SETFL,old|O_NONBLOCK);
    for(unsigned i=0;i<300000&&!g.stopped;i++)guest_run(&g,100000);
    fprintf(stderr,"\nGUEST state=%u pc=%llx cause=%llx value=%llx instructions=%llu\n",g.stopped,(unsigned long long)g.pc,(unsigned long long)g.cause,(unsigned long long)g.tval,(unsigned long long)g.retired);
    free(ram);return g.stopped==1?0:1;
}
