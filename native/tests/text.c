/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "font.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    /* Narrow letters must not be padded to the width of W. */
    assert(text_width("iiii",18) < text_width("WWWW",18)/2);
    assert(text_width("Hello",18) < 50);
    assert(text_width("iiii",FONT_MONO18) == text_width("WWWW",FONT_MONO18));
    assert(text_width("ii WW",18) > text_width("iiWW",18));
    assert(font_find('i',18)->advance % 64); /* Keep fractional advances. */
    const char *s="iiii WWWW\tHello\n显示设置\nLast line";
    size_t length=strlen(s);
    struct text_layout l={.s=s,.length=length,.width=80,.size=18};
    struct text_cell c;
    size_t previous=0, cells=0;
    while(text_next(&l,&c)) {
        assert(c.row >= previous); previous=c.row;
        /* Every rendered boundary maps back to the same UTF-8 byte. */
        struct text_cell caret=text_caret(s,length,c.byte,80);
        assert(caret.x == c.x && caret.row == c.row);
        assert(text_hit(s,length,c.row,c.x,80) == c.byte);
        if(c.byte) assert(text_previous(s,c.byte) < c.byte);
        cells++;
    }
    assert(cells > 20 && cells < length && previous >= 3);
    assert(c.byte == length);
    struct text_cell tab=text_caret("i\tW",3,2,500);
    assert(tab.x == font_find(' ',18)->advance * 4);
    const char *wide="显示W";
    assert(text_previous(wide,6) == 3);
    size_t p=0;
    assert(font_decode(wide,strlen(wide),&p) == 0x663e && p == 3);
    assert(font_decode("\xf0\x9f\x98\x80",4,&(size_t){0}) == 0x1f600);
    const char invalid[]={ (char)0xf0, (char)0x80, (char)0x80, 'X', 0 };
    p=0; assert(font_decode(invalid,4,&p)=='?' && p==1);
    p=0; assert(font_decode("\xe4",1,&p)=='?' && p==1);
    struct text_cell blank=text_caret("",0,0,80);
    assert(blank.byte==0 && blank.x==0 && blank.row==0);
    assert(text_hit("abc",3,100,0,50)==3);
    puts("TEXT PASS: proportional/monospace, UTF-8, tabs, wrapping, caret and pointer alignment");
}
