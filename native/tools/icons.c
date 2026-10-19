/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original mnOS vector icons. Build-time 8x coverage sampling, no runtime FP.
 * cc -O2 tools/icons.c -lm -o build/icon-gen
 * build/icon-gen > assets/icons.c */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
static bool box(double x, double y, double a, double b, double w, double h) {
    return x >= a && y >= b && x <= a + w && y <= b + h;
}
static bool roundbox(double x, double y, double a, double b, double w, double h, double r) {
    double dx = fmax(fabs(x-a-w/2)-w/2+r, 0), dy = fmax(fabs(y-b-h/2)-h/2+r, 0);
    return dx*dx+dy*dy <= r*r;
}
static bool circle(double x, double y, double a, double b, double r) {
    return (x-a)*(x-a)+(y-b)*(y-b) <= r*r;
}
static bool stroke(double x, double y, double a, double b, double c, double d, double width) {
    double dx=c-a, dy=d-b, t=fmax(0, fmin(1, ((x-a)*dx+(y-b)*dy)/(dx*dx+dy*dy)));
    return circle(x,y,a+t*dx,b+t*dy,width/2);
}
static uint32_t fade(uint32_t a, uint32_t b, double t) {
    t=fmax(0,fmin(1,t));
    unsigned r=((a>>16)&255)*(1-t)+((b>>16)&255)*t;
    unsigned g=((a>>8)&255)*(1-t)+((b>>8)&255)*t;
    unsigned bl=(a&255)*(1-t)+(b&255)*t;
    return 0xff000000u | r<<16 | g<<8 | bl;
}
static uint32_t sample(unsigned app, double x, double y) {
    uint32_t c=0;
    switch(app) {
    case 0: /* A folded M, matching the system wordmark. */
        if (stroke(x,y,8,35,8,12,4)||stroke(x,y,8,12,24,27,4)||stroke(x,y,24,27,40,12,4)||stroke(x,y,40,12,40,35,4)) c=0xffa3ead4;
        if (stroke(x,y,15,35,15,26,3)||stroke(x,y,15,26,24,35,3)||stroke(x,y,24,35,33,26,3)||stroke(x,y,33,26,33,35,3)) c=0xff5ba4c6;
        break;
    case 1: /* Folder, tab, inner paper, folded front edge. */
        if (roundbox(x,y,4,8,19,13,3)||roundbox(x,y,4,13,40,27,3)) c=0xff457cbe;
        if (roundbox(x,y,8,15,31,17,2)) c=0xffb6daf6;
        if (roundbox(x,y,4,20,40,22,3)) c=fade(0x79bdf5,0x4b8cdb,(y-20)/22);
        if (box(x,y,7,20,34,1)) c=0xffa9d7fc;
        break;
    case 2: /* Paper with folded corner and a pencil. */
        if (roundbox(x,y,9,4,29,39,3) && !(x>29 && y< x-25)) c=fade(0xf3f0e3,0xd3dcd7,y/48);
        if (x>=28 && y<=14 && y>=x-25) c=0xffa9bcbc;
        if (stroke(x,y,15,19,30,19,2)||stroke(x,y,15,25,26,25,2)||stroke(x,y,15,31,22,31,2)) c=0xff839ca3;
        if (stroke(x,y,26,39,41,22,5)) c=0xffe9b663;
        if (stroke(x,y,39,24,41,22,5)) c=0xffd57c75;
        if (x>=22 && x<=27 && y>=38 && y<=43 && x+y<67) c=0xff344858;
        break;
    case 3: /* Monitor and a pulse trace. */
        if (roundbox(x,y,18,33,12,8,1)||roundbox(x,y,12,40,24,3,1)) c=0xff8697b9;
        if (roundbox(x,y,3,7,42,29,4)) c=fade(0xb6a4ec,0x7989b9,y/40);
        if (roundbox(x,y,6,10,36,22,2)) c=0xff1f2d47;
        if (stroke(x,y,10,24,16,24,2)||stroke(x,y,16,24,21,15,2)||stroke(x,y,21,15,26,28,2)||stroke(x,y,26,28,31,20,2)||stroke(x,y,31,20,38,20,2)) c=0xff9be5ca;
        break;
    case 4: /* Terminal with a visible prompt and titlebar dots. */
        if (roundbox(x,y,3,6,42,36,4)) c=0xff6e8fa7;
        if (roundbox(x,y,5,8,38,32,3)) c=fade(0x2c4354,0x192c3b,y/48);
        if (box(x,y,5,15,38,1)) c=0xff526e82;
        if (circle(x,y,9,12,1)||circle(x,y,13,12,1)||circle(x,y,17,12,1)) c=0xffb3c9d3;
        if (stroke(x,y,12,23,18,28,2.5)||stroke(x,y,18,28,12,33,2.5)) c=0xffb3eed6;
        if (stroke(x,y,24,33,33,33,2)) c=0xffdeeaf0;
        break;
    case 5: /* Silicon package and pins. */
        for(unsigned n=0;n<4;n++) {
            double p=15+n*6;
            if (stroke(x,y,p,5,p,43,2)||stroke(x,y,5,p,43,p,2)) c=0xff8dbdbd;
        }
        if (roundbox(x,y,10,10,28,28,3)) c=fade(0x83cfc5,0x458e95,y/48);
        if (roundbox(x,y,15,15,18,18,2)) c=0xff244853;
        if (roundbox(x,y,18,18,12,12,1)) c=0xff76b9b3;
        if (circle(x,y,13,35,1)) c=0xffb9eee0;
        break;
    case 6: /* Eight-tooth gear, hub and clear aperture. */
        for(unsigned n=0;n<8;n++) {
            double a=n*3.141592653589793/4, dx=x-24, dy=y-24;
            double u=dx*cos(a)+dy*sin(a), v=-dx*sin(a)+dy*cos(a);
            if (roundbox(u,v,12,-4,9,8,1.5)) c=0xff9daccc;
        }
        if (circle(x,y,24,24,16)) c=fade(0xc1cce2,0x8395b8,y/48);
        if (circle(x,y,24,24,9)) c=0xff52677f;
        if (circle(x,y,24,24,6)) c=0;
        break;
    }
    return c;
}
int main(void) {
    const unsigned sizes[]={20,24,40,48};
    unsigned offset=0;
    puts("/* Generated from original mnOS artwork by tools/icons.c. */\n#include \"icons.h\"\nconst struct icon_asset icon_assets[28] = {");
    for(unsigned s=0;s<4;s++) for(unsigned app=0;app<7;app++) {
        printf("{%u,%u,%u},\n",offset,sizes[s],app); offset+=sizes[s]*sizes[s];
    }
    puts("};\nconst uint32_t icon_pixels[] = {");
    for(unsigned s=0;s<4;s++) for(unsigned app=0;app<7;app++)
        for(unsigned y=0;y<sizes[s];y++) for(unsigned x=0;x<sizes[s];x++) {
            unsigned a=0,r=0,g=0,b=0;
            for(unsigned j=0;j<8;j++) for(unsigned i=0;i<8;i++) {
                uint32_t c=sample(app,(x+(i+.5)/8)*48/sizes[s],(y+(j+.5)/8)*48/sizes[s]);
                if(c>>24) {a++;r+=(c>>16)&255;g+=(c>>8)&255;b+=c&255;}
            }
            uint32_t c=a ? ((a*255/64)<<24)|(r/a<<16)|(g/a<<8)|(b/a) : 0;
            printf("0x%08x,%s",c,x%8==7 ? "\n" : "");
        }
    puts("\n};");
}
