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
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "mnos-locale.h"
#include "install-notice.hpp"
static int run(std::vector<std::string> words) {
    if(!mnos::confirm_install(words))return 1;
    std::vector<char *> args;for(auto &w:words)args.push_back(w.data());args.push_back(nullptr);
    pid_t child=fork();if(child<0)return 1;
    if(!child) {execv(args[0],args.data());perror(args[0]);_exit(127);}
    int status=0;pid_t waited;do {waited=waitpid(child,&status,0);}while(waited<0&&errno==EINTR);
    return waited==child&&WIFEXITED(status)?WEXITSTATUS(status):1;
}
static int capture(std::vector<std::string> words,std::string &output) {
    int fds[2];if(pipe(fds))return 1;
    pid_t child=fork();
    if(child<0) {close(fds[0]);close(fds[1]);return 1;}
    if(!child) {
        close(fds[0]);if(dup2(fds[1],STDOUT_FILENO)<0)_exit(127);close(fds[1]);
        std::vector<char *> args;for(auto &word:words)args.push_back(word.data());args.push_back(nullptr);
        execv(args[0],args.data());_exit(127);
    }
    close(fds[1]);char buffer[4096];bool valid=true;
    for(;;) {
        ssize_t n=read(fds[0],buffer,sizeof buffer);
        if(n<0&&errno==EINTR)continue;
        if(n<=0) {if(n<0)valid=false;break;}
        if(output.size()+size_t(n)>4*1024*1024) {valid=false;break;}
        output.append(buffer,size_t(n));
    }
    close(fds[0]);int status=0;pid_t waited;
    do {waited=waitpid(child,&status,0);}while(waited<0&&errno==EINTR);
    return valid&&waited==child&&WIFEXITED(status)?WEXITSTATUS(status):1;
}
#include "catalog.hpp"
static bool version(const std::string &s) {
    if(s.empty()||s.size()>96||s[0]<'0'||s[0]>'9')return false;
    for(unsigned char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='+'||c=='-'||c=='~'))return false;
    return true;
}
static std::string installed_version(const char *package) {
    std::ifstream file("/var/lib/dpkg/status");std::string line,name,status,result;
    while(std::getline(file,line)) {
        if(line.empty()) {
            if(name==package&&status=="install ok installed")return result;
            name.clear();status.clear();result.clear();
        } else if(line.compare(0,9,"Package: ")==0)name=line.substr(9);
        else if(line.compare(0,8,"Status: ")==0)status=line.substr(8);
        else if(line.compare(0,9,"Version: ")==0)result=line.substr(9);
    }
    return name==package&&status=="install ok installed"?result:std::string();
}
static bool installed(const char *package) {
    return !installed_version(package).empty();
}
static bool package_name(const std::string &name) {
    if(name.empty() || name.size()>255)return false;
    bool architecture=false, beginning=true;
    for(unsigned char c:name) {
        bool alnum=(c>='a'&&c<='z')||(c>='0'&&c<='9');
        if(c==':'&&!architecture&&!beginning) {architecture=true;beginning=true;continue;}
        if(!alnum&&(beginning || (c!='-'&&c!='+'&&c!='.')))return false;
        beginning=false;
    }
    return !beginning;
}
static bool protected_toolchain(std::string name) {
    for(char &c:name)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    name=name.substr(0,name.find(':'));
    for(const char *word:{"c","cpp","c++","asm","gcc","g++","clang","clang++","binutils","nasm","yasm","build-essential","libc6-dev","linux-libc-dev"})
        if(name==word)return true;
    for(const char *prefix:{"gcc-","g++-","cpp-","clang-","binutils-","libstdc++-","libgcc-"})
        if(name.compare(0,strlen(prefix),prefix)==0)return true;
    return false;
}
static int protected_message(const std::string &name) {
    std::cerr<<"你的胆子也是肥嘟嘟的，居然敢卸载["<<name<<"]\n";
    return 1;
}
static int uninstall(const std::vector<std::string> &packages) {
    if(packages.empty())return 2;
    // Validate the complete request before changing anything, including mixed
    // requests such as "nano C++". The language aliases are case insensitive.
    for(const auto &name:packages) {
        if(protected_toolchain(name))return protected_message(name);
        if(!package_name(name)) {std::cerr<<"Uninstall expects package names only.\n";return 2;}
    }
    if(getuid()!=0||geteuid()!=0) {std::cerr<<"Use sudo mnos --uninstall PACKAGE...\n";return 1;}
    unsetenv("APT_CONFIG");setenv("PATH","/usr/sbin:/usr/bin:/sbin:/bin",1);setenv("LC_ALL","C",1);
    std::vector<std::string> plan{"/usr/bin/apt-get","--simulate","remove","--"};
    plan.insert(plan.end(),packages.begin(),packages.end());
    std::string output;int result=capture(plan,output);
    if(result) {std::cerr<<output;return result;}
    std::istringstream lines(output);std::string line;
    while(std::getline(lines,line)) {
        std::istringstream fields(line);std::string operation,name;fields>>operation>>name;
        if(operation=="Remv"&&protected_toolchain(name))return protected_message(name);
    }
    std::vector<std::string> args{"/usr/bin/apt-get","remove","--"};
    args.insert(args.end(),packages.begin(),packages.end());
    return run(args);
}
static bool excluded(const std::string &name,const std::vector<std::string> &names) {
    for(const auto &entry:names)
        if(entry==name || (entry.find(':')==std::string::npos && name.substr(0,name.find(':'))==entry))return true;
    return false;
}
static std::string preferences_path() {
    int fds[2];if(pipe(fds))throw std::runtime_error("Cannot read APT configuration.");
    pid_t child=fork();
    if(child<0) {close(fds[0]);close(fds[1]);throw std::runtime_error("Cannot read APT configuration.");}
    if(!child) {
        close(fds[0]);if(dup2(fds[1],STDOUT_FILENO)<0)_exit(127);close(fds[1]);
        execl("/usr/bin/apt-config","apt-config","shell","MNOS_PREFS","Dir::Etc::preferences/f",static_cast<char *>(nullptr));_exit(127);
    }
    close(fds[1]);std::string output;char buffer[1024];ssize_t count;
    while((count=read(fds[0],buffer,sizeof buffer))>0) {
        if(output.size()<65536)output.append(buffer,size_t(count));
    }
    close(fds[0]);int status=0;pid_t waited;
    do {waited=waitpid(child,&status,0);}while(waited<0&&errno==EINTR);
    if(waited!=child||!WIFEXITED(status)||WEXITSTATUS(status)||count<0||output.size()>=65536)
        throw std::runtime_error("Cannot read APT configuration.");
    if(output.empty())return {};
    const std::string prefix="MNOS_PREFS='";
    if(output.compare(0,prefix.size(),prefix)||output.size()<prefix.size()+2||output.substr(output.size()-2)!="'\n")
        throw std::runtime_error("Unsupported APT preferences configuration.");
    // Decode the one shell-quoted value as data; never evaluate its contents.
    std::string value=output.substr(prefix.size(),output.size()-prefix.size()-2),path;
    for(size_t i=0;i<value.size();++i) {
        if(value.compare(i,4,"'\\''")==0) {path+='\'';i+=3;}
        else if(value[i]=='\''||value[i]=='\n'||value[i]=='\r'||value[i]=='\0')
            throw std::runtime_error("Unsupported APT preferences path.");
        else path+=value[i];
    }
    if(!path.empty()&&path[0]!='/')throw std::runtime_error("APT preferences path must be absolute.");
    return path;
}
struct Preferences {
    std::string directory,path;
    Preferences(const std::vector<std::string> &names) {
        const auto source=preferences_path();std::string original,line;
        if(!source.empty()) {
            std::ifstream file(source);
            if(!file&&access(source.c_str(),F_OK)==0)throw std::runtime_error("Cannot read administrator APT preferences.");
            while(std::getline(file,line))original+=line+'\n';
            if(file.bad())throw std::runtime_error("Cannot read administrator APT preferences.");
        }
        char temp[]="/tmp/mnos-apt.XXXXXX";
        if(!mkdtemp(temp))throw std::runtime_error("Cannot create private APT preferences.");
        directory=temp;path=directory+"/preferences";
        std::ofstream file(path);
        // The first matching specific-package record wins. Keep administrator
        // preferences after these exclusions; preferences.d remains unchanged.
        for(const auto &name:names)
            file<<"Package: "<<name<<"\nPin: version *\nPin-Priority: -1\n\n";
        file<<original;
        file.close();
        if(!file) {unlink(path.c_str());rmdir(directory.c_str());throw std::runtime_error("Cannot write APT exclusions.");}
    }
    ~Preferences() {unlink(path.c_str());rmdir(directory.c_str());}
};
static int install_without(std::vector<std::string> packages,const std::vector<std::string> &without) {
    if(getuid()!=0||geteuid()!=0) {std::cerr<<"Use sudo for package installation.\n";return 1;}
    for(const auto &name:without)if(!package_name(name)) {std::cerr<<"--without expects package names only.\n";return 2;}
    for(const auto &name:packages)if(!package_name(name)) {std::cerr<<"With --without, use plain package names before the option.\n";return 2;}
    packages.erase(std::remove_if(packages.begin(),packages.end(),[&](const auto &name){return excluded(name,without);}),packages.end());
    if(packages.empty()) {std::cout<<"All requested packages are excluded; nothing was installed.\n";return 0;}
    unsetenv("APT_CONFIG");setenv("PATH","/usr/sbin:/usr/bin:/sbin:/bin",1);
    try {
        Preferences preferences(without);
        std::vector<std::string> args{"/usr/bin/apt-get","-o","Dir::Etc::preferences="+preferences.path,
            "install","--no-remove","--"};
        args.insert(args.end(),packages.begin(),packages.end());
        std::cout<<"Excluded packages:";for(const auto &name:without)std::cout<<' '<<name;
        std::cout<<"\nExisting packages will not be removed. Dependency conflicts stop installation.\n"<<std::flush;
        return run(args);
    } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"settings")) {
        execl("/usr/bin/mnos-region","mnos-region",(char *)nullptr);return 1;
    }
    if(argc==1||!strcmp(argv[1],"help")||!strcmp(argv[1],"--help")) {
        if(mnos_chinese()) {
            execl("/usr/bin/mnos-help", "mnos-help", "-cn", "updates", (char *)nullptr);
            perror("mnos-help");return 1;
        }
        std::cout << "mnos status | source | list [full|lite|patch|all] | packages [installed|full]\n"
          "mnos full | update | rollback VERSION | apt ...\n"
          "mnos --uninstall PACKAGE...: remove selected packages with APT confirmation.\n"
          "C/C++/ASM aliases and compiler toolchains are protected by this command.\n"
          "Queries need no root. Use sudo for full/update/rollback.\n"
          "full: add the full feature package, without reinstalling the OS.\n"
          "full --without PACKAGE...: omit these packages, including dependencies.\n"
          "apt install PACKAGE... --without PACKAGE...: install a selected package set.\n"
          "All names after --without are exclusions for this transaction; conflicts fail.\n"
          "Existing packages are not removed. Exclusions are not a permanent APT hold.\n"
          "list: fetch the signed GitHub Raw catalog of Full/Lite images and patches.\n"
          "packages: list every installed package; packages full lists Full components.\n"
          "update: refresh signed metadata and update installed mnOS packages.\n"
          "rollback VERSION: reinstall retained mnOS package versions; not disk/data rollback.\n"
          "apt ...: run the real Linux APT client. sudo apt upgrade updates Debian packages.\n"
          "Installing named Python packages or linux-libc-dev asks an extra y/N confirmation.\n"
          "APT -y does not bypass this notice; simulations and excluded packages do not prompt.\n"
          "Detailed English/Chinese manual: help updates / help -cn updates\n";
        return 0;
    }
    std::string command=argv[1];
    if(command=="--uninstall"||command==u8"—uninstall"||command=="uninstall") {
        std::vector<std::string> packages;for(int i=2;i<argc;++i)packages.emplace_back(argv[i]);
        return uninstall(packages);
    }
    if(command=="list"&&(argc==2||argc==3)) {
        std::string kind=argc==3?argv[2]:"all";
        if(kind!="all"&&kind!="full"&&kind!="lite"&&kind!="patch")return 2;
        try {Catalog catalog;return catalog.list(kind);}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
    }
    if(command=="packages"&&(argc==2||argc==3)) {
        if(argc==2||!strcmp(argv[2],"installed"))return run({"/usr/bin/dpkg-query","-W","-f=${binary:Package}\t${Version}\t${db:Status-Status}\n"});
        if(strcmp(argv[2],"full"))return 2;
        std::ifstream file("/usr/share/mnos/full-packages.txt");
        if(!file) {std::cerr<<"Full component manifest is unavailable.\n";return 1;}
        std::cout<<file.rdbuf();return 0;
    }
    int without_at=0;
    for(int i=2;i<argc;++i)if(!strcmp(argv[i],"--without")||!strcmp(argv[i],u8"—without")) {without_at=i;break;}
    std::vector<std::string> without;
    if(without_at) {
        if(without_at+1==argc) {std::cerr<<"Provide at least one package after --without.\n";return 2;}
        for(int i=without_at+1;i<argc;++i) {
            if(!package_name(argv[i])) {std::cerr<<"--without expects package names only.\n";return 2;}
            without.emplace_back(argv[i]);
        }
        if(command!="full"&&!(command=="apt"&&argc>3&&!strcmp(argv[2],"install"))) {
            std::cerr<<"Use --without with mnos full or mnos apt install.\n";return 2;
        }
    }
    if(command=="apt") {
        if(without_at) {
            if(without_at<=3)return 2;
            std::vector<std::string> packages;for(int i=3;i<without_at;++i)packages.emplace_back(argv[i]);
            return install_without(packages,without);
        }
        std::vector<std::string> words{"/usr/bin/apt"};for(int i=2;i<argc;++i)words.emplace_back(argv[i]);return run(words);
    }
    if(command=="source"&&argc==2) {
        std::ifstream source("/etc/apt/sources.list.d/mnos.sources");
        if(!source) {std::cerr<<"mnOS update source is not configured; no update was fetched.\n";return 1;}
        std::cout<<"Signed GitHub Raw catalog: "<<catalog_url<<'\n'<<source.rdbuf();return 0;
    }
    if(command=="status"&&argc==2) {
        std::string flavor;std::ifstream("/usr/share/mnos/flavor")>>flavor;
        std::cout<<"Edition features: "<<(flavor.empty()?"unknown":flavor)<<'\n'<<std::flush;
        return run({"/usr/bin/dpkg-query","-W","-f=${binary:Package}\t${Version}\n","mnos-runtime","mnos-auth","mnos-full"});
    }
    bool rollback=command=="rollback";
    if((command!="full"&&command!="update"&&!rollback)||(rollback?(argc!=3||!version(argv[2])):(without_at?without_at!=2:argc!=2)))return 2;
    if(getuid()!=0||geteuid()!=0) {std::cerr<<"Use sudo mnos "<<command<<" for system changes.\n";return 1;}
    if(!std::ifstream("/etc/apt/sources.list.d/mnos.sources")||!std::ifstream("/usr/share/keyrings/mnos-archive.gpg")) {
        std::cerr<<"A signed mnOS resource source is required. No update was attempted.\n";return 1;
    }
    unsetenv("APT_CONFIG");setenv("PATH","/usr/sbin:/usr/bin:/sbin:/bin",1);
    std::vector<std::string> features;
    std::string update_version;
    if(command=="update") {
        try {
            Catalog catalog;update_version=catalog.latest();
            if(!version(update_version))throw std::runtime_error("Invalid published runtime version.");
            std::cout<<"Latest published mnOS runtime: "<<update_version<<'\n'<<std::flush;
            const auto current=installed_version("mnos-runtime");
            if(!current.empty()&&run({"/usr/bin/dpkg","--compare-versions",update_version,"lt",current})==0) {
                std::cout<<"Installed version is newer; update will not downgrade it. Use an explicit rollback if needed.\n";
                return 0;
            }
        } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
    }
    if(command=="full"&&without_at) {
        std::ifstream manifest("/usr/share/mnos/full-packages.txt");std::string name;
        while(std::getline(manifest,name)) {
            if(!package_name(name)) {std::cerr<<"Invalid Full feature manifest.\n";return 1;}
            features.push_back(name);
        }
        if(!manifest.eof()||features.empty()) {std::cerr<<"Full feature manifest is unavailable.\n";return 1;}
    }
    int result=run({"/usr/bin/apt-get","-o","APT::Update::Error-Mode=any","update"});if(result)return result;
    if(command=="full"&&without_at) {
        result=install_without(features,without);
        if(!result)std::cout<<"Selected Full features installed. The all-features mnos-full metapackage was not requested.\n";
        return result;
    }
    std::vector<std::string> args{"/usr/bin/apt-get","install"};
    if(rollback) {
        std::cout<<"Rollback affects mnOS runtime/authentication packages only. It does not restore Linux, third-party packages or personal data.\n"<<std::flush;
        args.emplace_back("--allow-downgrades");
    } else if(command=="update")args.emplace_back("--only-upgrade");
    args.emplace_back("--");
    std::string suffix=rollback?"="+std::string(argv[2]):(update_version.empty()?"":"="+update_version);
    if(command=="full")args.emplace_back("mnos-full");
    else {
        if(!installed("mnos-runtime")) {std::cerr<<"This installation has no managed mnOS runtime package. Install the matching bootstrap package first.\n";return 1;}
        args.emplace_back("mnos-runtime"+suffix);
        if(installed("mnos-auth"))args.emplace_back("mnos-auth"+suffix);
        if(installed("mnos-full"))args.emplace_back("mnos-full"+suffix);
    }
    return run(args);
}
