/* SPDX-License-Identifier: GPL-3.0-or-later
 * Local-console accounts. All exposed filesystem and UI entry points check this
 * session; ring-3 probes have no filesystem/account syscalls. No Linux ABI claim.
 */
#include "auth.h"
#include <string.h>
#define AUTH_MAGIC 0x55414e4dU
#define NO_USER UINT32_MAX
enum state { SETUP_NAME, LOGIN_NAME, PASSWORD, NEW_PASSWORD, CONFIRM, HASHING, READY, BROKEN };
enum action { LOGIN, SU, ENROLL_USER, ENROLL_ROOT, ADD_USER, CHANGE, VERIFY_CHANGE };
static struct auth_io io;
static struct auth_database db, pending;
static struct password_kdf kdf;
static enum state state = BROKEN;
static enum action action;
static uint32_t session = NO_USER, previous = NO_USER;
static unsigned target, failures;
static bool known;
static uint64_t now, retry_after, root_until;
static char input[129], first[129], masked[129];
static unsigned used;
static const char *status = "Initializing accounts...";
static bool equal(const char *a,const char *b) { size_t n=strlen(a);return n==strlen(b)&&!memcmp(a,b,n); }
static bool same_hash(const uint8_t *a,const uint8_t *b) { uint8_t d=0;for(unsigned i=0;i<32;++i)d|=a[i]^b[i];return d==0; }
static void clear_input(void) { auth_wipe(input,sizeof input);auth_wipe(masked,sizeof masked);used=0; }
static void next(enum state s,const char *message) { clear_input();state=s;status=message; }
static bool username(const char *s) {
    size_t n=0;while(n<32&&s[n])++n;
    if(!n||n==32||s[0]<'a'||s[0]>'z')return false;
    for(size_t i=1;i<n;++i)if(!((s[i]>='a'&&s[i]<='z')||(s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-'))return false;
    return true;
}
static int find(const char *s) { for(unsigned i=0;i<db.count;++i)if(equal(s,db.users[i].name))return (int)i;return -1; }
static bool valid_database(void) {
    if(db.magic!=AUTH_MAGIC||db.version!=1||db.rounds!=AUTH_ROUNDS||db.count<2||db.count>AUTH_USERS)return false;
    if(db.users[0].uid!=0||!username(db.users[0].name)||!equal(db.users[0].name,"root"))return false;
    for(unsigned i=0;i<db.count;++i) {
        if(!username(db.users[i].name)||(i&&db.users[i].uid<1000))return false;
        for(unsigned j=0;j<i;++j)if(db.users[i].uid==db.users[j].uid||equal(db.users[i].name,db.users[j].name))return false;
    }
    return true;
}
void auth_lock(void) {
    session=previous=NO_USER;root_until=0;
    auth_wipe(&kdf,sizeof kdf);auth_wipe(&pending,sizeof pending);auth_wipe(first,sizeof first);
    next(LOGIN_NAME,"Sign in to mnOS.");
}
void auth_init(const struct auth_io *callbacks) {
    io=*callbacks;session=previous=NO_USER;failures=0;now=retry_after=root_until=0;
    auth_wipe(&db,sizeof db);auth_wipe(&pending,sizeof pending);auth_wipe(&kdf,sizeof kdf);auth_wipe(first,sizeof first);
    int result=io.load(&db);
    if(result==0&&valid_database()) { auth_lock();return; }
    if(result!=1) { next(BROKEN,"Account store unavailable or damaged. Access remains locked.");return; }
    memset(&pending,0,sizeof pending);pending.magic=AUTH_MAGIC;pending.version=1;pending.rounds=AUTH_ROUNDS;pending.count=2;
    memcpy(pending.users[0].name,"root",5);pending.users[1].uid=1000;
    next(SETUP_NAME,io.persistent?"First boot: create your user and root passwords.":"No MNFS disk: accounts last for this boot only.");
}
bool auth_logged_in(void) { return session!=NO_USER; }
bool auth_admin(void) { return auth_logged_in()&&db.users[session].uid==0; }
uint32_t auth_uid(void) { return auth_logged_in()?db.users[session].uid:NO_USER; }
const char *auth_name(void) { return auth_logged_in()?db.users[session].name:"locked"; }
bool auth_prompting(void) { return state!=READY; }
const char *auth_status(void) { return status; }
const char *auth_prompt(void) {
    if(state==SETUP_NAME)return "New username (a-z, 0-9, _ or -):";
    if(state==LOGIN_NAME)return "Username:";
    if(state==PASSWORD)return action==SU?"Root password:":"Password:";
    if(state==NEW_PASSWORD)return action==ENROLL_ROOT?"Create root password (12+ characters):":"New password (12+ characters):";
    if(state==CONFIRM)return "Confirm new password:";
    if(state==HASHING)return "Processing password...";
    return "";
}
const char *auth_input(void) {
    if(state==SETUP_NAME||state==LOGIN_NAME)return input;
    memset(masked,'*',used);masked[used]=0;return masked;
}
static void signed_in(unsigned index) {
    session=index;failures=0;retry_after=0;root_until=index==0?now+300:0;
    next(READY,index==0?"Root session: expires after 5 minutes.":"Signed in as an ordinary user.");
}
static void failed(void) {
    if(failures<5)++failures;retry_after=now+(1u<<failures);
    auth_wipe(&kdf,sizeof kdf);auth_wipe(first,sizeof first);
    next(auth_logged_in()?READY:LOGIN_NAME,"Authentication failed. Wait before retrying.");
}
static void start_hash(bool verify) {
    struct auth_user *user=verify?&db.users[target]:&pending.users[target];
    if(!verify&&!io.random(user->salt,sizeof user->salt)) {
        auth_wipe(first,sizeof first);
        next(auth_logged_in()?READY:BROKEN,"No secure RNG. Enable a virtual RNG device or CPU RNG, then reboot.");return;
    }
    password_begin(&kdf,verify?input:first,user->salt,AUTH_ROUNDS);
    auth_wipe(first,sizeof first);next(HASHING,"Password hashing: PBKDF2-HMAC-SHA256, 600000 iterations.");
}
void auth_tick(uint64_t seconds) {
    now=seconds;
    if(auth_admin()&&now>=root_until) { auth_lock();status="Root session expired. Sign in again.";return; }
    if(state!=HASHING||!password_step(&kdf,1024))return;
    if(action==LOGIN||action==SU||action==VERIFY_CHANGE) {
        bool ok=same_hash(kdf.result,db.users[target].hash)&&known;auth_wipe(&kdf,sizeof kdf);
        if(!ok) { failed();return; }
        if(action==VERIFY_CHANGE) { pending=db;action=CHANGE;next(NEW_PASSWORD,"Choose a new account password.");return; }
        if(action==SU)previous=session;else previous=NO_USER;
        signed_in(target);return;
    }
    memcpy(pending.users[target].hash,kdf.result,32);auth_wipe(&kdf,sizeof kdf);
    if(action==ENROLL_USER) { action=ENROLL_ROOT;target=0;next(NEW_PASSWORD,"Set a separate password for root administration.");return; }
    if(io.save(&pending)) {
        auth_wipe(&pending,sizeof pending);
        next(auth_logged_in()?READY:BROKEN,"Account save failed. Previous credentials were not replaced in memory.");return;
    }
    db=pending;auth_wipe(&pending,sizeof pending);
    if(action==ENROLL_ROOT) { signed_in(1);status="Accounts created. Signed in as the ordinary user."; }
    else next(READY,"Account password saved.");
}
void auth_key(char c) {
    if(state==READY||state==BROKEN)return;
    if(c==27) {
        if(db.count) { auth_wipe(&kdf,sizeof kdf);auth_wipe(&pending,sizeof pending);auth_wipe(first,sizeof first);
            next(auth_logged_in()?READY:LOGIN_NAME,"Authentication cancelled."); }
        return;
    }
    if(state==HASHING)return;
    if(c=='\b'||c==127) { if(used)input[--used]=0;return; }
    if(c!='\n'&&c!='\r') { if(c>=32&&c<127&&used<128) {input[used++]=c;input[used]=0;}return; }
    if(now<retry_after) { clear_input();status="Too many attempts. Wait before retrying.";return; }
    if(state==SETUP_NAME) {
        if(!username(input)||equal(input,"root")) {next(SETUP_NAME,"Choose a valid ordinary username, not root.");return;}
        memcpy(pending.users[1].name,input,used+1);target=1;action=ENROLL_USER;
        next(NEW_PASSWORD,"Use 12-128 characters and at least two character types.");
    } else if(state==LOGIN_NAME) {
        int index=username(input)?find(input):-1;known=index>=0;target=known?(unsigned)index:0;action=LOGIN;
        next(PASSWORD,"Enter your account password.");
    } else if(state==PASSWORD)start_hash(true);
    else if(state==NEW_PASSWORD) {
        unsigned types=0;
        for(unsigned i=0;i<used;++i)types|=input[i]>='a'&&input[i]<='z'?1u:input[i]>='A'&&input[i]<='Z'?2u:input[i]>='0'&&input[i]<='9'?4u:8u;
        if(used<12||!(types&(types-1))) {next(NEW_PASSWORD,"Use 12-128 characters and at least two character types.");return;}
        memcpy(first,input,used+1);next(CONFIRM,"Repeat the new password.");
    } else if(state==CONFIRM) {
        if(!equal(input,first)) {auth_wipe(first,sizeof first);next(NEW_PASSWORD,"Passwords did not match.");return;}
        start_hash(false);
    }
}
bool auth_command(const char *command,void (*print)(const char *)) {
    if(equal(command,"whoami")) {print(auth_name());return true;}
    if(equal(command,"logout")||equal(command,"lock")) {auth_lock();print("Session locked.");return true;}
    if(equal(command,"exit")&&auth_admin()&&previous!=NO_USER) {unsigned user=previous;previous=NO_USER;signed_in(user);print("Returned to ordinary user.");return true;}
    if(equal(command,"su")) {
        if(!auth_logged_in()) {print("Sign in first.");return true;}
        if(auth_admin()) {print("Already root.");return true;}
        if(now<retry_after) {print("Wait before retrying authentication.");return true;}
        target=0;known=true;action=SU;next(PASSWORD,"Authenticate as root. The ordinary account remains separate.");return true;
    }
    bool add=!strncmp(command,"useradd ",8), change=equal(command,"passwd")||!strncmp(command,"passwd ",7);
    if(!add&&!change)return false;
    if(!auth_logged_in()) {print("Sign in first.");return true;}
    const char *name=add?command+8:command[6]?command+7:auth_name();
    if(!username(name)) {print("Invalid username.");return true;}
    int index=find(name);
    if(add) {
        if(!auth_admin()) {print("Permission denied: root is required to add accounts.");return true;}
        if(index>=0||db.count==AUTH_USERS) {print("Account exists or the 16-account limit was reached.");return true;}
        pending=db;target=pending.count++;memset(&pending.users[target],0,sizeof pending.users[target]);
        pending.users[target].uid=999+target;memcpy(pending.users[target].name,name,strlen(name)+1);action=ADD_USER;
    } else {
        if(index<0||(!auth_admin()&&(unsigned)index!=session)) {print("Permission denied: only your own password can be changed.");return true;}
        target=(unsigned)index;known=true;
        if(!auth_admin()) {action=VERIFY_CHANGE;next(PASSWORD,"Verify your current password before changing it.");return true;}
        pending=db;action=CHANGE;
    }
    next(NEW_PASSWORD,"Choose a password for the account.");return true;
}
bool auth_path(const char *path,bool write) {
    if(!auth_logged_in()||path[0]!='/')return false;
    /* Reject noncanonical paths before permission checks; no dot/empty component aliases. */
    const char *p=path+1;
    while(*p) {
        const char *start=p;while(*p&&*p!='/')++p;size_t n=(size_t)(p-start);
        if(!n||(n==1&&start[0]=='.')||(n==2&&start[0]=='.'&&start[1]=='.'))return false;
        if(*p&&!*++p)return false;
    }
    if(!strncmp(path,"/system",7)&&(!path[7]||path[7]=='/'))return false;
    if(auth_admin())return true;
    if(!strncmp(path,"/root",5)&&(!path[5]||path[5]=='/'))return false;
    char home[40]="/home/";size_t n=strlen(auth_name());memcpy(home+6,auth_name(),n+1);n+=6;
    if(!strncmp(path,home,n)&&(!path[n]||path[n]=='/'))return !write||path[n]=='/';
    if(!strncmp(path,"/home/",6)&&!equal(path,"/home/readme.txt"))return false;
    return !write;
}
