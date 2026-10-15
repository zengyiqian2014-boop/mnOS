/* SPDX-License-Identifier: GPL-3.0-or-later
 * Optional network delegation, never UID 0. File capabilities and group DAC
 * are configured only by the administrator/installer after an explicit choice.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <grp.h>
#include <linux/capability.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
static bool authorized(void) {
    struct group *group=getgrnam("mnos-netadmin");if(!group)return false;
    gid_t wanted=group->gr_gid;
    if(getgid()==wanted)return true;
    int count=getgroups(0,NULL);if(count<0)return false;
    gid_t *groups=calloc((size_t)count+1,sizeof *groups);if(!groups)return false;
    int n=getgroups(count,groups);bool found=false;
    for(int i=0;i<n;++i)if(groups[i]==wanted)found=true;
    free(groups);return found;
}
int main(int argc,char **argv) {
    if(argc<2)return 2;
    if(getuid()!=geteuid()||getuid()==0) {
        fputs("Use the network tool directly with sudo for root administration.\n",stderr);return 77;
    }
    if(!authorized()) {fputs("Network delegation is not authorized for this account.\n",stderr);return 77;}
    static const struct {const char *name,*path;} tools[]={
        {"nmap","/usr/bin/nmap"},{"tcpdump","/usr/bin/tcpdump"},
        {"ip","/usr/sbin/ip"},{"tc","/usr/sbin/tc"},{"iw","/usr/sbin/iw"}};
    const char *path=NULL;
    for(size_t i=0;i<sizeof tools/sizeof tools[0];++i)if(!strcmp(argv[1],tools[i].name))path=tools[i].path;
    if(!path) {fputs("Supported tools: nmap, tcpdump, ip, tc, iw.\n",stderr);return 2;}
    struct __user_cap_header_struct header={.version=_LINUX_CAPABILITY_VERSION_3,.pid=0};
    struct __user_cap_data_struct caps[2]={{0}};
    unsigned mask=(1u<<CAP_NET_ADMIN)|(1u<<CAP_NET_RAW);
    if(syscall(SYS_capget,&header,caps)||((caps[0].permitted&mask)!=mask)) {
        fputs("Network capabilities are disabled. Ask an administrator or use sudo with the tool.\n",stderr);return 77;
    }
    caps[0]=(struct __user_cap_data_struct){.effective=mask,.permitted=mask,.inheritable=mask};
    memset(&caps[1],0,sizeof caps[1]);
    if(syscall(SYS_capset,&header,caps)||prctl(PR_CAP_AMBIENT,PR_CAP_AMBIENT_CLEAR_ALL,0,0,0)||
       prctl(PR_CAP_AMBIENT,PR_CAP_AMBIENT_RAISE,CAP_NET_ADMIN,0,0)||
       prctl(PR_CAP_AMBIENT,PR_CAP_AMBIENT_RAISE,CAP_NET_RAW,0,0)) {perror("Network capabilities");return 77;}
    if(clearenv()||setenv("PATH","/usr/sbin:/usr/bin:/sbin:/bin",1)||setenv("LANG","C.UTF-8",1))return 1;
    if(!strcmp(argv[1],"nmap")&&setenv("NMAP_PRIVILEGED","1",1))return 1;
    argv[1]=(char *)path;execv(path,argv+1);perror(path);return 1;
}
