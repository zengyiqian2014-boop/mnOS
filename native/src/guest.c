/* SPDX-License-Identifier: GPL-3.0-or-later */
/* mnOS software machine, implemented against the RISC-V unprivileged ISA,
 * privileged ISA (Sv39/S-mode) and SBI specifications. No borrowed emulator. */
#include "guest.h"
#include <string.h>
#define BIT(n) (UINT64_C(1) << (n))
#define SIE BIT(1)
#define SPIE BIT(5)
#define SPP BIT(8)
#define SUM BIT(18)
#define MXR BIT(19)
static uint64_t sx(uint64_t x, unsigned bits) {
    uint64_t sign = BIT(bits - 1);
    return (x ^ sign) - sign;
}
static uint64_t word(uint64_t v) { return sx((uint32_t)v, 32); }
void guest_flush(struct guest *g) { memset(g->tlb, 0, sizeof g->tlb); }
void guest_init(struct guest *g, uint8_t *ram, size_t size) {
    memset(g, 0, sizeof *g); g->ram = ram; g->ram_size = size;
    g->pc = GUEST_BASE + GUEST_LOAD; g->privilege = 1;
    g->deadline = UINT64_MAX; g->reservation = UINT64_MAX;
    g->x[11] = GUEST_BASE + size - 65536;
}
static uint8_t *physical(struct guest *g, uint64_t p, size_t n) {
    if (p < GUEST_BASE || p - GUEST_BASE > g->ram_size ||
        n > g->ram_size - (size_t)(p - GUEST_BASE)) return NULL;
    return g->ram + (size_t)(p - GUEST_BASE);
}
bool guest_copy(struct guest *g, uint64_t p, const void *src, size_t n) {
    uint8_t *dst = physical(g, p, n); if (!dst) return false;
    memcpy(dst, src, n); return true;
}
static uint64_t le(const uint8_t *p, unsigned n) {
    uint64_t v = 0; for (unsigned i = 0; i < n; i++) v |= (uint64_t)p[i] << (8*i);
    return v;
}
static void put(uint8_t *p, unsigned n, uint64_t v) {
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8*i));
}
static void trap(struct guest *g, uint64_t cause, uint64_t value) {
    g->epc = g->pc; g->cause = cause; g->tval = value;
    g->status = (g->status & ~(SPIE | SIE | SPP)) |
                ((g->status & SIE) ? SPIE : 0) | (g->privilege ? SPP : 0);
    g->privilege = 1;
    g->pc = g->vector & ~UINT64_C(3);
    if ((cause >> 63) && (g->vector & 3) == 1) g->pc += 4*(cause & 63);
    g->waiting = 0; g->reservation = UINT64_MAX;
    guest_flush(g);
    /* A fault before the guest installs its vector must stop, never execute
     * host address zero or spin forever in an invalid trap-vector loop. */
    if (!g->pc) g->stopped = 2;
}
/* Access: 0 instruction, 1 read, 2 write. Sv39 leaf permissions and canonical
 * addresses are checked before caching; SUM/MXR/privilege changes flush it. */
