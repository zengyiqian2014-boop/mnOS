/* SPDX-License-Identifier: GPL-3.0-or-later
 * Lossless QEMU P6 captures to PNG. Build-host C utility, never in the ISO.
 * cc -std=c11 -O2 tools/screenshots.c -lz -o build/screenshots */
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
static void fail(const char *reason) { fprintf(stderr, "%s\n", reason); exit(1); }
static void big32(unsigned char *out, uint32_t n) {
    out[0]=n>>24; out[1]=n>>16; out[2]=n>>8; out[3]=n;
}
static void write_bytes(FILE *f, const void *p, size_t n) {
    if (n && fwrite(p,1,n,f)!=n) fail("Cannot write PNG");
}
static void chunk(FILE *f, const char *name, const unsigned char *body, size_t n) {
    unsigned char b[4]; big32(b,(uint32_t)n); write_bytes(f,b,4); write_bytes(f,name,4);
    write_bytes(f,body,n);
    uLong crc=crc32(0,(const Bytef *)name,4);
    if(n) crc=crc32(crc,body,(uInt)n);
    big32(b,(uint32_t)crc); write_bytes(f,b,4);
}
static void token(FILE *f, char *out, size_t cap) {
    int c;
    do {
        c=fgetc(f);
        if(c=='#') do { c=fgetc(f); } while(c!='\n' && c!=EOF);
    } while(c!=EOF && isspace(c));
    size_t n=0;
    while(c!=EOF && !isspace(c)) {
        if(n+1>=cap) fail("PPM header token too long");
        out[n++]=(char)c; c=fgetc(f);
    }
    if(c=='\r') { c=fgetc(f); if(c!='\n' && c!=EOF) ungetc(c,f); }
    out[n]=0;
}
static unsigned dimension(FILE *f) {
    char b[24], *end;
    token(f,b,sizeof b);
    unsigned long n=strtoul(b,&end,10);
    if(!*b || *end || n<1 || n>8192) fail("Invalid PPM dimensions");
    return (unsigned)n;
}
static void convert(const char *directory, const char *name) {
    char path[4096], header[8];
    if(snprintf(path,sizeof path,"%s/%s",directory,name)>=(int)sizeof path) fail("Path too long");
    FILE *in=fopen(path,"rb"); if(!in) fail("Cannot open PPM");
    token(in,header,sizeof header); if(strcmp(header,"P6")) fail("Expected P6");
    unsigned w=dimension(in),h=dimension(in);
    token(in,header,sizeof header); if(strcmp(header,"255")) fail("Expected 8-bit PPM");
    size_t stride=w*3+1, length=stride*h;
    unsigned char *rows=calloc(length,1); if(!rows) fail("Out of memory");
    for(unsigned y=0;y<h;y++)
        if(fread(rows+y*stride+1,1,w*3,in)!=w*3) fail("Truncated PPM");
    if(fgetc(in)!=EOF) fail("Unexpected PPM data");
    fclose(in);
    uLongf packed_size=compressBound(length);
    unsigned char *packed=malloc(packed_size); if(!packed) fail("Out of memory");
    if(compress2(packed,&packed_size,rows,length,9)!=Z_OK) fail("PNG compression failed");
    free(rows);
    strcpy(path+strlen(path)-4,".png");
    FILE *out=fopen(path,"wb"); if(!out) fail("Cannot create PNG");
    const unsigned char signature[]={137,80,78,71,13,10,26,10};
    unsigned char info[13]={0}; big32(info,w); big32(info+4,h); info[8]=8; info[9]=2;
    write_bytes(out,signature,sizeof signature);
    chunk(out,"IHDR",info,sizeof info); chunk(out,"IDAT",packed,packed_size); chunk(out,"IEND",NULL,0);
    free(packed);
    if(fclose(out)) fail("Cannot finish PNG");
    printf("PNG %s (%u x %u)\n",path,w,h);
}
int main(int argc,char **argv) {
    if(argc!=2) fail("Usage: screenshots DIRECTORY");
    DIR *dir=opendir(argv[1]); if(!dir) fail("Cannot open capture directory");
    struct dirent *entry;
    while((entry=readdir(dir))) {
        const char *name=entry->d_name; size_t n=strlen(name);
        if(n>4 && (!strncmp(name,"amd64-",6)||!strncmp(name,"arm64-",6)) && !strcmp(name+n-4,".ppm"))
            convert(argv[1],name);
    }
    closedir(dir);
}
