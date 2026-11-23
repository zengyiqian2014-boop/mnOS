// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit APT frontend. The caller chooses elevation; APT verifies repositories
// and still presents its own dependency/transaction confirmation.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include "install-notice.hpp"
static int run(std::vector<std::string> words) {
    if(!mnos::confirm_install(words))return 1;
    std::vector<char *> args;
    for(auto &word:words)args.push_back(word.data());
    args.push_back(nullptr);execv(args[0],args.data());perror(args[0]);return 1;
}
static int usage() {
    puts("mnos-pkg list | check | search TEXT | show PACKAGE\n"
         "sudo mnos-pkg update | upgrade | install PACKAGE... | remove PACKAGE...\n"
         "Queries need no root. Changes require explicit sudo. APT asks for confirmation.\n"
         "Python packages and linux-libc-dev ask an extra y/N installation confirmation.\n"
         "Uses the configured signed APT repositories. No --allow-unauthenticated.\n"
         "Details: help packages / help -cn packages");return 2;
}
static bool package_name(const char *p) {
    if(!*p||!std::isalnum(static_cast<unsigned char>(*p)))return false;
    for(;*p;++p)if(!std::isalnum(static_cast<unsigned char>(*p))&&*p!='+'&&*p!='-'&&*p!='.'&&*p!=':')return false;
    return true;
}
int main(int argc,char **argv) {
    if(argc==1||(argc==2&&!strcmp(argv[1],"--help"))){usage();return 0;}
    const std::string cmd=argv[1];
    if(cmd=="list"&&argc==2)return run({"/usr/bin/dpkg-query","-W"});
    if(cmd=="check"&&argc==2)return run({"/usr/bin/dpkg","--audit"});
    if(cmd=="search"&&argc==3&&*argv[2])return run({"/usr/bin/apt-cache","search","--",argv[2]});
    if(cmd=="show"&&argc==3&&package_name(argv[2]))return run({"/usr/bin/apt-cache","show","--",argv[2]});
    const bool packages=cmd=="install"||cmd=="remove",global=cmd=="update"||cmd=="upgrade";
    if((!packages&&!global)||(packages&&argc<3)||(global&&argc!=2)||argc>128)return usage();
    for(int i=2;i<argc;++i)if(!package_name(argv[i])) {
        fputs("Invalid package name. Options, paths and URLs are not package names.\n",stderr);return 2;
    }
    if(getuid()!=0||geteuid()!=0){fputs("Permission denied: use sudo mnos-pkg for package changes.\n",stderr);return 1;}
    if(setenv("PATH","/usr/sbin:/usr/bin:/sbin:/bin",1)){perror("PATH");return 1;}
    std::vector<std::string> words{"/usr/bin/apt-get",cmd,"--"};
    for(int i=2;i<argc;++i)words.emplace_back(argv[i]);
    return run(words);
}
