// SPDX-License-Identifier: GPL-3.0-or-later
// Production storage and model loading in a disposable container; no camera
// recordings or real people's faces are used by this test.
#define main face_preview_program
#include "face-preview.cpp"
#undef main
#include <cassert>
#include <grp.h>
int main() {
    assert(getenv("MNOS_AUTH_TEST_CONTAINER"));assert(!setgroups(0,nullptr)&&!setgid(1000)&&!setuid(1000));
    int directory=profile_directory();Sensitive sensitive;Profile p;p.uid=getuid();
    assert(random_bytes(p.salt.data(),16)&&random_bytes(p.nonce.data(),24)&&random_bytes(sensitive.key.data(),32));
    for(unsigned i=0;i<128;++i)sensitive.reference[i]=float(i+1)/128;
    crypto_aead_lock(p.encrypted.data(),p.tag.data(),sensitive.key.data(),p.nonce.data(),reinterpret_cast<const uint8_t *>(&p),offsetof(Profile,nonce),reinterpret_cast<uint8_t *>(sensitive.reference.data()),512);
    save_profile(directory,p);Profile loaded;load_profile(directory,loaded);
    assert(!memmem(&loaded,sizeof loaded,sensitive.reference.data(),512));
    std::array<float,128> plain{};
    assert(!crypto_aead_unlock(reinterpret_cast<uint8_t *>(plain.data()),loaded.tag.data(),sensitive.key.data(),loaded.nonce.data(),reinterpret_cast<const uint8_t *>(&loaded),offsetof(Profile,nonce),loaded.encrypted.data(),512));
    assert(plain==sensitive.reference);loaded.uid++;
    assert(crypto_aead_unlock(reinterpret_cast<uint8_t *>(plain.data()),loaded.tag.data(),sensitive.key.data(),loaded.nonce.data(),reinterpret_cast<const uint8_t *>(&loaded),offsetof(Profile,nonce),loaded.encrypted.data(),512));
    assert(!unlinkat(directory,"face-v1",0));assert(!symlinkat("/etc/passwd",directory,"face-v1"));
    bool rejected=false;try {load_profile(directory,loaded);}catch(const std::exception &) {rejected=true;}assert(rejected);
    assert(!unlinkat(directory,"face-v1",0));close(directory);auth_wipe(plain.data(),sizeof plain);
    /* Exercise the pinned detector's real CPU inference on an empty image. */
    auto detector=cv::FaceDetectorYN::create("/usr/share/mnos/face/yunet.onnx","",cv::Size(320,240),0.9f);
    cv::Mat black=cv::Mat::zeros(240,320,CV_8UC3),faces;detector->detect(black,faces);assert(faces.rows==0);
    auto recognizer=cv::FaceRecognizerSF::create("/usr/share/mnos/face/sface.onnx","");assert(recognizer);
    if(access("/dev/video0",F_OK)) {Capture capture(sensitive,true);capture.worker.join();assert(capture.done&&!capture.success);}
    puts("PASS face preview: encrypted template roundtrip, UID binding, symlink refusal, pinned CPU model loading, blank-image rejection and unavailable-camera handling (no physical-camera validation)");
}
