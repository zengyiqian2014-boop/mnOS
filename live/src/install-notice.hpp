// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MNOS_INSTALL_NOTICE_HPP
#define MNOS_INSTALL_NOTICE_HPP
#include <cerrno>
#include <cstdio>
#include <string>
#include <vector>
#include <unistd.h>
#include "mnos-locale.h"

namespace mnos {
enum InstallNotice : unsigned { Python = 1, LinuxHeaders = 2 };

inline unsigned package_notice(std::string name) {
    // APT accepts architecture, version and release selectors. A trailing '-'
    // requests removal even in an install transaction; do not warn about it.
    if(name.empty() || name.back()=='-' || name.front()=='.' || name.front()=='/')return 0;
    if(name.back()=='+')name.pop_back();
    name.resize(name.find_first_of(":=/") == std::string::npos ? name.size() : name.find_first_of(":=/"));
    if(name=="linux-libc-dev")return LinuxHeaders;
    for(const char *prefix:{"python","libpython","pypy"}) {
        const std::string start=prefix;
        if(name.compare(0,start.size(),start))continue;
        if(name.size()==start.size() || name[start.size()]=='-' ||
           (name[start.size()]>='0'&&name[start.size()]<='9'))return Python;
    }
    return 0;
}

inline unsigned install_notices(const std::vector<std::string> &args) {
    if(args.empty() || (args[0]!="/usr/bin/apt" && args[0]!="/usr/bin/apt-get"))return 0;
    bool operation=false,install=false,end_options=false,preview=false;
    unsigned notices=0;
    for(size_t i=1;i<args.size();++i) {
        const auto &word=args[i];
        if(!end_options && word=="--") {end_options=true;continue;}
        if(!end_options && !word.empty() && word[0]=='-') {
            if(word=="--simulate"||word=="--just-print"||word=="--dry-run"||word=="--recon"||
               word=="--download-only"||word=="-s"||word=="-d")preview=true;
            // Option values can themselves be named 'install' or 'python3'.
            if(word=="-o"||word=="--option"||word=="-c"||word=="--config-file"||
               word=="-t"||word=="--target-release"||word=="-S"||word=="--snapshot")++i;
            continue;
        }
        if(!operation) {operation=true;install=word=="install"||word=="reinstall";continue;}
        if(install)notices|=package_notice(word);
    }
    return preview?0:notices;
}

inline bool read_install_confirmation(int fd) {
    // Do not use buffered stdio: reading ahead from a pipe would swallow the
    // separate answer intended for APT's own dependency/transaction prompt.
    std::string answer;
    bool too_long=false;
    for(;;) {
        char ch;ssize_t got;
        do {got=read(fd,&ch,1);}while(got<0&&errno==EINTR);
        if(got!=1)return false;
        if(ch=='\n')break;
        if(answer.size()<64)answer+=ch;else too_long=true;
    }
    if(too_long)return false;
    const auto begin=answer.find_first_not_of(" \t\r");
    if(begin==std::string::npos)return false;
    answer=answer.substr(begin,answer.find_last_not_of(" \t\r")-begin+1);
    for(char &ch:answer)if(ch>='A'&&ch<='Z')ch=char(ch-'A'+'a');
    return answer=="y"||answer=="yes";
}

inline bool confirm_install(const std::vector<std::string> &args) {
    const auto notices=install_notices(args);
    if(!notices)return true;
    if(notices&Python)std::fputs(mnos_text(
        "Python and its dependencies may increase disk and runtime memory use. Performance and memory leaks depend on the program and native extensions.\n",
        "Python 及其依赖可能增加磁盘和运行时内存占用。运行速度及内存泄漏取决于程序和原生扩展的实现。\n"),stderr);
    if(notices&LinuxHeaders)std::fputs(mnos_text(
        "linux-libc-dev adds Linux development headers and uses disk space. Headers are not loaded as runtime libraries; installing them does not itself cause memory leaks.\n",
        "linux-libc-dev 会安装 Linux 开发头文件并占用磁盘空间。头文件不作为运行时库加载，安装它本身不会造成内存泄漏。\n"),stderr);
    std::fputs(mnos_text("Continue installation? [y/N] ","请问你确认要安装它们吗？[y/N] "),stderr);
    std::fflush(stderr);
    if(read_install_confirmation(STDIN_FILENO))return true;
    std::fputs(mnos_text("Installation cancelled; no package changes were started.\n","已取消安装，尚未开始更改软件包。\n"),stderr);
    return false;
}
}
#endif
