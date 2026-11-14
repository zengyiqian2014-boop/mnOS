/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "auth.h"
#include "clock.h"
#include <assert.h>
#include <openssl/hmac.h>
#include <stdio.h>
#include <string.h>
static struct auth_database saved;
static bool exists,save_fail,random_fail,clock_fail;
static uint64_t mono=100,utc=1800000000;
static unsigned entropy;
static char output[256];
static int load(struct auth_database *out) {if(!exists)return 1;*out=saved;return 0;}
static int save(const struct auth_database *in) {if(save_fail)return -1;saved=*in;exists=true;return 0;}
static bool random_bytes(uint8_t *p,size_t n) {if(random_fail)return false;while(n--)*p++=(uint8_t)++entropy;return true;}
static bool wall(uint64_t *out) {*out=utc;return !clock_fail;}
static const struct auth_io io={load,save,random_bytes,true,wall};
static void print(const char *s) {snprintf(output,sizeof output,"%s",s);}
static void command(const char *s) {assert(auth_command(s,print));}
static void input(const char *s) {while(*s)auth_key(*s++);auth_key('\n');}
static void finish(void) {for(unsigned i=0;i<700&&strstr(auth_prompt(),"Processing");++i)auth_tick(mono);assert(!strstr(auth_prompt(),"Processing"));}
static void password(const char *s) {input(s);input(s);finish();}
static void login(const char *pass) {input("alice");input(pass);finish();}
static void wait_retry(void) {mono+=40;auth_tick(mono);}
static void decode(const char *s,uint8_t key[32]) {
    const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";uint32_t v=0;unsigned bits=0,at=0;
    while(*s) {const char *p=strchr(alphabet,*s++);assert(p);v=(v<<5)|(unsigned)(p-alphabet);bits+=5;if(bits>=8) {bits-=8;assert(at<32);key[at++]=(uint8_t)(v>>bits);}}
    assert(at==32);
}
/* Independent OpenSSL oracle for production TOTP, not a copy of its code. */
static void code(const uint8_t key[32],char out[7]) {
    uint64_t step=utc/30;uint8_t counter[8],mac[EVP_MAX_MD_SIZE];unsigned length=0;
    for(unsigned i=0;i<8;++i)counter[7-i]=(uint8_t)(step>>(i*8));
    assert(HMAC(EVP_sha1(),key,32,counter,8,mac,&length)&&length==20);
    unsigned o=mac[19]&15;uint32_t n=((uint32_t)(mac[o]&127)<<24)|((uint32_t)mac[o+1]<<16)|((uint32_t)mac[o+2]<<8)|mac[o+3];
    snprintf(out,7,"%06u",n%1000000);auth_wipe(mac,sizeof mac);
}
static void factor_input(const uint8_t key[32]) {char otp[7];code(key,otp);input(otp);}
static void primitive_tests(void) {
    const char *key="12345678901234567890";
    const unsigned hotp[]={755224,287082,359152,969429,338314,254676,287922,162583,399871,520489};
    for(unsigned i=0;i<10;++i)assert(factor_hotp(key,20,i,6)==hotp[i]);
    const uint64_t times[]={59,1111111109,1111111111,1234567890,2000000000,20000000000ULL};
    const unsigned expected[]={94287082,7081804,14050471,89005924,69279037,65353130};
    for(unsigned i=0;i<6;++i)assert(factor_hotp(key,20,times[i]/30,8)==expected[i]);
    uint8_t secret[32],wrapping[32],opened[32];assert(random_bytes(secret,32)&&random_bytes(wrapping,32));
    struct factor_record r={0};assert(random_bytes(r.salt,16));
    assert(factor_seal(&r,1000,wrapping,secret,random_bytes));
    assert(factor_open(&r,1000,wrapping,opened)&&!memcmp(secret,opened,32));
    assert(!memmem(&r,sizeof r,secret,32));assert(!factor_open(&r,0,wrapping,opened));
    for(size_t i=0;i<sizeof r;++i) {
        /* Padding is not protocol data. Test every authenticated field separately. */
        struct factor_record bad=r;
        if(i<4||(i>=4&&i<92)||(i>=offsetof(struct factor_record,last_step)&&i<sizeof r)) {
            ((uint8_t *)&bad)[i]^=1;assert(!factor_open(&bad,1000,wrapping,opened));
        }
    }
    uint64_t epoch;assert(clock_calendar(2024,2,29,0,0,0,&epoch)&&epoch==1709164800);
    assert(!clock_calendar(2023,2,29,0,0,0,&epoch));
    assert(clock_calendar(2026,1,1,0,0,0,&epoch)&&epoch==1767225600);
    puts("PASS: RFC 4226/6238 vectors, AEAD tamper checks, UID binding and UTC calendar");
}
int main(void) {
    primitive_tests();auth_init(&io);auth_tick(mono);
    input("alice");password("Ordinary-test-491");password("Root-admin-test-785");
    assert(auth_logged_in()&&!auth_factor_enabled());
    struct old_user {uint32_t uid;char name[32];uint8_t salt[16],hash[32];};
    struct old_database {uint32_t magic,version,rounds,count;struct old_user users[AUTH_USERS];} old={0};
    old.magic=saved.magic;old.version=1;old.rounds=saved.rounds;old.count=saved.count;
    for(unsigned i=0;i<AUTH_USERS;++i)memcpy(&old.users[i],&saved.users[i],sizeof old.users[i]);
    struct auth_database migrated;assert(auth_decode_database(&old,sizeof old,&migrated));
    assert(migrated.version==2&&migrated.count==saved.count&&!memcmp(migrated.users[1].hash,saved.users[1].hash,32));
    for(unsigned i=0;i<AUTH_USERS;++i)assert(!migrated.users[i].factor.enabled);
    assert(!auth_decode_database(&old,sizeof old-1,&migrated));auth_wipe(&old,sizeof old);auth_wipe(&migrated,sizeof migrated);
    assert(strstr(auth_prompt(),"Enable an authenticator"));input("y");
    assert(!*auth_setup_secret());input("Ordinary-test-491");finish();
    assert(strlen(auth_setup_secret())==52&&strstr(auth_setup_uri(),"algorithm=SHA1"));
    uint8_t secret[32];decode(auth_setup_secret(),secret);factor_input(secret);
    assert(saved.users[1].factor.enabled&&auth_factor_enabled());
    char recovery[FACTOR_RECOVERY][33];
    for(unsigned i=0;i<FACTOR_RECOVERY;++i) {assert(strlen(auth_recovery_code())==32);strcpy(recovery[i],auth_recovery_code());input("");}
    assert(!auth_prompting()&&!*auth_setup_secret()&&!*auth_setup_uri()&&!*auth_recovery_code());
    assert(!memmem(&saved,sizeof saved,secret,32));
    command("lock");login("Ordinary-test-491");assert(!auth_logged_in()&&strstr(auth_prompt(),"Authenticator"));
    factor_input(secret);assert(!auth_logged_in()); /* enrollment code cannot be replayed */
    wait_retry();utc+=30;login("Ordinary-test-491");factor_input(secret);assert(auth_logged_in());
    command("lock");login("Ordinary-test-491");input(recovery[0]);assert(auth_logged_in());
    command("lock");login("Ordinary-test-491");input(recovery[0]);assert(!auth_logged_in());
    wait_retry();login("wrong-password");assert(!auth_logged_in()&&!strstr(auth_prompt(),"Authenticator"));
    wait_retry();login("Ordinary-test-491");input(recovery[1]);assert(auth_logged_in());
    command("passwd");input("Ordinary-test-491");finish();utc+=30;factor_input(secret);
    assert(strstr(auth_prompt(),"New password"));password("Changed-password-459");
    command("lock");login("Changed-password-459");utc+=30;factor_input(secret);assert(auth_logged_in());
    command("lock");clock_fail=true;login("Changed-password-459");input(recovery[2]);assert(auth_logged_in());clock_fail=false;
    command("lock");login("Changed-password-459");struct auth_database before=saved;save_fail=true;utc+=30;factor_input(secret);
    assert(!auth_logged_in()&&!memcmp(&before,&saved,sizeof before));save_fail=false;
    wait_retry();login("Changed-password-459");factor_input(secret);assert(auth_logged_in());
    command("lock");login("Changed-password-459");mono+=181;auth_tick(mono);assert(!auth_logged_in()&&!strstr(auth_prompt(),"Authenticator"));
    wait_retry();login("Changed-password-459");input(recovery[3]);assert(auth_logged_in());
    command("2fa disable");input("Changed-password-459");finish();utc+=30;factor_input(secret);assert(!auth_factor_enabled());
    command("lock");login("Changed-password-459");assert(auth_logged_in()&&!auth_prompting());
    command("2fa enable");input("Changed-password-459");finish();assert(*auth_setup_secret());auth_key(27);assert(!*auth_setup_secret()&&!*auth_setup_uri()&&!auth_factor_enabled());
    puts("PASS: password-first enrollment, independent OTP oracle, persistent replay defense, one-use recovery, password rewrap, timeout, cancellation, disable and fail-closed writes");
    return 0;
}
