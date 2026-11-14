// SPDX-License-Identifier: GPL-3.0-or-later
// Camera capture shared by the unprivileged worker and tests.
// RGB challenges are not trusted depth or certified presentation-attack detection.
#pragma once
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
    Sensitive() {require(!mlock(this,sizeof *this),i18n::text("无法保护认证内存，请重试。"));}
    ~Sensitive() {auth_wipe(this,sizeof *this);munlock(this,sizeof *this);}
};
static std::array<float,128> embedding(cv::Ptr<cv::FaceRecognizerSF> recognizer,const cv::Mat &frame,const cv::Mat &face) {
    cv::Mat aligned,feature;recognizer->alignCrop(frame,face,aligned);recognizer->feature(aligned,feature);
    require(feature.total()==128&&feature.type()==CV_32F,i18n::text("人脸模型输出格式不匹配。"));
    std::array<float,128> out{};double norm=cv::norm(feature);require(norm>0&&std::isfinite(norm),i18n::text("人脸特征无效。"));
    for(unsigned i=0;i<128;++i)out[i]=float(feature.ptr<float>()[i]/norm);
    aligned.setTo(0);feature.setTo(0);return out;
}
static double cosine(const std::array<float,128> &a,const std::array<float,128> &b) {double n=0;for(unsigned i=0;i<128;++i)n+=double(a[i])*b[i];return n;}
static bool estimate_yaw(const std::vector<cv::Point2d> &landmarks,int width,int height,double &yaw) {
    // OpenCV camera coordinates have +Y down and +Z away from the lens. The
    // eyes/mouth sit behind the nose. Inverting these axes reports a frontal
    // face as ~180 degrees and makes the initial centering task impossible.
    const std::vector<cv::Point3d> model{{-32,-34,26},{32,-34,26},{0,0,0},{-23,25,20},{23,25,20}};
    cv::Mat camera=(cv::Mat_<double>(3,3)<<width,0,width/2.,0,width,height/2.,0,0,1),rotation,translation,matrix;
    if(!cv::solvePnP(model,landmarks,camera,cv::noArray(),rotation,translation,false,cv::SOLVEPNP_EPNP)||translation.at<double>(2)<=0)return false;
    cv::Rodrigues(rotation,matrix);yaw=std::atan2(matrix.at<double>(0,2),matrix.at<double>(2,2))*180/CV_PI;
    return std::isfinite(yaw);
}
struct Capture {
    Sensitive &s;bool enroll;std::atomic<bool> stop{false},done{false};std::mutex mutex;
    cv::Mat preview;std::string message=i18n::text("正在打开本机摄像头…");unsigned light=3;bool success=false;std::thread worker;
    Capture(Sensitive &state,bool create):s(state),enroll(create),worker([this] {run();}) {}
    ~Capture() {stop=true;if(worker.joinable())worker.join();preview.setTo(0);}
    void run() {
        try {
            auto detector=cv::FaceDetectorYN::create("/usr/share/mnos/face/yunet.onnx","",cv::Size(640,480),0.9f);
            auto recognizer=cv::FaceRecognizerSF::create("/usr/share/mnos/face/sface.onnx","");
            cv::CascadeClassifier eyes;require(eyes.load("/usr/share/opencv4/haarcascades/haarcascade_eye_tree_eyeglasses.xml"),i18n::text("缺少眼部检测模型。"));
            cv::VideoCapture camera(0,cv::CAP_V4L2);require(camera.isOpened(),i18n::text("未检测到可用摄像头。请连接或向虚拟机转发 USB 摄像头。"));
            camera.set(cv::CAP_PROP_FRAME_WIDTH,640);camera.set(cv::CAP_PROP_FRAME_HEIGHT,480);camera.set(cv::CAP_PROP_BUFFERSIZE,1);
            std::array<uint8_t,32> random{};require(random_bytes(random.data(),random.size()),i18n::text("安全随机源不可用。"));
            Challenge challenge(random,seconds());bool reference_ready=!enroll;std::array<double,128> sum{};unsigned count=0;
            while(!stop&&challenge.state()!=Task::Complete&&challenge.state()!=Task::Failed) {
                cv::Mat frame,faces;require(camera.read(frame)&&!frame.empty(),i18n::text("摄像头没有返回画面。"));
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
                        sample.usable=estimate_yaw(landmarks,frame.cols,frame.rows,sample.yaw);
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
                const char *prompt=i18n::text("保持正视镜头、睁开双眼，脸部完整位于画面中。");
                switch(challenge.state()) {
                case Task::Blink:prompt=i18n::text("请缓慢眨眼，再睁开。");break;
                case Task::Left:prompt=i18n::text("请向一侧缓慢转头（左）。");break;
                case Task::Right:prompt=i18n::text("请向另一侧缓慢转头（右）。");break;
                case Task::Color:prompt=i18n::text("回到正面，保持不动；圆环将缓慢变色。");break;
                case Task::Complete:prompt=i18n::text("本次动作与颜色响应检查完成。");break;
                case Task::Failed:prompt=i18n::text("本次采集未通过，请改善光线后重试。");break;
                default:break;
                }
                {std::lock_guard<std::mutex> guard(mutex);cv::cvtColor(frame,preview,cv::COLOR_BGR2BGRA);light=challenge.light();message=prompt;}
                frame.setTo(0);std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            success=!stop&&challenge.state()==Task::Complete;
            if(success&&enroll) {
                double norm=0;for(double v:sum)norm+=v*v;norm=std::sqrt(norm);require(count>=10&&norm>0,i18n::text("有效人脸样本不足。"));
                for(unsigned i=0;i<128;++i)s.reference[i]=float(sum[i]/norm);
            }
            auth_wipe(sum.data(),sizeof sum);camera.release();
        } catch(const std::exception &) {
            std::lock_guard<std::mutex> guard(mutex);success=false;message=i18n::text("无法完成摄像头采集。请检查摄像头转发、设备权限、模型文件和光线。");
        }
        done=true;
    }
};
struct View {
    Capture &capture;GtkWidget *window{},*drawing{},*status{},*close{};guint timer=0;bool accepted=false;
    View(Capture &c):capture(c) {
        window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(window),i18n::text("人脸验证 / Face verification"));gtk_window_set_default_size(GTK_WINDOW(window),800,680);
        auto *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);gtk_container_set_border_width(GTK_CONTAINER(box),16);gtk_container_add(GTK_CONTAINER(window),box);
        gtk_box_pack_start(GTK_BOX(box),security::label(i18n::text("普通摄像头 · 本地验证\n请按提示完成眨眼、转头和圆环颜色检测。密码和已开启的验证器仍需验证。")),false,false,0);
        drawing=gtk_drawing_area_new();gtk_widget_set_size_request(drawing,720,510);gtk_box_pack_start(GTK_BOX(box),drawing,true,true,0);
        status=security::label(i18n::text("正在准备…"));gtk_box_pack_start(GTK_BOX(box),status,false,false,0);close=gtk_button_new_with_label(i18n::text("取消"));gtk_box_pack_start(GTK_BOX(box),close,false,false,0);
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
            if(view->capture.done&&view->capture.success) {view->accepted=true;view->timer=0;gtk_widget_destroy(view->window);return G_SOURCE_REMOVE;}
            if(view->capture.done)gtk_button_set_label(GTK_BUTTON(view->close),i18n::text("完成 / 关闭"));return G_SOURCE_CONTINUE;
        },this);gtk_widget_show_all(window);
    }
    ~View() {if(timer)g_source_remove(timer);}
};
