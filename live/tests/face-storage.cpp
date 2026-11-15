// SPDX-License-Identifier: GPL-3.0-or-later
// Real cryptography/model execution; no photographs of actual people.
#include "face-capture.hpp"
#include "face-record.hpp"
#include <cassert>
#include <grp.h>
int main(){
    assert(getenv("MNOS_AUTH_TEST_CONTAINER"));assert(!setgroups(0,nullptr)&&!setgid(1000)&&!setuid(1000));
    Sensitive sensitive;face_auth::Record record;record.uid=1000;
    assert(random_bytes(record.salt.data(),16)&&random_bytes(sensitive.key.data(),32));
    sensitive.reference.fill(float(1/std::sqrt(128.)));
    assert(face_auth::seal(record,sensitive.key.data(),sensitive.reference.data()));
    assert(!memmem(&record,sizeof record,sensitive.reference.data(),512));
    std::array<float,128> plain{};assert(face_auth::open(record,sensitive.key.data(),plain.data())&&plain==sensitive.reference);
    record.uid++;assert(!face_auth::open(record,sensitive.key.data(),plain.data()));record.uid--;
    record.encrypted[0]^=1;assert(!face_auth::open(record,sensitive.key.data(),plain.data()));record.encrypted[0]^=1;
    sensitive.key[0]^=1;assert(!face_auth::open(record,sensitive.key.data(),plain.data()));
    const char *code="0123456789abcdef0123456789abcdef";factor_recovery_hash(code,record.salt.data(),record.recovery[0].data());
    assert(face_auth::recovery(record,code)&&!face_auth::recovery(record,code));auth_wipe(plain.data(),sizeof plain);
    // Regression for the camera-axis inversion that reported frontal faces as
    // 180 degrees. Project known head poses, then run the actual pose estimator.
    const std::vector<cv::Point3d> model{{-32,-34,26},{32,-34,26},{0,0,0},{-23,25,20},{23,25,20}};
    cv::Mat intrinsics=(cv::Mat_<double>(3,3)<<640,0,320,0,640,240,0,0,1);
    for(double angle:{-25.,0.,25.}){
        cv::Mat rotation=(cv::Mat_<double>(3,1)<<0,angle*CV_PI/180,0),translation=(cv::Mat_<double>(3,1)<<0,0,600);
        std::vector<cv::Point2d> landmarks;cv::projectPoints(model,rotation,translation,intrinsics,cv::noArray(),landmarks);
        double measured=999;assert(estimate_yaw(landmarks,640,480,measured)&&std::abs(measured-angle)<.1);
    }
    auto detector=cv::FaceDetectorYN::create("/usr/share/mnos/face/yunet.onnx","",cv::Size(320,240),0.9f);
    cv::Mat black=cv::Mat::zeros(240,320,CV_8UC3),faces;detector->detect(black,faces);assert(faces.rows==0);
    auto recognizer=cv::FaceRecognizerSF::create("/usr/share/mnos/face/sface.onnx","");assert(recognizer);
    if(camera_inventory().devices.empty()){Capture capture(sensitive,true);capture.worker.join();assert(capture.done&&!capture.success);}
    puts("PASS face encryption, UID binding, tamper/wrong-key rejection, one-use recovery, pinned model inference, blank-image rejection, no-camera rejection. No physical-camera validation.");
}
