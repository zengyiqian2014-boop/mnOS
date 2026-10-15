/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "auth.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct auth_database saved;
static bool exists,save_fail,random_fail;
static unsigned salt_byte;
static uint64_t seconds=100;
static char output[160];
static int load(struct auth_database *db) {if(!exists)return 1;*db=saved;return 0;}
static int save(const struct auth_database *db) {if(save_fail)return -1;saved=*db;exists=true;return 0;}
static bool random_bytes(uint8_t *p,size_t n) {if(random_fail)return false;while(n--)*p++=(uint8_t)++salt_byte;return true;}
static void print(const char *s) {snprintf(output,sizeof output,"%s",s);}
static void input(const char *s) {while(*s)auth_key(*s++);auth_key('\n');}
static void finish(void) {
    for(unsigned i=0;i<700&&strstr(auth_prompt(),"Processing");++i)auth_tick(seconds);
    assert(!strstr(auth_prompt(),"Processing"));
}
static void new_password(const char *s) {input(s);input(s);finish();}
static void command(const char *s) {assert(auth_command(s,print));}
static void login(const char *name,const char *password) {input(name);input(password);finish();}
static void hex(const uint8_t *p,char *s) {for(unsigned i=0;i<32;++i)sprintf(s+i*2,"%02x",p[i]);}
int main(void) {
    uint8_t digest[32];char text[65];sha256_digest("abc",3,digest);hex(digest,text);
    assert(!strcmp(text,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    unsigned rounds[]={1,2,4096};const char *expected[]={
        "ef9d5f6add4a5d19f4a7fc92b48f2351ea95bb977642c0071ed4e4010a42cb6c",
        "4536427916ecc4db17ff9702daac1a9a755fb17f9a544ebc4e9dc4ce1689112e",
        "615eeee652a9fe1cf9f427d6218526a937add018079854b2aa11875d759837ac"};
    for(unsigned i=0;i<3;++i) {struct password_kdf k;password_begin(&k,"password",(const uint8_t *)"0123456789abcdef",rounds[i]);
        while(!password_step(&k,73)){}hex(k.result,text);assert(!strcmp(text,expected[i]));}
    char long_password[101];memset(long_password,'a',100);long_password[100]=0;
    struct password_kdf long_key;password_begin(&long_key,long_password,(const uint8_t *)"0123456789abcdef",4096);
    while(!password_step(&long_key,73)){}hex(long_key.result,text);
    assert(!strcmp(text,"1bcd073e6448fe006604d2fcc50ecbb6ae4265a6a8ca62b767ce008c9b44cdd2"));
    const struct auth_io io={load,save,random_bytes,true};auth_init(&io);auth_tick(seconds);
    assert(!auth_logged_in()&&!auth_admin()&&!auth_path("/home/readme.txt",false));
    input("root");assert(strstr(auth_status(),"valid ordinary"));input("alice");
    input("short");assert(strstr(auth_status(),"12-128"));
    input("Ordinary-test-491");input("not-the-same");assert(strstr(auth_status(),"did not match"));
    new_password("Ordinary-test-491");new_password("Root-admin-test-785");
    assert(auth_logged_in()&&!auth_admin()&&auth_uid()==1000&&exists);
    assert(saved.rounds==600000&&!memcmp(saved.users[1].name,"alice",6));
    assert(memcmp(saved.users[0].salt,saved.users[1].salt,16));
    assert(auth_path("/home/alice/file",true)&&auth_path("/home/alice/file",false));
    assert(!auth_path("/home/alice",true)&&!auth_path("/home/bob/file",false));
    assert(!auth_path("/home/alice/../bob/file",true)&&!auth_path("/home/alice//file",true));
    assert(!auth_path("/system/accounts.bin",false)&&!auth_path("/root/file",false));
    assert(!auth_path("/file",true)&&auth_path("/home/readme.txt",false));
    command("useradd bob");assert(strstr(output,"Permission denied"));
    command("passwd root");assert(strstr(output,"Permission denied"));
    command("su");
    for(const char *p="wrong-password-111";*p;++p)auth_key(*p);
    assert(!strstr(auth_input(),"wrong"));auth_key('\n');finish();
    assert(!auth_admin());command("su");assert(strstr(output,"Wait"));
    seconds+=40;auth_tick(seconds);command("su");input("Root-admin-test-785");finish();assert(auth_admin());
    assert(auth_path("/root/file",true)&&!auth_path("/system/accounts.bin",true));
    command("useradd bob");new_password("Second-user-test-541");assert(saved.count==3);
    command("exit");assert(!auth_admin()&&!strcmp(auth_name(),"alice"));
    command("passwd");input("Ordinary-test-491");finish();save_fail=true;
    new_password("Replacement-test-394");assert(strstr(auth_status(),"save failed"));save_fail=false;
    command("logout");assert(!auth_logged_in());login("alice","Ordinary-test-491");assert(auth_logged_in());
    command("passwd");input("Ordinary-test-491");finish();new_password("Replacement-test-394");
    auth_init(&io);auth_tick(seconds);login("alice","Replacement-test-394");assert(auth_logged_in());
    command("logout");login("bob","Second-user-test-541");assert(auth_uid()==1001);
    assert(auth_path("/home/bob/data",true)&&!auth_path("/home/alice/file",false));
    command("logout");login("root","Root-admin-test-785");assert(auth_admin());
    seconds+=301;auth_tick(seconds);assert(!auth_logged_in());
    saved.users[1].uid=0;auth_init(&io);assert(!auth_logged_in()&&strstr(auth_status(),"damaged"));
    exists=false;auth_init(&io);random_fail=true;input("alice");new_password("Ordinary-test-491");
    assert(!auth_logged_in()&&strstr(auth_status(),"random source unavailable"));
    puts("AUTH PASS: SHA/PBKDF2 vectors, enrollment, masked input, login, root separation, homes, retries, password changes, persistence, expiry and fail-closed storage/randomness");
}