static bool translate(struct guest *g, uint64_t va, unsigned access, uint64_t *pa) {
    unsigned mode = g->satp >> 60;
    if (!mode) { *pa = va; return true; }
    if (mode != 8 || sx(va & (BIT(39)-1), 39) != va) goto fault;
    uint64_t tag = (va >> 12) * 4 + access;
    struct guest_tlb *t = &g->tlb[(va >> 12) & 255];
    if (t->valid && t->tag == tag) { *pa = t->physical | (va & 4095); return true; }
    uint64_t table = (g->satp & (BIT(44)-1)) << 12;
    for (int level = 2; level >= 0; level--) {
        uint64_t addr = table + ((va >> (12 + 9*level)) & 511) * 8;
        uint8_t *entry = physical(g, addr, 8);
        if (!entry) goto fault;
        uint64_t pte = le(entry, 8), ppn = (pte >> 10) & (BIT(44)-1);
        if (!(pte & 1) || (!(pte & 2) && (pte & 4)) || (pte >> 54)) goto fault;
        if (pte & 10) {
            if ((ppn & (BIT(9*level)-1)) != 0) goto fault;
            if (!g->privilege && !(pte & 16)) goto fault;
            if (g->privilege && (pte & 16) && (!access || !(g->status & SUM))) goto fault;
            if (!access && !(pte & 8)) goto fault;
            if (access == 1 && !(pte & 2) && !((g->status & MXR) && (pte & 8))) goto fault;
            if (access == 2 && !(pte & 4)) goto fault;
            put(entry, 8, pte | 64 | (access == 2 ? 128 : 0));
            *pa = (ppn << 12) | (va & (BIT(12+9*level)-1));
            t->valid = 1; t->tag = tag; t->physical = *pa & ~UINT64_C(4095);
            return true;
        }
        if (pte & (16 | 64 | 128)) goto fault;
        table = ppn << 12;
    }
fault:
    trap(g, access == 0 ? 12 : access == 1 ? 13 : 15, va); return false;
}
static bool memory(struct guest *g, uint64_t addr, unsigned n, unsigned access, uint64_t *value) {
    if (addr & (n-1)) { trap(g, access == 0 ? 0 : access == 1 ? 4 : 6, addr); return false; }
    uint64_t pa;
    if (!translate(g, addr, access, &pa)) return false;
    uint8_t *p = physical(g, pa, n);
    if (!p) { trap(g, access == 0 ? 1 : access == 1 ? 5 : 7, addr); return false; }
    if (access == 2) { put(p, n, *value); g->reservation = UINT64_MAX; }
    else *value = le(p, n);
    return true;
}
static uint64_t mulhi(uint64_t a, uint64_t b) {
    uint64_t al = (uint32_t)a, bl = (uint32_t)b, ah = a >> 32, bh = b >> 32;
    uint64_t low = al*bl, mid = ah*bl + (low >> 32), carry = mid >> 32;
    mid = (uint32_t)mid + al*bh;
    return ah*bh + carry + (mid >> 32);
}
static bool csr(struct guest *g, unsigned id, uint64_t *v, bool write) {
    if (((id >> 8) & 3) > g->privilege || (write && (id >> 10) == 3)) return false;
    uint64_t *p = NULL;
    switch (id) {
    case 0x100: p=&g->status; break; case 0x104: p=&g->ie; break;
    case 0x105: p=&g->vector; break; case 0x106: p=&g->counteren; break;
    case 0x140: p=&g->scratch; break; case 0x141: p=&g->epc; break;
    case 0x142: p=&g->cause; break; case 0x143: p=&g->tval; break;
    case 0x144: p=&g->ip; break; case 0x180: p=&g->satp; break;
    case 0xc00: case 0xc01: case 0xc02:
        if (!g->privilege && !(g->counteren & BIT(id-0xc00))) return false;
        *v = id == 0xc01 ? g->time : g->retired; return true;
    default: return false;
    }
    if (!write) { *v = *p; return true; }
    if (id == 0x100) { *p = (*v & (SIE|SPIE|SPP|SUM|MXR)) | (UINT64_C(2) << 32); guest_flush(g); }
    else if (id == 0x180) { if ((*v >> 60) == 0 || (*v >> 60) == 8) *p = *v; guest_flush(g); }
    else if (id == 0x144) *p = (*p & BIT(5)) | (*v & BIT(1));
    else if (id == 0x104) *p = *v & (BIT(1)|BIT(5)|BIT(9));
    else if (id == 0x105) *p = (*v & ~UINT64_C(3)) | ((*v & 3) == 1);
    else if (id == 0x141) *p = *v & ~UINT64_C(1);
    else *p = *v;
    return true;
}
static void sbi(struct guest *g) {
    uint64_t ext=g->x[17], fn=g->x[16], a=g->x[10];
    int64_t error=0; uint64_t value=0;
    if (ext == 0 || (ext == 0x54494d45 && fn == 0)) {
        g->deadline=a; g->ip &= ~BIT(5);
    } else if (ext == 1) { if(g->output)g->output(g->opaque,(uint8_t)a); }
    else if (ext == 2) { g->x[10]=g->input?(uint64_t)(int64_t)g->input(g->opaque):UINT64_MAX; return; }
    else if (ext == 8 || (ext == 0x53525354 && fn == 0)) g->stopped=1;
    else if (ext == 0x10) {
        if(fn==0)value=0x02000000;
        else if(fn==1)value=0x4d4e;
        else if(fn==2)value=1;
        else if(fn==3)value=(a<=8 || a==0x10 || a==0x54494d45 || a==0x53525354);
        else if(fn>=4 && fn<=6)value=0;
        else error=-2;
    } else if (ext >= 3 && ext <= 7) { if(ext>=5)guest_flush(g); }
    else error=-2;
    g->x[10]=(uint64_t)error;
    if(ext>8)g->x[11]=value;
}
static uint32_t i_ins(unsigned op,unsigned rd,unsigned f,unsigned rs,uint32_t imm) {
    return ((imm&4095)<<20)|(rs<<15)|(f<<12)|(rd<<7)|op;
}
static uint32_t r_ins(unsigned op,unsigned rd,unsigned f,unsigned a,unsigned b,unsigned top) {
    return (top<<25)|(b<<20)|(a<<15)|(f<<12)|(rd<<7)|op;
}
static uint32_t s_ins(unsigned f,unsigned a,unsigned b,unsigned imm) {
    return ((imm>>5)<<25)|(b<<20)|(a<<15)|(f<<12)|((imm&31)<<7)|0x23;
}
/* Decompress RV64C to the same checked 32-bit execution path. Zero means an
 * illegal/reserved encoding; floating point compressed forms are unsupported. */
