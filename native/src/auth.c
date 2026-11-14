/* SPDX-License-Identifier: GPL-3.0-or-later
 * Local-console accounts. All exposed filesystem and UI entry points check this
 * session; native file requests also verify the process's captured UID.
 * Account administration stays in the console. No Linux ABI claim.
 */
#include "auth.h"
#include <string.h>
#define AUTH_MAGIC 0x55414e4dU
#define NO_USER UINT32_MAX
enum state { SETUP_NAME, SETUP_FACTOR, LOGIN_NAME, PASSWORD, NEW_PASSWORD, CONFIRM, HASHING, FACTOR, RECOVERY, READY, BROKEN };
enum action { LOGIN, SU, ENROLL_USER, ENROLL_ROOT, ADD_USER, CHANGE, VERIFY_CHANGE, MFA_ENROLL, MFA_DISABLE };
static struct auth_io io;
static struct auth_database db, pending;
static struct password_kdf kdf;
static struct password_kdf wrapping;
static bool wrap_active;
static uint8_t wrapping_key[32], factor_secret[32];
static char setup_secret[53], recovery_codes[FACTOR_RECOVERY][33];
static char setup_uri[224];
static unsigned recovery_page;
static uint64_t challenge_until;
static enum state state = BROKEN;
static enum action action;
static uint32_t session = NO_USER, previous = NO_USER;
static unsigned target, failures;
static bool known;
static uint64_t now, retry_after, root_until;
static char input[129], first[129], masked[129];
static unsigned used;
static const char *status = "Initializing accounts...";
static void clear_factor(void) {
    auth_wipe(&wrapping,sizeof wrapping);auth_wipe(wrapping_key,sizeof wrapping_key);
    auth_wipe(factor_secret,sizeof factor_secret);auth_wipe(setup_secret,sizeof setup_secret);
    auth_wipe(setup_uri,sizeof setup_uri);
    auth_wipe(recovery_codes,sizeof recovery_codes);wrap_active=false;challenge_until=0;
}
bool auth_decode_database(const void *data,size_t size,struct auth_database *out) {
    struct legacy_user {uint32_t uid;char name[32];uint8_t salt[16],hash[32];};
    struct legacy {uint32_t magic,version,rounds,count;struct legacy_user users[AUTH_USERS];};
    if(size==sizeof *out) {memcpy(out,data,size);return out->version==2;}
    if(size!=sizeof(struct legacy))return false;
    struct legacy old;memcpy(&old,data,size);
    if(old.version!=1)return false;
    memset(out,0,sizeof *out);out->magic=old.magic;out->version=2;out->rounds=old.rounds;out->count=old.count;
    for(unsigned i=0;i<AUTH_USERS;++i)memcpy(&out->users[i],&old.users[i],sizeof old.users[i]);
    auth_wipe(&old,sizeof old);return true;
}
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
    if(db.magic!=AUTH_MAGIC||db.version!=2||db.rounds!=AUTH_ROUNDS||db.count<2||db.count>AUTH_USERS)return false;
    if(db.users[0].uid!=0||!username(db.users[0].name)||!equal(db.users[0].name,"root"))return false;
    for(unsigned i=0;i<db.count;++i) {
        if(!username(db.users[i].name)||(i&&db.users[i].uid<1000)||db.users[i].factor.enabled>1)return false;
        for(unsigned j=0;j<i;++j)if(db.users[i].uid==db.users[j].uid||equal(db.users[i].name,db.users[j].name))return false;
    }
    return true;
}
void auth_lock(void) {
    clear_factor();
    session=previous=NO_USER;root_until=0;
    auth_wipe(&kdf,sizeof kdf);auth_wipe(&pending,sizeof pending);auth_wipe(first,sizeof first);
    next(LOGIN_NAME,"Sign in to mnOS.");
}
void auth_init(const struct auth_io *callbacks) {
    clear_factor();
    io=*callbacks;session=previous=NO_USER;failures=0;now=retry_after=root_until=0;
    auth_wipe(&db,sizeof db);auth_wipe(&pending,sizeof pending);auth_wipe(&kdf,sizeof kdf);auth_wipe(first,sizeof first);
    int result=io.load(&db);
    if(result==0&&valid_database()) { auth_lock();return; }
    if(result!=1) { next(BROKEN,"Account store unavailable or damaged. Access remains locked.");return; }
    memset(&pending,0,sizeof pending);pending.magic=AUTH_MAGIC;pending.version=2;pending.rounds=AUTH_ROUNDS;pending.count=2;
    memcpy(pending.users[0].name,"root",5);pending.users[1].uid=1000;
    next(SETUP_NAME,io.persistent?"First boot: create your user and root passwords.":"No MNFS disk: accounts last for this boot only.");
}
bool auth_logged_in(void) { return session!=NO_USER; }
bool auth_admin(void) { return auth_logged_in()&&db.users[session].uid==0; }
uint32_t auth_uid(void) { return auth_logged_in()?db.users[session].uid:NO_USER; }
const char *auth_name(void) { return auth_logged_in()?db.users[session].name:"locked"; }
bool auth_prompting(void) { return state!=READY; }
const char *auth_status(void) { return status; }
bool auth_factor_enabled(void) {return auth_logged_in()&&db.users[session].factor.enabled==1;}
const char *auth_setup_secret(void) {return state==FACTOR&&action==MFA_ENROLL?setup_secret:"";}
const char *auth_setup_uri(void) {return state==FACTOR&&action==MFA_ENROLL?setup_uri:"";}
const char *auth_recovery_code(void) {return state==RECOVERY?recovery_codes[recovery_page]:"";}
const char *auth_prompt(void) {
    if(state==SETUP_NAME)return "New username (a-z, 0-9, _ or -):";
    if(state==SETUP_FACTOR)return "Enable an authenticator now? Y: set up / N: use password only";
    if(state==LOGIN_NAME)return "Username:";
    if(state==PASSWORD)return action==SU?"Root password:":"Password:";
    if(state==NEW_PASSWORD)return action==ENROLL_ROOT?"Create root password (12+ characters):":"New password (12+ characters):";
    if(state==CONFIRM)return "Confirm new password:";
    if(state==HASHING)return "Processing password...";
    if(state==FACTOR)return action==MFA_ENROLL?"Enter the authenticator's 6-digit code to enable 2FA:":"Authenticator code or an unused recovery code:";
    if(state==RECOVERY)return "Save this recovery code; Enter shows the next (8 total):";
    return "";
}
const char *auth_input(void) {
    if(state==SETUP_NAME||state==SETUP_FACTOR||state==LOGIN_NAME)return input;
    memset(masked,'*',used);masked[used]=0;return masked;
}
static void signed_in(unsigned index) {
    clear_factor();
    session=index;failures=0;retry_after=0;root_until=index==0?now+300:0;
    next(READY,index==0?"Root session: expires after 5 minutes.":"Signed in as an ordinary user.");
}
static void failed(void) {
    clear_factor();auth_wipe(&pending,sizeof pending);
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
    bool needed=verify?(db.users[target].factor.enabled||action==MFA_ENROLL):pending.users[target].factor.enabled;
    if(needed) {
        struct factor_record *factor=verify&&action!=MFA_ENROLL?&db.users[target].factor:&pending.users[target].factor;
        if(action==MFA_ENROLL&&!io.random(factor->salt,16)) {failed();status="Secure randomness unavailable; operation cancelled.";return;}
        password_begin(&wrapping,verify?input:first,factor->salt,AUTH_ROUNDS);wrap_active=true;
    }
    password_begin(&kdf,verify?input:first,user->salt,AUTH_ROUNDS);
    auth_wipe(first,sizeof first);next(HASHING,"Password hashing: PBKDF2-HMAC-SHA256, 600000 iterations.");
}
static bool save_factor(void) {
    if(io.save(&pending)) {failed();status="2FA state could not be saved; access denied.";return false;}
    db=pending;auth_wipe(&pending,sizeof pending);return true;
}
static void verified(void) {
    if(action==VERIFY_CHANGE) {pending=db;action=CHANGE;next(NEW_PASSWORD,"Choose a new account password.");return;}
    if(action==MFA_DISABLE) {
        pending=db;auth_wipe(&pending.users[target].factor,sizeof pending.users[target].factor);
        if(!save_factor())return;clear_factor();next(READY,"Two-factor authentication disabled.");return;
    }
    if(action==SU)previous=session;else previous=NO_USER;signed_in(target);
}
static void start_factor(void) {
    uint64_t time=0;
    bool clock_valid=io.wall_time&&io.wall_time(&time)&&time>=1577836800ULL;
    if(action==MFA_ENROLL&&!clock_valid) {failed();status="Valid UTC clock required for enrollment. Check firmware clock.";return;}
    if(action==MFA_ENROLL) {
        if(!io.random(factor_secret,32)) {failed();return;}
        for(unsigned i=0;i<FACTOR_RECOVERY;++i) {
            uint8_t bytes[16];if(!io.random(bytes,sizeof bytes)) {failed();return;}
            for(unsigned j=0;j<16;++j) {recovery_codes[i][j*2]="0123456789abcdef"[bytes[j]>>4];recovery_codes[i][j*2+1]="0123456789abcdef"[bytes[j]&15];}
            recovery_codes[i][32]=0;auth_wipe(bytes,sizeof bytes);
            factor_recovery_hash(recovery_codes[i],pending.users[target].factor.salt,pending.users[target].factor.recovery[i]);
        }
        factor_base32(factor_secret,setup_secret);
        const char *parts[]={"otpauth://totp/mnOS:",db.users[target].name,"?secret=",setup_secret,"&issuer=mnOS&algorithm=SHA1&digits=6&period=30"};
        size_t length=0;for(unsigned i=0;i<5;++i) {size_t n=strlen(parts[i]);memcpy(setup_uri+length,parts[i],n);length+=n;}setup_uri[length]=0;
        next(FACTOR,"Scan the QR code or enter the key manually: time-based / 6 digits.");
    } else {
        if(!factor_open(&db.users[target].factor,db.users[target].uid,wrapping_key,factor_secret)) {failed();return;}
        next(FACTOR,clock_valid?"Password accepted. Complete the second factor.":"Clock unavailable. Enter a recovery code, or correct the firmware clock.");
    }
    challenge_until=now+180;
}
static void check_factor(void) {
    uint64_t time=0,step=0;bool ok=false;
    if(action!=MFA_ENROLL)pending=db;
    struct factor_record *factor=&pending.users[target].factor;
    if(io.wall_time&&io.wall_time(&time))ok=factor_check(factor_secret,time,factor->last_step,input,&step);
    if(ok)factor->last_step=step;
    else if(action!=MFA_ENROLL&&strlen(input)==32) {
        uint8_t hash[32],zero[32]={0};factor_recovery_hash(input,factor->salt,hash);
        for(unsigned i=0;i<FACTOR_RECOVERY;++i)if(!factor_same(factor->recovery[i],zero,32)&&factor_same(hash,factor->recovery[i],32)) {memset(factor->recovery[i],0,32);ok=true;}
        auth_wipe(hash,sizeof hash);
    }
    if(!ok) {failed();return;}
    if(!factor_seal(factor,db.users[target].uid,wrapping_key,factor_secret,io.random)||!save_factor()) {if(state==FACTOR)failed();return;}
    if(action==MFA_ENROLL) {
        auth_wipe(wrapping_key,32);auth_wipe(factor_secret,32);auth_wipe(setup_secret,sizeof setup_secret);
        auth_wipe(setup_uri,sizeof setup_uri);
        recovery_page=0;challenge_until=now+300;next(RECOVERY,"2FA enabled. Store all 8 one-use recovery codes outside this machine.");return;
    }
    verified();
}
void auth_tick(uint64_t seconds) {
    now=seconds;
    if(auth_admin()&&now>=root_until) { auth_lock();status="Root session expired. Sign in again.";return; }
    if((state==FACTOR||state==RECOVERY)&&now>=challenge_until) {failed();status="Authentication setup expired. Start again.";return;}
    if(state!=HASHING)return;
    bool done=password_step(&kdf,1024),wrapped=!wrap_active||password_step(&wrapping,1024);
    if(!done||!wrapped)return;
    if(wrap_active) {memcpy(wrapping_key,wrapping.result,32);auth_wipe(&wrapping,sizeof wrapping);wrap_active=false;}
    if(action==LOGIN||action==SU||action==VERIFY_CHANGE||action==MFA_ENROLL||action==MFA_DISABLE) {
        bool ok=same_hash(kdf.result,db.users[target].hash)&&known;auth_wipe(&kdf,sizeof kdf);
        if(!ok) { failed();return; }
        if(db.users[target].factor.enabled||action==MFA_ENROLL)start_factor();else verified();return;
    }
    memcpy(pending.users[target].hash,kdf.result,32);auth_wipe(&kdf,sizeof kdf);
    if(pending.users[target].factor.enabled&&!factor_seal(&pending.users[target].factor,pending.users[target].uid,wrapping_key,factor_secret,io.random)) {failed();return;}
    clear_factor();
    if(action==ENROLL_USER) { action=ENROLL_ROOT;target=0;next(NEW_PASSWORD,"Set a separate password for root administration.");return; }
    if(io.save(&pending)) {
        auth_wipe(&pending,sizeof pending);
        next(auth_logged_in()?READY:BROKEN,"Account save failed. Previous credentials were not replaced in memory.");return;
    }
    db=pending;auth_wipe(&pending,sizeof pending);
    if(action==ENROLL_ROOT) {
        signed_in(1);status="Accounts created. Signed in as the ordinary user.";
        if(io.persistent)next(SETUP_FACTOR,status);
    }
    else next(READY,"Account password saved.");
}
void auth_key(char c) {
    if(state==READY||state==BROKEN)return;
    if(c==27) {
        clear_factor();
        if(db.count) { auth_wipe(&kdf,sizeof kdf);auth_wipe(&pending,sizeof pending);auth_wipe(first,sizeof first);
            next(auth_logged_in()?READY:LOGIN_NAME,"Authentication cancelled."); }
        return;
    }
    if(state==HASHING)return;
    if(state==RECOVERY) {if(c=='\n'||c=='\r') {if(++recovery_page==FACTOR_RECOVERY) {clear_factor();next(READY,"Recovery codes acknowledged; password + 2FA required on next login.");}}return;}
    if(c=='\b'||c==127) { if(used)input[--used]=0;return; }
    if(c!='\n'&&c!='\r') { if(c>=32&&c<127&&used<128) {input[used++]=c;input[used]=0;}return; }
    if(now<retry_after) { clear_input();status="Too many attempts. Wait before retrying.";return; }
    if(state==FACTOR) {check_factor();return;}
    if(state==SETUP_FACTOR) {
        if(equal(input,"n")||equal(input,"N")) {next(READY,"Password-only setup complete. Enable 2FA later in Settings.");return;}
        if(equal(input,"y")||equal(input,"Y")) {
            target=session;known=true;pending=db;action=MFA_ENROLL;
            next(PASSWORD,"Enter your password to bind an authenticator (QR or manual key).");return;
        }
        next(SETUP_FACTOR,"Choose Y to bind an authenticator or N to do this later.");return;
    }
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
    if(equal(command,"2fa status")) {print(auth_factor_enabled()?"Password + TOTP enabled.":"TOTP is not enabled for this account.");return true;}
    if(equal(command,"2fa enable")||equal(command,"2fa disable")) {
        if(!auth_logged_in()||state!=READY) {print("Sign in first.");return true;}
        if(now<retry_after) {print("Wait before retrying authentication.");return true;}
        if(!io.persistent) {print("2FA requires persistent account storage.");return true;}
        bool enable=equal(command,"2fa enable");
        if(enable==auth_factor_enabled()) {print(enable?"2FA is already enabled.":"2FA is already disabled.");return true;}
        target=session;known=true;pending=db;action=enable?MFA_ENROLL:MFA_DISABLE;
        next(PASSWORD,"Verify your account password to change two-factor settings.");return true;
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
        if(db.users[target].factor.enabled&&(unsigned)index!=session) {print("An encrypted 2FA account requires its owner's password; root reset is refused.");return true;}
        if(!auth_admin()||db.users[target].factor.enabled) {action=VERIFY_CHANGE;next(PASSWORD,"Verify your current password before changing it.");return true;}
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
