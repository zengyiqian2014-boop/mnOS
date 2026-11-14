// SPDX-License-Identifier: GPL-3.0-or-later
// Privileged broker only: never loads GTK, camera drivers or inference models.
#include "face-record.hpp"
#include <security/pam_appl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <string>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
using face_auth::Record;
struct Fd {int n=-1;explicit Fd(int fd=-1):n(fd){}~Fd(){if(n>=0)close(n);}Fd(const Fd&)=delete;};
struct Secret {
    char *password=nullptr;size_t bytes=0;uint8_t key[32]{};float reference[128]{};char totp[129]{},recovery[33]{};
    ~Secret(){if(password){auth_wipe(password,bytes);munlock(password,bytes);free(password);}auth_wipe(this,sizeof *this);munlock(this,sizeof *this);}
};
static void wait_child(pid_t child) {while(waitpid(child,nullptr,0)<0&&errno==EINTR){}}
static bool write_all(int fd,const void *data,size_t size) {
    auto *p=static_cast<const uint8_t *>(data);while(size){ssize_t n=write(fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;size-=size_t(n);}return true;
}
static bool trusted(int fd,uid_t owner,bool directory=false) {
    struct stat st{};return fd>=0&&!fstat(fd,&st)&&st.st_uid==owner&&!(st.st_mode&077)&&(directory?S_ISDIR(st.st_mode):S_ISREG(st.st_mode)&&st.st_nlink==1);
}
static int store_open(uid_t owner,gid_t group,bool create) {
    if(create&&mkdir(MNOS_FACE_STORE,0700)==0) {
        Fd fd(open(MNOS_FACE_STORE,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
        if(fd.n<0||fchown(fd.n,owner,group))return -1;
    }
    int fd=open(MNOS_FACE_STORE,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return errno==ENOENT&&!create?-2:-1;
    if(!trusted(fd,owner,true)){close(fd);return -1;}return fd;
}
static int load(int dir,const char *name,uid_t owner,uid_t uid,Record &record) {
    Fd fd(openat(dir,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC));if(fd.n<0)return errno==ENOENT?1:-1;
    struct stat st{};if(!trusted(fd.n,owner)||fstat(fd.n,&st)||st.st_size!=sizeof record||login_read(fd.n,&record,sizeof record))return -1;
    return record.magic==FACE_MAGIC&&record.version==2&&record.uid==uid&&record.rounds==AUTH_ROUNDS&&record.failures<=6?0:-1;
}
static bool save(int dir,const char *name,uid_t owner,gid_t group,const Record &record) {
    uint8_t random[8];if(!face_auth::random_bytes(random,sizeof random))return false;
    char temporary[64];int length=snprintf(temporary,sizeof temporary,".%s.",name);
    for(unsigned i=0;i<8;++i)snprintf(temporary+length+i*2,3,"%02x",random[i]);
    Fd fd(openat(dir,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));if(fd.n<0)return false;
    bool ok=!fchown(fd.n,owner,group)&&write_all(fd.n,&record,sizeof record)&&!fsync(fd.n)&&!renameat(dir,temporary,dir,name)&&!fsync(dir);
    if(!ok)unlinkat(dir,temporary,0);return ok;
}
struct Credentials {const char *name,*password;};
static int conversation(int n,const pam_message **messages,pam_response **out,void *data) {
    auto *c=static_cast<Credentials *>(data);if(n<1||n>PAM_MAX_NUM_MSG)return PAM_CONV_ERR;
    auto *r=static_cast<pam_response *>(calloc(size_t(n),sizeof(pam_response)));if(!r)return PAM_BUF_ERR;
    for(int i=0;i<n;++i) {
        const char *text=nullptr;
        switch(messages[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:text=c->password;break;
        case PAM_PROMPT_ECHO_ON:text=c->name;break;
        case PAM_TEXT_INFO:case PAM_ERROR_MSG:break;
        default:goto fail;
        }
        if(text&&!(r[i].resp=strdup(text)))goto fail;
    }
    *out=r;return PAM_SUCCESS;
fail:
    for(int i=0;i<n;++i)if(r[i].resp){auth_wipe(r[i].resp,strlen(r[i].resp));free(r[i].resp);}free(r);return PAM_CONV_ERR;
}
static bool password_valid(const char *name,const char *password) {
    Credentials credentials{name,password};pam_conv conv{conversation,&credentials};pam_handle_t *pam=nullptr;
    int result=pam_start("mnos-security-setup",name,&conv,&pam);
    if(result==PAM_SUCCESS)result=pam_authenticate(pam,PAM_DISALLOW_NULL_AUTHTOK);
    if(result==PAM_SUCCESS)result=pam_acct_mgmt(pam,0);
    if(pam)pam_end(pam,result);return result==PAM_SUCCESS;
}
static bool settings_auth(uid_t uid,const Secret &secret) {
    int child=-1;Fd fd(login_child(uid,0,&child));if(fd.n<0)return false;
    login_response response{};bool ok=!login_send(fd.n,LOGIN_VERIFY,uid,secret.password,secret.totp)&&!login_read(fd.n,&response,sizeof response);
    shutdown(fd.n,SHUT_RDWR);wait_child(child);return ok&&(response.result==LOGIN_OK||response.result==LOGIN_ABSENT);
}
static bool live_session() {
    FILE *f=fopen("/proc/cmdline","re");if(!f)return true;
    char word[256];bool live=false;while(fscanf(f,"%255s",word)==1)if(!strcmp(word,"boot=live"))live=true;fclose(f);return live;
}
static int cookie_file(const char *display,uid_t target,uid_t worker) {
    // Only local X11 displays. Never connect a privileged camera process to a
    // caller-supplied network display or read an arbitrary XAUTHORITY path.
    size_t length=strlen(display);if(length<2||length>6||display[0]!=':')return -1;
    for(size_t i=1;i<length;++i)if(display[i]<'0'||display[i]>'9')return -1;
    char path[128];snprintf(path,sizeof path,"/run/lightdm/root/%s",display);
    Fd root(open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC));int source=root.n;
    Fd user;
    if(!trusted(source,0)) {
        auto *pw=getpwuid(target);if(!pw)return -1;
        std::string home=pw->pw_dir;home+="/.Xauthority";
        user.n=open(home.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);source=user.n;
        if(!trusted(source,target))return -1;
    }
    struct stat st{};if(fstat(source,&st)||st.st_size<=0||st.st_size>16384)return -1;
    Fd output(memfd_create("mnos-camera-display",MFD_CLOEXEC|MFD_ALLOW_SEALING));if(output.n<0)return -1;
    char bytes[4096];bool ok=true;off_t remaining=st.st_size;
    while(remaining>0){ssize_t n=read(source,bytes,std::min<size_t>(size_t(remaining),sizeof bytes));if(n<0&&errno==EINTR)continue;if(n<=0||!write_all(output.n,bytes,size_t(n))){ok=false;break;}remaining-=n;}
    auth_wipe(bytes,sizeof bytes);
    if(!ok||fchown(output.n,worker,static_cast<gid_t>(-1))||fchmod(output.n,0400)||fcntl(output.n,F_ADD_SEALS,F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)<0)return -1;
    int fd=output.n;output.n=-1;return fd;
}
static bool camera(uid_t target,bool enroll,const char *display,bool chinese,float *reference) {
    auto *pw=getpwnam("login-camera");if(!pw||!pw->pw_uid)return false;uid_t uid=pw->pw_uid;gid_t gid=pw->pw_gid;
    auto *video=getgrnam("video");if(!video)return false;gid_t video_gid=video->gr_gid;
    std::string local=display; if(local.size()>2&&local.substr(local.size()-2)==".0")local.resize(local.size()-2);display=local.c_str();
    Fd cookie(cookie_file(display,target,uid));if(cookie.n<0)return false;
    int pair[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair))return false;
    pid_t child=fork();
    if(!child) {
        // Move both descriptors above the final fixed fd numbers before dup2.
        int channel=fcntl(pair[1],F_DUPFD_CLOEXEC,10),authority=fcntl(cookie.n,F_DUPFD_CLOEXEC,10);
        if(channel<0||authority<0||dup2(channel,3)<0||dup2(authority,4)<0)_exit(1);
        closefrom(5);int null=open("/dev/null",O_RDWR|O_CLOEXEC);if(null<0)_exit(1);
        if(dup2(null,0)<0||dup2(null,1)<0||dup2(null,2)<0)_exit(1);if(null>4)close(null);
        if(setgroups(1,&video_gid)||setresgid(gid,gid,gid)||setresuid(uid,uid,uid)||prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)||prctl(PR_SET_DUMPABLE,0))_exit(1);
        clearenv();if(setenv("DISPLAY",display,1)||setenv("XAUTHORITY","/proc/self/fd/4",1)||setenv("HOME","/nonexistent",1)||setenv("PATH","/usr/bin:/bin",1)||setenv("LANG",chinese?"zh_CN.UTF-8":"en_US.UTF-8",1)||setenv("GDK_BACKEND","x11",1))_exit(1);
        alarm(110);execl(MNOS_FACE_CAMERA,"face-camera",(char *)nullptr);_exit(1);
    }
    close(pair[1]);Fd channel(pair[0]);if(child<0)return false;
    timeval timeout{115,0};bool ok=!setsockopt(channel.n,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout)&&!setsockopt(channel.n,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
    face_camera_request request{};request.magic=FACE_MAGIC;request.enroll=enroll?1:0;if(!enroll)memcpy(request.reference,reference,512);
    face_camera_response response{};ok=ok&&!login_write(channel.n,&request,sizeof request)&&!login_read(channel.n,&response,sizeof response);
    shutdown(channel.n,SHUT_RDWR);int status=-1;if(!ok)kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}
    ok=ok&&WIFEXITED(status)&&WEXITSTATUS(status)==0&&response.magic==FACE_MAGIC&&response.success==1;
    if(ok&&enroll){ok=face_auth::valid(response.reference);if(ok)memcpy(reference,response.reference,512);}
    auth_wipe(&request,sizeof request);auth_wipe(&response,sizeof response);return ok;
}
static unsigned serve(uid_t uid,bool pam,bool chinese,face_response &response) {
    face_request q{};Secret secret;if(mlock(&secret,sizeof secret))return LOGIN_ERROR;
    if(login_read(0,&q,sizeof q)||q.magic!=FACE_MAGIC||q.uid!=uid||q.operation<LOGIN_STATUS||q.operation>LOGIN_DISABLE||q.password_bytes>1048576||q.totp_bytes>128||q.recovery_bytes>32||!memchr(q.display,0,sizeof q.display))return LOGIN_ERROR;
    secret.bytes=size_t(q.password_bytes)+1;secret.password=static_cast<char *>(calloc(1,secret.bytes));if(!secret.password||mlock(secret.password,secret.bytes))return LOGIN_ERROR;
    if(login_read(0,secret.password,q.password_bytes)||login_read(0,secret.totp,q.totp_bytes)||login_read(0,secret.recovery,q.recovery_bytes)||memchr(secret.password,0,q.password_bytes)||memchr(secret.totp,0,q.totp_bytes)||memchr(secret.recovery,0,q.recovery_bytes))return LOGIN_ERROR;
    auto *user=getpwuid(uid);if(!user)return LOGIN_ERROR;std::string name=user->pw_name;
    auto *service=getpwnam("login-process");if(!service||!service->pw_uid)return LOGIN_ERROR;uid_t owner=service->pw_uid;gid_t group=service->pw_gid;
    if(pam&&q.operation!=LOGIN_STATUS&&q.operation!=LOGIN_VERIFY)return LOGIN_DENIED;
    if(q.operation!=LOGIN_STATUS) {
        if(uid<1000||(live_session()&&q.operation!=LOGIN_VERIFY))return LOGIN_DENIED;
        if(pam?!password_valid(name.c_str(),secret.password):!settings_auth(uid,secret))return LOGIN_DENIED;
    }
    Fd dir(store_open(owner,group,q.operation==LOGIN_ENROLL));if(dir.n==-2)return q.operation==LOGIN_STATUS||q.operation==LOGIN_VERIFY?LOGIN_ABSENT:LOGIN_ERROR;if(dir.n<0)return LOGIN_ERROR;
    char filename[32],lockname[32];snprintf(filename,sizeof filename,"%u.face",uid);snprintf(lockname,sizeof lockname,"%u.lock",uid);
    Fd lock(openat(dir.n,lockname,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600));if(lock.n<0)return LOGIN_ERROR;
    struct stat ls{};if(fstat(lock.n,&ls)||!S_ISREG(ls.st_mode)||ls.st_nlink!=1||(ls.st_uid!=0&&ls.st_uid!=owner)||(ls.st_mode&077)||fchown(lock.n,owner,group)||flock(lock.n,LOCK_EX))return LOGIN_ERROR;
    Record record;int loaded=load(dir.n,filename,owner,uid,record);if(loaded<0)return LOGIN_ERROR;
    if(q.operation==LOGIN_STATUS)return loaded?LOGIN_ABSENT:LOGIN_OK;
    if(q.operation==LOGIN_ENROLL) {
        if(!loaded)return LOGIN_DENIED;record.uid=uid;
        if(!face_auth::random_bytes(record.salt.data(),16)||!camera(uid,true,q.display,chinese,secret.reference))return LOGIN_ERROR;
        face_auth::derive(secret.password,record.salt.data(),secret.key);
        if(!face_auth::seal(record,secret.key,secret.reference))return LOGIN_ERROR;
        for(unsigned i=0;i<8;++i){uint8_t bytes[16];if(!face_auth::random_bytes(bytes,16))return LOGIN_ERROR;for(unsigned j=0;j<16;++j)snprintf(response.recovery[i]+j*2,3,"%02x",bytes[j]);auth_wipe(bytes,16);factor_recovery_hash(response.recovery[i],record.salt.data(),record.recovery[i].data());}
        response.result=LOGIN_SETUP;if(login_write(1,&response,sizeof response))return LOGIN_ERROR;
        auth_wipe(response.recovery,sizeof response.recovery);uint32_t confirm=0;
        if(login_read(0,&confirm,sizeof confirm)||confirm!=FACE_MAGIC)return LOGIN_DENIED;
        return save(dir.n,filename,owner,group,record)?LOGIN_OK:LOGIN_ERROR;
    }
    if(loaded)return q.operation==LOGIN_VERIFY?LOGIN_ABSENT:LOGIN_DENIED;
    auto now=static_cast<uint64_t>(time(nullptr));if(now<record.retry_after&&!q.recovery_bytes)return LOGIN_RETRY;
    face_auth::derive(secret.password,record.salt.data(),secret.key);
    bool accepted=face_auth::open(record,secret.key,secret.reference);
    accepted=accepted&&(q.recovery_bytes?face_auth::recovery(record,secret.recovery):camera(uid,false,q.display,chinese,secret.reference));
    if(!accepted){if(record.failures<6)++record.failures;record.retry_after=now+(1u<<record.failures);return save(dir.n,filename,owner,group,record)?LOGIN_DENIED:LOGIN_ERROR;}
    record.failures=0;record.retry_after=0;
    // Persist recovery-code consumption before reporting success or disabling.
    if(!save(dir.n,filename,owner,group,record))return LOGIN_ERROR;
    if(q.operation==LOGIN_DISABLE&&(unlinkat(dir.n,filename,0)||fsync(dir.n)))return LOGIN_ERROR;
    return LOGIN_OK;
}
int main(int argc,char **argv) {
    uid_t caller=getuid();rlimit no_core{0,0};if(geteuid()!=0||setrlimit(RLIMIT_CORE,&no_core)||prctl(PR_SET_DUMPABLE,0))return 1;
    if(argc!=3||(strcmp(argv[1],"--user")&&strcmp(argv[1],"--pam")))return 1;
    bool pam=!strcmp(argv[1],"--pam");char *end=nullptr;errno=0;unsigned long id=strtoul(argv[2],&end,10);
    if(errno||!*argv[2]||*end||id>UINT32_MAX||(caller!=0&&id!=caller))return 1;
    const char *lang=getenv("LANG");bool chinese=lang&&!strncmp(lang,"zh",2);
    clearenv();if(setenv("PATH","/usr/sbin:/usr/bin:/sbin:/bin",1)||setenv("LC_ALL","C.UTF-8",1))return 1;
    signal(SIGPIPE,SIG_IGN);alarm(180);umask(077);face_response response{};
    response.result=LOGIN_ERROR;try{response.result=serve(static_cast<uid_t>(id),pam,chinese,response);}catch(...){response.result=LOGIN_ERROR;}
    auth_wipe(response.recovery,sizeof response.recovery);(void)login_write(1,&response,sizeof response);return 0;
}
