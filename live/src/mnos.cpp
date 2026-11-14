// SPDX-License-Identifier: GPL-3.0-or-later
// No shell expansion or implicit elevation. APT verifies signed metadata and
// prompts for the transaction; version rollback covers mnOS packages only.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
static int run(std::vector<std::string> words) {
    std::vector<char *> args;for(auto &w:words)args.push_back(w.data());args.push_back(nullptr);
    pid_t child=fork();if(child<0)return 1;
    if(!child) {execv(args[0],args.data());perror(args[0]);_exit(127);}
    int status=0;pid_t waited;do {waited=waitpid(child,&status,0);}while(waited<0&&errno==EINTR);
    return waited==child&&WIFEXITED(status)?WEXITSTATUS(status):1;
}
static bool version(const std::string &s) {
    if(s.empty()||s.size()>96||s[0]<'0'||s[0]>'9')return false;
    for(unsigned char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='+'||c=='-'||c=='~'))return false;
    return true;
}
static bool installed(const char *package) {
    // Query a fixed database-owned package status, never parse executable input.
    std::ifstream file(std::string("/var/lib/dpkg/info/")+package+".list");return file.good();
}
int main(int argc,char **argv) {
    if(argc==1||!strcmp(argv[1],"help")||!strcmp(argv[1],"--help")) {
        std::cout << "mnos status | source | full | update | rollback VERSION | apt ...\n"
          "Queries need no root. Use sudo for full/update/rollback.\n"
          "full: add the full feature package, without reinstalling the OS.\n"
          "update: refresh signed metadata and update installed mnOS packages.\n"
          "rollback VERSION: reinstall retained mnOS package versions; not disk/data rollback.\n"
          "apt ...: run the real Linux APT client. sudo apt upgrade updates Debian packages.\n"
          "Detailed English/Chinese manual: help updates / help -cn updates\n";
        return 0;
    }
    std::string command=argv[1];
    if(command=="apt") {std::vector<std::string> words{"/usr/bin/apt"};for(int i=2;i<argc;++i)words.emplace_back(argv[i]);return run(words);}
    if(command=="source"&&argc==2) {
        std::ifstream source("/etc/apt/sources.list.d/mnos.sources");
        if(!source) {std::cerr<<"mnOS update source is not configured; no update was fetched.\n";return 1;}
        std::cout<<source.rdbuf();return 0;
    }
    if(command=="status"&&argc==2) {
        std::string flavor;std::ifstream("/usr/share/mnos/flavor")>>flavor;
        std::cout<<"Edition features: "<<(flavor.empty()?"unknown":flavor)<<'\n'<<std::flush;
        return run({"/usr/bin/dpkg-query","-W","-f=${binary:Package}\t${Version}\n","mnos-runtime","mnos-auth","mnos-full"});
    }
    bool rollback=command=="rollback";
    if((command!="full"&&command!="update"&&!rollback)||(rollback?(argc!=3||!version(argv[2])):argc!=2))return 2;
    if(getuid()!=0||geteuid()!=0) {std::cerr<<"Use sudo mnos "<<command<<" for system changes.\n";return 1;}
    if(!std::ifstream("/etc/apt/sources.list.d/mnos.sources")||!std::ifstream("/usr/share/keyrings/mnos-archive.gpg")) {
        std::cerr<<"A signed mnOS resource source is required. No update was attempted.\n";return 1;
    }
    unsetenv("APT_CONFIG");setenv("PATH","/usr/sbin:/usr/bin:/sbin:/bin",1);
    int result=run({"/usr/bin/apt-get","-o","APT::Update::Error-Mode=any","update"});if(result)return result;
    std::vector<std::string> args{"/usr/bin/apt-get","install"};
    if(rollback) {
        std::cout<<"Rollback affects mnOS runtime/authentication packages only. It does not restore Linux, third-party packages or personal data.\n"<<std::flush;
        args.emplace_back("--allow-downgrades");
    } else if(command=="update")args.emplace_back("--only-upgrade");
    args.emplace_back("--");
    std::string suffix=rollback?"="+std::string(argv[2]):"";
    if(command=="full")args.emplace_back("mnos-full");
    else {
        if(!installed("mnos-runtime")) {std::cerr<<"This installation has no managed mnOS runtime package. Install the matching bootstrap package first.\n";return 1;}
        args.emplace_back("mnos-runtime"+suffix);
        if(installed("mnos-auth"))args.emplace_back("mnos-auth"+suffix);
        if(installed("mnos-full"))args.emplace_back("mnos-full"+suffix);
    }
    return run(args);
}
