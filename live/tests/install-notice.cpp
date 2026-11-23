// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/install-notice.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
static void answer(const std::string &text,bool expected,const char *remaining="") {
    int fds[2];assert(pipe(fds)==0);
    assert(write(fds[1],text.data(),text.size())==static_cast<ssize_t>(text.size()));close(fds[1]);
    assert(mnos::read_install_confirmation(fds[0])==expected);
    char buffer[128];const auto n=read(fds[0],buffer,sizeof buffer);assert(n>=0);
    assert(std::string(buffer,static_cast<size_t>(n))==remaining);close(fds[0]);
}
int main() {
    using namespace mnos;
    for(const auto *name:{"python3","python3.13-minimal","python3-pip","libpython3.13","pypy3","python3:riscv64","python3=3.13","python3/trixie","python3+"})
        assert(package_notice(name)==Python);
    assert(package_notice("linux-libc-dev:amd64")==LinuxHeaders);
    for(const auto *name:{"python3-","pythonista","libpythonista","nano","linux-libc-dev-tools","./python3.deb","/tmp/python3.deb"})
        assert(package_notice(name)==0);
    assert(install_notices({"/usr/bin/apt","-y","install","python3","linux-libc-dev"})==(Python|LinuxHeaders));
    assert(install_notices({"/usr/bin/apt-get","-o","Dir::Etc::preferences=/tmp/preferences","install","--","python3"})==Python);
    assert(install_notices({"/usr/bin/apt","-c","install","show","python3"})==0);
    assert(install_notices({"/usr/bin/apt","-o","python3","install","nano"})==0);
    assert(install_notices({"/usr/bin/apt","install","-s","python3"})==0);
    assert(install_notices({"/usr/bin/apt-get","--download-only","install","python3"})==0);
    assert(install_notices({"/usr/bin/apt","remove","python3"})==0);
    assert(install_notices({"/usr/bin/apt","search","python3"})==0);
    assert(install_notices({"/usr/bin/apt","install","python3-","nano"})==0);
    assert(install_notices({"/usr/bin/apt-cache","show","python3"})==0);
    answer("Y\nAPT_ANSWER\n",true,"APT_ANSWER\n");
    answer("  YeS \r\n",true);
    answer("n\n",false);answer("\n",false);answer("",false);answer("yes",false);
    answer("yes please\n",false);answer(std::string(65,'y')+"\n",false);
    std::cout<<"PASS install notices: package selectors, exclusions/removal, APT options, dry runs, default cancel and preservation of the next APT answer\n";
}
