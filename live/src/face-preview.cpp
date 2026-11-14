// SPDX-License-Identifier: GPL-3.0-or-later
// Monocular enrollment/test preview. Deliberately NOT a PAM authentication
// method: synthetic challenge tests cannot certify camera spoof resistance.
#include "security-ui.hpp"
#include "face-challenge.hpp"
extern "C" {
#include "monocypher.h"
}
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect/face.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fcntl.h>
#include <mutex>
#include <pwd.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace mnos_face;
using Steady=std::chrono::steady_clock;
static void require(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
static bool random_bytes(uint8_t *p,size_t size) {
    while(size) {ssize_t n=getrandom(p,size,0);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;size-=size_t(n);}return true;
}
static double seconds() {return std::chrono::duration<double>(Steady::now().time_since_epoch()).count();}
struct Sensitive {
    std::array<uint8_t,32> key{};
    std::array<float,128> reference{};
    Sensitive() {require(!mlock(this,sizeof *this),"无法保护认证内存，请重试。");}
    ~Sensitive() {auth_wipe(this,sizeof *this);munlock(this,sizeof *this);}
};
struct Profile {
    uint32_t magic=0x3143464d,version=1,uid=0,rounds=AUTH_ROUNDS;
    std::array<uint8_t,16> salt{};
    std::array<uint8_t,24> nonce{};
    std::array<uint8_t,16> tag{};
    std::array<uint8_t,512> encrypted{};
};
static int profile_directory() {
    auto *user=getpwuid(getuid());require(user&&getuid()>=1000,"请以普通用户打开人脸设置。");
    int fd=open(user->pw_dir,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(fd>=0,"无法打开用户目录。");
    for(const char *name:{".local","share","mnos","security"}) {
        if(mkdirat(fd,name,0700)&&errno!=EEXIST) {close(fd);throw std::runtime_error("无法创建私有人脸配置目录。");}
        int next=openat(fd,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(fd);fd=next;
        struct stat s{};
        if(fd<0||fstat(fd,&s)||s.st_uid!=getuid()||(s.st_mode&0022)) {if(fd>=0)close(fd);throw std::runtime_error("人脸配置目录的所有者或权限不正确。");}
    }
    if(fchmod(fd,0700)) {close(fd);throw std::runtime_error("无法保护人脸配置目录。");}return fd;
}
static void load_profile(int dir,Profile &profile) {
    int fd=openat(dir,"face-v1",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);require(fd>=0,"尚未录入人脸，请先选择录入。");struct stat s{};
    bool ok=!fstat(fd,&s)&&S_ISREG(s.st_mode)&&s.st_nlink==1&&s.st_uid==getuid()&&!(s.st_mode&077)&&s.st_size==sizeof profile;
    if(ok)ok=read(fd,&profile,sizeof profile)==sizeof profile;close(fd);
    require(ok&&profile.magic==0x3143464d&&profile.version==1&&profile.rounds==AUTH_ROUNDS&&profile.uid==getuid(),"人脸配置损坏或不属于此账号。");
}
static void save_profile(int dir,const Profile &profile) {
    std::array<uint8_t,8> random{};require(random_bytes(random.data(),random.size()),"安全随机源不可用。");
    char name[40]=".face-";for(unsigned i=0;i<8;++i)snprintf(name+6+i*2,3,"%02x",random[i]);
    int fd=openat(dir,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);require(fd>=0,"无法保存人脸配置。");
    bool ok=write(fd,&profile,sizeof profile)==sizeof profile&&!fsync(fd);if(close(fd))ok=false;
    if(ok)ok=!renameat(dir,name,dir,"face-v1")&&!fsync(dir);if(!ok)unlinkat(dir,name,0);
    require(ok,"人脸配置未能可靠保存，请重试。");
}
static bool password_key(GtkWindow *parent,const Profile &profile,Sensitive &s) {
    int child=-1,fd=login_child(getuid(),0,&child);require(fd>=0,"认证服务不可用。");
    struct login_response r{};bool ok=!login_send(fd,LOGIN_STATUS,getuid(),"","")&&!login_read(fd,&r,sizeof r);close(fd);waitpid(child,nullptr,0);
    require(ok&&(r.result==LOGIN_OK||r.result==LOGIN_ABSENT),"无法读取账号认证设置。");bool factor=r.result==LOGIN_OK;
    auto *d=security::dialog(parent,"人脸设置：先验证账号","继续");auto *box=gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_box_pack_start(GTK_BOX(box),security::label("输入账号密码。人脸模板只在本机加密保存，不保存摄像头照片或录像。"),false,false,8);
    auto *password=gtk_entry_new();gtk_entry_set_visibility(GTK_ENTRY(password),false);gtk_box_pack_start(GTK_BOX(box),password,false,false,8);
    GtkWidget *code=nullptr;if(factor) {gtk_box_pack_start(GTK_BOX(box),security::label("验证器验证码或恢复码："),false,false,8);code=gtk_entry_new();gtk_entry_set_visibility(GTK_ENTRY(code),false);gtk_box_pack_start(GTK_BOX(box),code,false,false,8);}
    gtk_widget_show_all(d);bool accept=gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT;
    if(accept) {
        fd=login_child(getuid(),0,&child);
        ok=fd>=0&&!login_send(fd,LOGIN_VERIFY,getuid(),gtk_entry_get_text(GTK_ENTRY(password)),code?gtk_entry_get_text(GTK_ENTRY(code)):"")&&!login_read(fd,&r,sizeof r);
        if(fd>=0) {close(fd);waitpid(child,nullptr,0);}
        ok=ok&&(r.result==LOGIN_OK||(!factor&&r.result==LOGIN_ABSENT));
        if(ok) {
            struct password_kdf kdf;password_begin(&kdf,gtk_entry_get_text(GTK_ENTRY(password)),profile.salt.data(),AUTH_ROUNDS);
            while(!password_step(&kdf,4096)){}memcpy(s.key.data(),kdf.result,32);auth_wipe(&kdf,sizeof kdf);
        }
    } else ok=false;
    gtk_entry_set_text(GTK_ENTRY(password),"");if(code)gtk_entry_set_text(GTK_ENTRY(code),"");gtk_widget_destroy(d);
    if(accept&&!ok)security::notice(parent,"认证未通过，请检查密码和验证码后重试。");return ok;
}
static std::array<float,128> embedding(cv::Ptr<cv::FaceRecognizerSF> recognizer,const cv::Mat &frame,const cv::Mat &face) {
    cv::Mat aligned,feature;recognizer->alignCrop(frame,face,aligned);recognizer->feature(aligned,feature);
    require(feature.total()==128&&feature.type()==CV_32F,"人脸模型输出格式不匹配。");
    std::array<float,128> out{};double norm=cv::norm(feature);require(norm>0&&std::isfinite(norm),"人脸特征无效。");
    for(unsigned i=0;i<128;++i)out[i]=float(feature.ptr<float>()[i]/norm);
    aligned.setTo(0);feature.setTo(0);return out;
}
static double cosine(const std::array<float,128> &a,const std::array<float,128> &b) {double n=0;for(unsigned i=0;i<128;++i)n+=double(a[i])*b[i];return n;}
struct Capture {
    Sensitive &s;bool enroll;std::atomic<bool> stop{false},done{false};std::mutex mutex;
    cv::Mat preview;std::string message="正在打开本机摄像头…";unsigned light=3;bool success=false;std::thread worker;
    Capture(Sensitive &state,bool create):s(state),enroll(create),worker([this] {run();}) {}
    ~Capture() {stop=true;if(worker.joinable())worker.join();preview.setTo(0);}
    void run() {
        try {
            auto detector=cv::FaceDetectorYN::create("/usr/share/mnos/face/yunet.onnx","",cv::Size(640,480),0.9f);
            auto recognizer=cv::FaceRecognizerSF::create("/usr/share/mnos/face/sface.onnx","");
            cv::CascadeClassifier eyes;require(eyes.load("/usr/share/opencv4/haarcascades/haarcascade_eye_tree_eyeglasses.xml"),"缺少眼部检测模型。");
            cv::VideoCapture camera(0,cv::CAP_V4L2);require(camera.isOpened(),"未检测到可用摄像头。请连接或向虚拟机转发 USB 摄像头。");
            camera.set(cv::CAP_PROP_FRAME_WIDTH,640);camera.set(cv::CAP_PROP_FRAME_HEIGHT,480);camera.set(cv::CAP_PROP_BUFFERSIZE,1);
            std::array<uint8_t,32> random{};require(random_bytes(random.data(),random.size()),"安全随机源不可用。");
            Challenge challenge(random,seconds());bool reference_ready=!enroll;std::array<double,128> sum{};unsigned count=0;
            while(!stop&&challenge.state()!=Task::Complete&&challenge.state()!=Task::Failed) {
                cv::Mat frame,faces;require(camera.read(frame)&&!frame.empty(),"摄像头没有返回画面。");
                detector->setInputSize(frame.size());detector->detect(frame,faces);
                Sample sample;sample.time=seconds();sample.faces=unsigned(faces.rows);
                if(faces.rows==1) {
                    auto face=faces.row(0);const float *f=face.ptr<float>();cv::Rect bounds{int(f[0]),int(f[1]),int(f[2]),int(f[3])};bounds&=cv::Rect(0,0,frame.cols,frame.rows);
                    if(bounds.width>=140&&bounds.height>=140) {
                        auto feature=embedding(recognizer,frame,face);
                        if(!reference_ready) {s.reference=feature;reference_ready=true;}
                        sample.match=cosine(feature,s.reference);sample.usable=true;
                        std::vector<cv::Point2d> landmarks;
                        for(unsigned i=0;i<5;++i)landmarks.emplace_back(f[4+i*2],f[5+i*2]);
                        if(landmarks[0].x>landmarks[1].x)std::swap(landmarks[0],landmarks[1]);
                        if(landmarks[3].x>landmarks[4].x)std::swap(landmarks[3],landmarks[4]);
                        std::vector<cv::Point3d> model{{-32,34,-26},{32,34,-26},{0,0,0},{-23,-25,-20},{23,-25,-20}};
                        cv::Mat camera_matrix=(cv::Mat_<double>(3,3)<<frame.cols,0,frame.cols/2.,0,frame.cols,frame.rows/2.,0,0,1),rotation,translation,matrix;
                        sample.usable=cv::solvePnP(model,landmarks,camera_matrix,cv::noArray(),rotation,translation,false,cv::SOLVEPNP_EPNP);
                        if(sample.usable) {cv::Rodrigues(rotation,matrix);sample.yaw=std::atan2(matrix.at<double>(0,2),matrix.at<double>(2,2))*180/CV_PI;}
                        cv::Mat gray;cv::cvtColor(frame(bounds),gray,cv::COLOR_BGR2GRAY);std::vector<cv::Rect> detected;
                        eyes.detectMultiScale(gray(cv::Rect(0,0,gray.cols,gray.rows/2)),detected,1.1,3,0,cv::Size(16,16));sample.eyes_open=detected.size()>=2;
                        cv::Rect cheeks(bounds.x+bounds.width/4,bounds.y+bounds.height/2,bounds.width/2,bounds.height/4);cv::Scalar color=cv::mean(frame(cheeks));
                        double total=color[0]+color[1]+color[2];if(total<30||total>730)sample.usable=false;
                        if(total>0)sample.color={color[2]/total,color[1]/total,color[0]/total};
                        if(enroll&&sample.usable&&sample.match>=0.55) {for(unsigned i=0;i<128;++i)sum[i]+=feature[i];++count;}
                        auth_wipe(feature.data(),sizeof feature);gray.setTo(0);cv::rectangle(frame,bounds,cv::Scalar(160,220,180),2);
                    }
                }
                challenge.sample(sample);
                const char *prompt="保持正视镜头、睁开双眼，脸部完整位于画面中。";
                switch(challenge.state()) {
                case Task::Blink:prompt="请缓慢眨眼，再睁开。";break;
                case Task::Left:prompt="请向一侧缓慢转头（左）。";break;
                case Task::Right:prompt="请向另一侧缓慢转头（右）。";break;
                case Task::Color:prompt="回到正面，保持不动；圆环将缓慢变色。";break;
                case Task::Complete:prompt="本次动作与颜色响应检查完成。";break;
                case Task::Failed:prompt="本次采集未通过，请改善光线后重试。";break;
                default:break;
                }
                {std::lock_guard<std::mutex> guard(mutex);cv::cvtColor(frame,preview,cv::COLOR_BGR2BGRA);light=challenge.light();message=prompt;}
                frame.setTo(0);std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            success=!stop&&challenge.state()==Task::Complete;
            if(success&&enroll) {
                double norm=0;for(double v:sum)norm+=v*v;norm=std::sqrt(norm);require(count>=10&&norm>0,"有效人脸样本不足。");
                for(unsigned i=0;i<128;++i)s.reference[i]=float(sum[i]/norm);
            }
            auth_wipe(sum.data(),sizeof sum);camera.release();
        } catch(const std::exception &) {
            std::lock_guard<std::mutex> guard(mutex);success=false;message="无法完成摄像头采集。请检查摄像头转发、设备权限、模型文件和光线。";
        }
        done=true;
    }
};
struct View {
    Capture &capture;GtkWidget *window{},*drawing{},*status{},*close{};guint timer=0;bool accepted=false;
    View(Capture &c):capture(c) {
        window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(window),"人脸辅助采集 / Experimental face preview");gtk_window_set_default_size(GTK_WINDOW(window),800,680);
        auto *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);gtk_container_set_border_width(GTK_CONTAINER(box),16);gtk_container_add(GTK_CONTAINER(window),box);
        gtk_box_pack_start(GTK_BOX(box),security::label("普通摄像头 · 本地处理 · 实验性采集\n此页面用于录入与测试，尚不作为系统登录凭据，也不替代密码或验证器。"),false,false,0);
        drawing=gtk_drawing_area_new();gtk_widget_set_size_request(drawing,720,510);gtk_box_pack_start(GTK_BOX(box),drawing,true,true,0);
        status=security::label("正在准备…");gtk_box_pack_start(GTK_BOX(box),status,false,false,0);close=gtk_button_new_with_label("取消");gtk_box_pack_start(GTK_BOX(box),close,false,false,0);
        g_signal_connect(drawing,"draw",G_CALLBACK((+[](GtkWidget *w,cairo_t *cr,gpointer data)->gboolean {
            auto *view=static_cast<View *>(data);std::lock_guard<std::mutex> guard(view->capture.mutex);auto &frame=view->capture.preview;
            double width=gtk_widget_get_allocated_width(w),height=gtk_widget_get_allocated_height(w);
            cairo_set_source_rgb(cr,.04,.07,.09);cairo_paint(cr);
            if(!frame.empty()) {
                double scale=std::min((width-50)/frame.cols,(height-50)/frame.rows);
                cairo_save(cr);cairo_translate(cr,(width-frame.cols*scale)/2,(height-frame.rows*scale)/2);cairo_scale(cr,scale,scale);
                auto *surface=cairo_image_surface_create_for_data(frame.data,CAIRO_FORMAT_RGB24,frame.cols,frame.rows,int(frame.step));cairo_set_source_surface(cr,surface,0,0);cairo_paint(cr);cairo_surface_destroy(surface);cairo_restore(cr);
            }
            unsigned color=view->capture.light;cairo_set_source_rgb(cr,color==0?.9:.3,color==1?.85:.3,color==2?.9:.3);cairo_set_line_width(cr,20);
            cairo_arc(cr,width/2,height/2,std::min(width,height)/2-12,0,2*CV_PI);cairo_stroke(cr);return false;
        })),this);
        g_signal_connect_swapped(close,"clicked",G_CALLBACK((+[](View *view) {view->accepted=view->capture.done&&view->capture.success;view->capture.stop=true;gtk_widget_destroy(view->window);})),this);
        g_signal_connect(window,"destroy",G_CALLBACK(gtk_main_quit),nullptr);
        timer=g_timeout_add(100,+[](gpointer data)->gboolean {
            auto *view=static_cast<View *>(data);std::lock_guard<std::mutex> guard(view->capture.mutex);
            gtk_label_set_text(GTK_LABEL(view->status),view->capture.message.c_str());gtk_widget_queue_draw(view->drawing);
            if(view->capture.done)gtk_button_set_label(GTK_BUTTON(view->close),"完成 / 关闭");return G_SOURCE_CONTINUE;
        },this);gtk_widget_show_all(window);
    }
    ~View() {if(timer)g_source_remove(timer);}
};
int main(int argc,char **argv) {
    if(argc>1&&(!strcmp(argv[1],"--help")||!strcmp(argv[1],"-h"))) {
        puts("mnos-face-preview --enroll | --test | --delete\nLocal, experimental ordinary-camera enrollment and challenge testing.\nPassword reauthentication is required; enrolled TOTP is also checked.\nTemplates are encrypted; photos/video are not saved.\nThis preview does not authenticate system login or measure trusted depth.\nAfter a password change, enroll again. Manual: help -cn 2fa");return 0;
    }
    const rlimit limit{0,0};if(setrlimit(RLIMIT_CORE,&limit)||prctl(PR_SET_DUMPABLE,0))return 1;
    gtk_init(&argc,&argv);int directory=-1;
    try {
        const char *mode=argc==2?argv[1]:"";
        if(!strcmp(mode,"--first-run")) {
            char path[96];snprintf(path,sizeof path,"/var/lib/mnos/face-setup/%u",getuid());struct stat st{};
            if(lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&0022))return 0;
            directory=profile_directory();int marker=openat(directory,"preview-offered",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
            if(marker<0) {close(directory);return 0;}close(marker);close(directory);directory=-1;mode="--enroll";
        }
        require(!strcmp(mode,"--enroll")||!strcmp(mode,"--test")||!strcmp(mode,"--delete"),"使用 --enroll 录入、--test 测试或 --delete 删除人脸模板。");
        bool enroll=!strcmp(mode,"--enroll"),remove=!strcmp(mode,"--delete");directory=profile_directory();Profile profile;profile.uid=getuid();Sensitive sensitive;
        if(enroll)require(random_bytes(profile.salt.data(),16)&&random_bytes(profile.nonce.data(),24),"安全随机源不可用。");else load_profile(directory,profile);
        if(!password_key(nullptr,profile,sensitive)) {close(directory);return 1;}
        if(!enroll)require(!crypto_aead_unlock(reinterpret_cast<uint8_t *>(sensitive.reference.data()),profile.tag.data(),sensitive.key.data(),profile.nonce.data(),reinterpret_cast<const uint8_t *>(&profile),offsetof(Profile,nonce),profile.encrypted.data(),512),"模板密码不匹配或文件损坏。修改账号密码后需要重新录入。");
        if(remove) {require(!unlinkat(directory,"face-v1",0)&&!fsync(directory),"未能删除模板。");security::notice(nullptr,"人脸模板已删除。");close(directory);return 0;}
        bool success=false;
        {Capture capture(sensitive,enroll);bool accepted=false;{View view(capture);gtk_main();accepted=view.accepted;}capture.stop=true;if(capture.worker.joinable())capture.worker.join();success=accepted&&capture.done&&capture.success;}
        if(success&&enroll) {
            crypto_aead_lock(profile.encrypted.data(),profile.tag.data(),sensitive.key.data(),profile.nonce.data(),reinterpret_cast<const uint8_t *>(&profile),offsetof(Profile,nonce),reinterpret_cast<const uint8_t *>(sensitive.reference.data()),512);
            save_profile(directory,profile);security::notice(nullptr,"人脸模板已加密保存。此版本用于采集与测试，尚未接入登录。");
        }
        close(directory);return success?0:1;
    } catch(const std::exception &error) {if(directory>=0)close(directory);security::notice(nullptr,error.what());return 1;}
}
