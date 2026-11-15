// SPDX-License-Identifier: GPL-3.0-or-later
// Boot the release EFI image without CPU/VirtIO RNG. A TPM is optional on
// physical machines: enrollment must succeed with it and fail without entropy.
#define main random_vm_main
#include "random_vm.cpp"
#undef main

static void run(std::vector<std::string> args) {
    pid_t p=fork();check(p>=0,"command fork");
    if(!p) {
        std::vector<char *> a;for(auto &s:args)a.push_back(s.data());a.push_back(nullptr);
        execvp(a[0],a.data());_exit(127);
    }
    int status=0;check(waitpid(p,&status,0)==p,"command wait");
    check(WIFEXITED(status)&&WEXITSTATUS(status)==0,"Command failed: "+args[0]);
}
struct FirmwareFixture {
    fs::path work;pid_t tpm=-1;
    FirmwareFixture() {
        char temp[]="/tmp/mnos-firmware-XXXXXX";check(mkdtemp(temp),"mkdtemp");work=temp;
    }
    ~FirmwareFixture() {
        if(tpm>0){kill(tpm,SIGTERM);waitpid(tpm,nullptr,0);}
        std::error_code error;fs::remove_all(work,error);
    }
    void prepare(const fs::path &efi) {
        const uintmax_t mib=1024*1024,bytes=fs::file_size(efi);
        check(bytes&&bytes<1024*mib,"Unexpected EFI payload size");
        uintmax_t size=((bytes+16*mib+mib-1)/mib)*mib;if(size<64*mib)size=64*mib;
        auto esp=work/"esp.img";
        std::ofstream image(esp,std::ios::binary);image.seekp(size-1);image.put(0);
        check(bool(image),"Create disposable EFI disk");image.close();
        run({"mkfs.vfat","-F","32",esp.string()});
        run({"mmd","-i",esp.string(),"::/EFI","::/EFI/BOOT"});
        run({"mcopy","-i",esp.string(),efi.string(),"::/EFI/BOOT/BOOTX64.EFI"});
        fs::create_directory(work/"state");
        tpm=fork();check(tpm>=0,"swtpm fork");
        if(!tpm) {
            std::string state="dir="+(work/"state").string();
            std::string socket="type=unixio,path="+(work/"tpm.sock").string();
            execlp("swtpm","swtpm","socket","--tpm2","--tpmstate",state.c_str(),"--ctrl",socket.c_str(),(char *)nullptr);
            _exit(127);
        }
        auto until=std::chrono::steady_clock::now()+10s;
        while(!fs::exists(work/"tpm.sock")) {
            int status=0;pid_t exited=waitpid(tpm,&status,WNOHANG);
            if(exited==tpm){tpm=-1;throw std::runtime_error("swtpm exited before opening its socket");}
            check(std::chrono::steady_clock::now()<until,"swtpm socket timeout");
            std::this_thread::sleep_for(50ms);
        }
    }
};
int main(int argc,char **argv) {
    try {
        check(argc==3,"Usage: firmware-vm DESKTOP_BUILD OUTPUT");
        auto build=fs::absolute(argv[1]),out=fs::absolute(argv[2]);fs::create_directories(out);
        FirmwareFixture fixture;fixture.prepare(build/"amd64/BOOTX64.EFI");
        for(bool present:{true,false}) {
            fs::copy_file("/usr/share/OVMF/OVMF_VARS_4M.fd",fixture.work/"vars.fd",fs::copy_options::overwrite_existing);
            std::vector<std::string> args={"qemu-system-x86_64","-machine","q35",
                "-cpu","max,rdrand=off,rdseed=off","-vga","std",
                "-drive","if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
                "-drive","if=pflash,format=raw,file="+(fixture.work/"vars.fd").string(),
                "-drive","file="+(fixture.work/"esp.img").string()+",format=raw,readonly=on,if=virtio"};
            if(present)args.insert(args.end(),{
                "-chardev","socket,id=tpmsock,path="+(fixture.work/"tpm.sock").string(),
                "-tpmdev","emulator,id=tpm0,chardev=tpmsock","-device","tpm-tis,tpmdev=tpm0"});
            Guest vm(args,out/(present?"firmware-tpm-present.log":"firmware-tpm-missing.log"),2048);
            vm.enrollment(present);
            if(present)check(vm.output.find("RNG: TPM 2.0 firmware seed ready")!=std::string::npos,"TPM firmware seed not used");
            std::cout<<"FIRMWARE RNG BOOT PASS: CPU RNG and VirtIO RNG absent, TPM "
                <<(present?"allows enrollment":"missing fails closed")<<std::endl;
        }
        return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
}
