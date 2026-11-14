/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real ISO/disk GUI, decoded screenshot QR, independent OpenSSL authenticator.
 * All credentials here belong only to the disposable test machine. */
#define main desktop_regression_main
#include "desktop_vm.cpp"
#undef main
#include <openssl/hmac.h>
#include <array>
#include <ctime>
static std::string scan(const fs::path &path) {
    int pipefd[2];require(!pipe(pipefd),"QR pipe");pid_t child=fork();require(child>=0,"QR fork");
    if(child==0) {close(pipefd[0]);if(dup2(pipefd[1],1)<0)_exit(127);close(pipefd[1]);execlp("zbarimg","zbarimg","--quiet","--raw",path.c_str(),(char *)0);_exit(127);}
    close(pipefd[1]);std::string result;char bytes[1024];ssize_t n;
    while((n=read(pipefd[0],bytes,sizeof bytes))>0) {result.append(bytes,size_t(n));require(result.size()<4096,"QR result size");}
    close(pipefd[0]);int status;require(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"QR screenshot did not decode");return result;
}
static std::array<uint8_t,32> seed(const std::string &uri) {
    require(uri.find("otpauth://totp/mnOS:demo?")==0&&uri.find("algorithm=SHA1")!=std::string::npos,"Authenticator URI parameters");
    size_t begin=uri.find("secret=");require(begin!=std::string::npos,"QR has no key");begin+=7;
    size_t end=uri.find('&',begin);require(end-begin==52,"QR key length");
    std::array<uint8_t,32> result{};uint32_t value=0;unsigned bits=0,at=0;const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    for(size_t i=begin;i<end;++i) {auto digit=alphabet.find(uri[i]);require(digit!=std::string::npos,"QR base32");value=(value<<5)|uint32_t(digit);bits+=5;if(bits>=8) {bits-=8;require(at<32,"QR key bounds");result[at++]=uint8_t(value>>bits);}}
    require(at==32,"QR seed bytes");return result;
}
static std::string otp(const std::array<uint8_t,32> &key,uint64_t &last) {
    while(uint64_t(std::time(nullptr))/30<=last)std::this_thread::sleep_for(200ms);
    last=uint64_t(std::time(nullptr))/30;uint8_t counter[8],digest[EVP_MAX_MD_SIZE];unsigned length;
    for(unsigned i=0;i<8;++i)counter[7-i]=uint8_t(last>>(8*i));
    require(HMAC(EVP_sha1(),key.data(),32,counter,8,digest,&length)&&length==20,"OpenSSL HMAC");
    unsigned at=digest[19]&15;uint32_t n=((uint32_t(digest[at])&127)<<24)|(uint32_t(digest[at+1])<<16)|(uint32_t(digest[at+2])<<8)|digest[at+3];
    char text[7];snprintf(text,sizeof text,"%06u",n%1000000);OPENSSL_cleanse(digest,sizeof digest);return text;
}
int main(int argc,char **argv) {
    try {
        require(argc==3,"usage: factor-vm BUILD/DESKTOP amd64|arm64");auto build=fs::canonical(argv[1]);std::string arch=argv[2];
        require(arch=="amd64"||arch=="arm64","arch");char directory[]="/tmp/mnos-factor-XXXXXX";require(mkdtemp(directory),"temporary disk");fs::path work=directory;
        std::ofstream(work/"disk.img").close();fs::resize_file(work/"disk.img",256ULL<<20);
        {
            VM vm(build,work,arch,0);vm.wait("INSTALLER READY");vm.send("\r");vm.wait("INSTALL CONFIRM");vm.send("ERASE\r");vm.wait("INSTALL PASS");
        }
        std::array<uint8_t,32> secret{};uint64_t last=0;
        {
            VM vm(build,work,arch,1);vm.login(true,true);
            vm.command("ordinarytest491","AUTH PROMPT: Enter the authenticator");
            auto screenshot=build/("factor-"+arch+"-qr.ppm");vm.screenshot(screenshot);std::string uri=scan(screenshot);secret=seed(uri);
            require(vm.output.find("secret=")==std::string::npos,"Enrollment key leaked to UART");
            vm.command(otp(secret,last),"2FA enabled. Store all 8 one-use recovery codes");vm.send(std::string(8,'\n'));vm.wait("Recovery codes acknowledged");
            vm.page_key("f5",4);vm.command("2fa status","Password + TOTP enabled.");
            OPENSSL_cleanse(uri.data(),uri.size());fs::remove(screenshot);
        }
        {
            VM vm(build,work,arch,2);vm.wait("AUTH PROMPT: Username:");vm.command("demo","AUTH PROMPT: Password:");vm.command("ordinarytest491","Authenticator code or an unused recovery code:");
            require(vm.output.find("DESKTOP READY")==std::string::npos,"Password alone opened 2FA desktop");
            vm.screenshot(build/("factor-"+arch+"-login.ppm"));
            vm.command("xxxxxx","Authentication failed");require(vm.output.find("DESKTOP READY")==std::string::npos,"Invalid code opened desktop");
            std::this_thread::sleep_for(3s);
            vm.command("demo","AUTH PROMPT: Password:");vm.command("ordinarytest491","Authenticator code or an unused recovery code:");
            vm.command(otp(secret,last),"Signed in as an ordinary user.");vm.wait("DESKTOP READY");
            vm.page_key("f5",4);vm.command("2fa status","Password + TOTP enabled.");
            vm.command("2fa disable","AUTH PROMPT: Password:");vm.command("ordinarytest491","Authenticator code or an unused recovery code:");
            vm.command(otp(secret,last),"Two-factor authentication disabled.");
        }
        OPENSSL_cleanse(secret.data(),secret.size());fs::remove_all(work);
        std::cout<<"2FA VM PASS "<<arch<<": real install, QR scan, independent TOTP, reboot persistence, password-only denial, wrong-code denial, login and authenticated disable\n";return 0;
    } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