static uint32_t compressed(uint16_t c) {
    unsigned q=c&3,f=c>>13,rd=(c>>7)&31,rs=(c>>2)&31,a=8+((c>>7)&7),b=8+((c>>2)&7);
    uint32_t imm=(uint32_t)sx(((c>>2)&31)|((c>>7)&32),6);
    if(q==0) {
        if(f==0) {unsigned n=((c>>7)&15)*64+((c>>11)&3)*16+((c>>5)&1)*8+((c>>6)&1)*4;
            return n?i_ins(0x13,b,0,2,n):0;}
        unsigned n=((c>>10)&7)*8+(f==2||f==6?((c>>6)&1)*4+((c>>5)&1)*64:((c>>5)&3)*64);
        if(f==2||f==3)return i_ins(3,b,f, a,n);
        if(f==6||f==7)return s_ins(f-4,a,b,n);
    } else if(q==1) {
        if(f==0)return i_ins(0x13,rd,0,rd,imm);
        if(f==1)return rd?i_ins(0x1b,rd,0,rd,imm):0;
        if(f==2)return i_ins(0x13,rd,0,0,imm);
        if(f==3) {
            if(rd==2) {unsigned n=((c>>12)&1)*512+((c>>6)&1)*16+((c>>5)&1)*64+((c>>3)&3)*128+((c>>2)&1)*32;
                return n?i_ins(0x13,2,0,2,(uint32_t)sx(n,10)):0;}
            return rd&&imm?((imm&0xfffff)<<12)|(rd<<7)|0x37:0;
        }
        if(f==4) {
            unsigned sub=(c>>10)&3;
            if(sub<2)return i_ins(0x13,a,5,a,(imm&63)+(sub==1?1024:0));
            if(sub==2)return i_ins(0x13,a,7,a,imm);
            unsigned sel=(c>>5)&3;
            if(c&4096)return sel<2?r_ins(0x3b,a,0,a,b,sel==0?32:0):0;
            unsigned fun[]={0,4,6,7};return r_ins(0x33,a,fun[sel],a,b,sel==0?32:0);
        }
        if(f==5) {
            uint32_t n=((c>>12)&1)*2048+((c>>11)&1)*16+((c>>9)&3)*256+((c>>8)&1)*1024+
                ((c>>7)&1)*64+((c>>6)&1)*128+((c>>3)&7)*2+((c>>2)&1)*32;
            n=(uint32_t)sx(n,12);
            return ((n&0x100000)<<11)|((n&0x7fe)<<20)|((n&0x800)<<9)|(n&0xff000)|0x6f;
        }
        if(f>=6) {
            uint32_t n=((c>>12)&1)*256+((c>>10)&3)*8+((c>>5)&3)*64+((c>>3)&3)*2+((c>>2)&1)*32;
            n=(uint32_t)sx(n,9);
            return ((n&0x1000)<<19)|((n&0x7e0)<<20)|(a<<15)|((f-6)<<12)|((n&30)<<7)|((n&0x800)>>4)|0x63;
        }
    } else if(q==2) {
        if(f==0)return i_ins(0x13,rd,1,rd,imm&63);
        if(f==2||f==3) {
            unsigned n=f==2?((c>>12)&1)*32+((c>>4)&7)*4+((c>>2)&3)*64:
                ((c>>12)&1)*32+((c>>5)&3)*8+((c>>2)&7)*64;
            return rd?i_ins(3,rd,f,2,n):0;
        }
        if(f==4) {
            if(!(c&4096))return rs?r_ins(0x33,rd,0,0,rs,0):rd?i_ins(0x67,0,0,rd,0):0;
            if(rs)return r_ins(0x33,rd,0,rd,rs,0);
            return rd?i_ins(0x67,1,0,rd,0):0x00100073;
        }
        if(f==6)return s_ins(2,2,rs,((c>>9)&15)*4+((c>>7)&3)*64);
        if(f==7)return s_ins(3,2,rs,((c>>10)&7)*8+((c>>7)&7)*64);
    }
    return 0;
}
void guest_run(struct guest *g, unsigned budget) {
    while (budget-- && !g->stopped) {
        g->time++; g->retired++;
        if(g->time>=g->deadline)g->ip|=BIT(5);
        if((g->ie&g->ip&BIT(5)) && (!g->privilege || (g->status&SIE))) {trap(g,BIT(63)|5,0);continue;}
        if(g->waiting) {
            /* WFI wakes on a locally enabled interrupt even while global SIE
             * is clear. Linux reenables SIE AFTER WFI; gating wake on SIE
             * deadlocks its idle loop and the HVC input polling worker. */
            if(g->ie&g->ip)g->waiting=0;
            else {if(g->deadline!=UINT64_MAX && g->time<g->deadline)g->time=g->deadline;continue;}
        }
        uint64_t raw=0;
        if(!memory(g,g->pc,2,0,&raw))continue;
        unsigned size=2; uint32_t ins;
        if((raw&3)==3) {
            uint64_t high=0; if(!memory(g,g->pc+2,2,0,&high))continue;
            ins=(uint32_t)(raw|(high<<16));size=4;
        } else ins=compressed((uint16_t)raw);
        uint64_t next=g->pc+size, result=0, a=g->x[(ins>>15)&31], b=g->x[(ins>>20)&31];
        unsigned op=ins&127,rd=(ins>>7)&31,f=(ins>>12)&7,top=ins>>25;
        uint64_t imm=sx(ins>>20,12);bool write=true, bad=false;
        switch(op) {
        case 0x37:result=word(ins&0xfffff000);break;
        case 0x17:result=g->pc+word(ins&0xfffff000);break;
        case 0x6f:result=next;next=g->pc+sx(((uint64_t)(ins>>31)<<20)|(ins&0xff000)|((ins>>9)&0x800)|((ins>>20)&0x7fe),21);break;
        case 0x67:result=next;next=(a+imm)&~UINT64_C(1);bad=f!=0;break;
        case 0x63: {
            write=false;bool take=false;
            if(f==0)take=a==b;else if(f==1)take=a!=b;else if(f==4)take=(int64_t)a<(int64_t)b;
            else if(f==5)take=(int64_t)a>=(int64_t)b;else if(f==6)take=a<b;else if(f==7)take=a>=b;else bad=true;
            if(take)next=g->pc+sx(((uint64_t)(ins>>31)<<12)|((ins<<4)&0x800)|((ins>>20)&0x7e0)|((ins>>7)&30),13);
            break;
        }
        case 3: {
            unsigned n=1u<<(f&3);if(f==7){bad=true;break;}
            if(!memory(g,a+imm,n,1,&result))continue;
            if(f<4&&n<8)result=sx(result,n*8);break;
        }
        case 0x23: {
            write=false;if(f>3){bad=true;break;}
            uint64_t value=b,addr=a+sx(((ins>>20)&0xfe0)|((ins>>7)&31),12);
            if(!memory(g,addr,1u<<f,2,&value))continue;break;
        }
        case 0x13: case 0x1b: {
            unsigned sh=(ins>>20)&(op==0x1b?31:63);
            if(f==0)result=a+imm;
            else if(f==2&&op==0x13)result=(int64_t)a<(int64_t)imm;
            else if(f==3&&op==0x13)result=a<imm;
            else if(f==4&&op==0x13)result=a^imm;
            else if(f==6&&op==0x13)result=a|imm;
            else if(f==7&&op==0x13)result=a&imm;
            else if(f==1) {bad=op==0x1b?top!=0:(top&~1u)!=0;result=a<<sh;}
            else if(f==5) {unsigned t=op==0x1b?top:top&~1u;bad=t!=0&&t!=32;
                uint64_t v=op==0x1b?word(a):a;
                result=t==32?(uint64_t)((int64_t)v>>sh):(op==0x1b?(uint32_t)a:a)>>sh;}
            else bad=true;
            if(op==0x1b)result=word(result);break;
        }
        case 0x33: case 0x3b: {
            bool w=op==0x3b; unsigned sh=b&(w?31:63);
            if(w){a=word(a);b=word(b);}
            if(top==1) {
                if(f==0)result=a*b;
                else if(f<4&&!w){result=mulhi(a,b);if(f<=2&&(int64_t)a<0)result-=b;if(f==1&&(int64_t)b<0)result-=a;}
                else if(f==4)result=!b?UINT64_MAX:a==BIT(63)&&b==UINT64_MAX?a:(uint64_t)((int64_t)a/(int64_t)b);
                else if(f==5) {if(w){a=(uint32_t)a;b=(uint32_t)b;}result=b?a/b:UINT64_MAX;}
                else if(f==6)result=!b?a:a==BIT(63)&&b==UINT64_MAX?0:(uint64_t)((int64_t)a%(int64_t)b);
                else if(f==7) {if(w){a=(uint32_t)a;b=(uint32_t)b;}result=b?a%b:a;}
                else bad=true;
            } else if(top==0||top==32) {
                if(f==0)result=top?a-b:a+b;
                else if(f==5)result=top?(uint64_t)((int64_t)a>>sh):(w?(uint32_t)a:a)>>sh;
                else if(top)bad=true;
                else if(f==1)result=a<<sh;
                else if(!w&&f==2)result=(int64_t)a<(int64_t)b;
                else if(!w&&f==3)result=a<b;
                else if(!w&&f==4)result=a^b;
                else if(!w&&f==6)result=a|b;
                else if(!w&&f==7)result=a&b;
                else bad=true;
            } else bad=true;
            if(w)result=word(result);break;
        }
        case 0x0f: write=false;bad=f>1;break;
        case 0x2f: {
            unsigned n=f==2?4:8,fun=ins>>27;
            if(f!=2&&f!=3){bad=true;break;}
            /* AMOs require store permission even for their read half. */
            uint64_t pa;
            if(fun!=2 && !translate(g,a,2,&pa))continue;
            if(!memory(g,a,n,1,&result))continue;
            if(!translate(g,a,fun==2?1:2,&pa))continue;
            if(fun==2){if((ins>>20)&31){bad=true;break;}g->reservation=pa;}
            else {
                uint64_t v=b;
                if(fun==3){bool ok=g->reservation==pa;g->reservation=UINT64_MAX;result=ok?0:1;if(!ok)break;}
                else if(fun==0)v=result+b;
                else if(fun==1)v=b;
                else if(fun==4)v=result^b;
                else if(fun==8)v=result|b;
                else if(fun==12)v=result&b;
                else if(fun==16)v=(int64_t)(n==4?word(result):result)<(int64_t)(n==4?word(b):b)?result:b;
                else if(fun==20)v=(int64_t)(n==4?word(result):result)>(int64_t)(n==4?word(b):b)?result:b;
                else if(fun==24)v=result<(n==4?(uint32_t)b:b)?result:b;
                else if(fun==28)v=result>(n==4?(uint32_t)b:b)?result:b;
                else {bad=true;break;}
                if(!memory(g,a,n,2,&v))continue;
            }
            if(n==4)result=word(result);break;
        }
        case 0x73:
            if(!f) {
                write=false;
                if(ins==0x73){if(g->privilege){sbi(g);}else{trap(g,8,0);continue;}}
                else if(ins==0x00100073){trap(g,3,g->pc);continue;}
                else if(ins==0x10200073&&g->privilege){next=g->epc;g->privilege=(g->status&SPP)?1:0;
                    g->status=(g->status&~(SIE|SPP))|((g->status&SPIE)?SIE:0)|SPIE;guest_flush(g);}
                else if(ins==0x10500073&&g->privilege)g->waiting=1;
                else if((ins&0xfe007fff)==0x12000073&&g->privilege)guest_flush(g);
                else bad=true;
            } else {
                unsigned id=ins>>20,action=f&3;uint64_t value=f&4?(ins>>15)&31:a;
                bool changes=action==1||((ins>>15)&31)!=0;
                if(!action||!csr(g,id,&result,false)){bad=true;break;}
                if(changes){if(action==2)value=result|value;else if(action==3)value=result&~value;
                    if(!csr(g,id,&value,true))bad=true;}
            }break;
        default:bad=true;break;
        }
        if(bad){trap(g,2,size==2?raw:ins);continue;}
        if(next&1){trap(g,0,next);continue;}
        if(write&&rd)g->x[rd]=result;
        g->x[0]=0;g->pc=next;
    }
}
