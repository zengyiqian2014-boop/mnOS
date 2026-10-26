/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>
/* Fixed programs/argv, no shell, no automatic privilege escalation. */
static int launch(const char *file, const char *a, const char *b, const char *c, const char *d) {
    char *args[]={(char *)file,(char *)a,(char *)b,(char *)c,(char *)d,NULL};
    execv(file,args);perror(file);return 1;
}
static int admin(void) {
    if(getuid()==0&&geteuid()==0)return 1;
    fputs("This change requires administrator authentication. Run sudo mnos-admin ...\n",stderr);return 0;
}
static int unit(const char *s) {
    if(!s||!isalnum((unsigned char)*s))return 0;
    for(const char *p=s;*p;p++)if(!isalnum((unsigned char)*p)&&!strchr("_.@:-",*p))return 0;
    const char *dot=strrchr(s,'.');
    return dot&&(!strcmp(dot,".service")||!strcmp(dot,".socket")||!strcmp(dot,".timer")||!strcmp(dot,".target"));
}
static void help(int cn) {
    puts(cn?"mnOS Linux 系统管理：查询无需提权，修改系统需 sudo。":"mnOS Linux management: queries use your account; system changes require sudo.");
    puts("mnos-admin info | gui | processes | storage | network | accounts | boot | security");
    puts("mnos-admin services [list|status UNIT|start UNIT|stop UNIT|restart UNIT|enable UNIT|disable UNIT]");
    puts("mnos-admin logs [UNIT] | commands | tools | ctf | packages | updates");
    puts(cn?"gui：打开统一管理中心；服务、账户、磁盘、网络、日志与更新都有图形入口。":"gui: open the management center for services, accounts, disks, network, logs and updates.");
    puts(cn?"services：UNIT 必须明确写 .service/.socket/.timer/.target；list/status 查询，其他操作需 root。":"services: supply a .service/.socket/.timer/.target unit; list/status query, other actions require root.");
    puts(cn?"logs：最近 100 条日志；只能读取当前账号有权访问的记录，必要时自行用 sudo。":"logs: latest 100 accessible journal entries; use sudo explicitly for protected logs.");
    puts(cn?"processes→ps；storage→lsblk；network→nmcli；accounts→getent；boot→systemd-analyze。":"processes→ps; storage→lsblk; network→nmcli; accounts→getent; boot→systemd-analyze.");
    puts(cn?"packages/updates：打开软件管理界面，应用系统修改时由其请求认证。":"packages/updates open package management; the manager authenticates changes.");
    puts(cn?"security：查看 AppArmor 状态，受系统访问权限约束。":"security reports AppArmor status subject to system access controls.");
    puts(cn?"commands/tools/ctf：打开实际安装的命令、工具与 CTF 目录，不自动执行扫描。":"commands/tools/ctf open installed command and tool catalogs; no automatic scans.");
    puts(cn?"示例：mnos-admin services status NetworkManager.service；sudo mnos-admin services restart NetworkManager.service":"Example: mnos-admin services status NetworkManager.service; sudo mnos-admin services restart NetworkManager.service");
}
int main(int argc,char **argv) {
    const char *cmd=argc>1?argv[1]:"help";
    if(!strcmp(cmd,"help")||!strcmp(cmd,"--help")){help(argc>2&&!strcmp(argv[2],"-cn"));return 0;}
    if(!strcmp(cmd,"info")&&argc==2){struct utsname u;if(uname(&u))return 1;
        printf("mnOS Linux Edition\nHost kernel: %s %s / %s\nDesktop: Xfce with mnOS C/C++ tools\n",u.sysname,u.release,u.machine);
        puts("Native Edition has a separate original kernel and software Linux guest.");return 0;}
    if(!strcmp(cmd,"services")) {
        if(argc==2||(argc==3&&!strcmp(argv[2],"list")))return launch("/usr/bin/systemctl","--no-pager","--type=service","--all",NULL);
        if(argc!=4||!unit(argv[3])){fputs("Supply one explicit unit name, such as NetworkManager.service.\n",stderr);return 2;}
        if(!strcmp(argv[2],"status"))return launch("/usr/bin/systemctl","--no-pager","status","--",argv[3]);
        const char *verbs[]={"start","stop","restart","enable","disable",NULL};
        for(unsigned i=0;verbs[i];i++)if(!strcmp(argv[2],verbs[i])) {
            if(!admin())return 1;
            return launch("/usr/bin/systemctl",argv[2],"--",argv[3],NULL);
        }
        fputs("Unknown service operation. See mnos-admin help.\n",stderr);return 2;
    }
    if(!strcmp(cmd,"logs")) {
        if(argc==2)return launch("/usr/bin/journalctl","--no-pager","-n","100",NULL);
        if(argc==3&&unit(argv[2])) {
            char *args[]={"/usr/bin/journalctl","--no-pager","-n","100","-u",argv[2],NULL};execv(args[0],args);perror(args[0]);return 1;
        }
        fputs("Usage: mnos-admin logs [UNIT]\n",stderr);return 2;
    }
    if(argc!=2){help(0);return 2;}
    if(!strcmp(cmd,"gui"))return launch("/usr/bin/mnos-center","--management",NULL,NULL,NULL);
    if(!strcmp(cmd,"processes"))return launch("/usr/bin/ps","aux",NULL,NULL,NULL);
    if(!strcmp(cmd,"storage"))return launch("/usr/bin/lsblk","--fs",NULL,NULL,NULL);
    if(!strcmp(cmd,"network"))return launch("/usr/bin/nmcli","device","status",NULL,NULL);
    if(!strcmp(cmd,"accounts"))return launch("/usr/bin/getent","passwd",NULL,NULL,NULL);
    if(!strcmp(cmd,"boot"))return launch("/usr/bin/systemd-analyze","time",NULL,NULL,NULL);
    if(!strcmp(cmd,"security"))return launch("/usr/sbin/aa-status",NULL,NULL,NULL,NULL);
    if(!strcmp(cmd,"packages"))return launch("/usr/bin/synaptic-pkexec",NULL,NULL,NULL,NULL);
    if(!strcmp(cmd,"updates"))return launch("/usr/lib/cockpit/cockpit-desktop","updates",NULL,NULL,NULL);
    if(!strcmp(cmd,"commands"))return launch("/usr/bin/mnos-center","--commands",NULL,NULL,NULL);
    if(!strcmp(cmd,"tools"))return launch("/usr/bin/mnos-center","--tools",NULL,NULL,NULL);
    if(!strcmp(cmd,"ctf"))return launch("/usr/bin/mnos-center","--ctf",NULL,NULL,NULL);
    help(0);return 2;
}
