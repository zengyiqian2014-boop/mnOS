// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
extern "C" {
#include "face-protocol.h"
#include "auth.h"
#include "monocypher.h"
}
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <sys/random.h>
namespace face_auth {
struct Record {
    uint32_t magic=FACE_MAGIC,version=2,uid=0,rounds=AUTH_ROUNDS;
    std::array<uint8_t,16> salt{};
    std::array<uint8_t,24> nonce{};
    std::array<uint8_t,16> tag{};
    std::array<uint8_t,512> encrypted{};
    std::array<std::array<uint8_t,32>,8> recovery{};
    uint64_t retry_after=0;
    uint32_t failures=0,reserved=0;
};
inline bool random_bytes(void *out,size_t n) {
    auto *p=static_cast<uint8_t *>(out);
    while(n) {ssize_t k=getrandom(p,n,0);if(k<0&&errno==EINTR)continue;if(k<=0)return false;p+=k;n-=size_t(k);}return true;
}
inline void derive(const char *password,const uint8_t *salt,uint8_t *key) {
    password_kdf work;password_begin(&work,password,salt,AUTH_ROUNDS);
    while(!password_step(&work,4096)){}memcpy(key,work.result,32);auth_wipe(&work,sizeof work);
}
inline bool valid(const float *feature) {
    double norm=0;for(unsigned i=0;i<128;++i) {if(!std::isfinite(feature[i]))return false;norm+=double(feature[i])*feature[i];}
    return norm>.98&&norm<1.02;
}
inline bool seal(Record &record,const uint8_t *key,const float *feature) {
    if(!valid(feature)||!random_bytes(record.nonce.data(),24))return false;
    crypto_aead_lock(record.encrypted.data(),record.tag.data(),key,record.nonce.data(),reinterpret_cast<const uint8_t *>(&record),offsetof(Record,nonce),reinterpret_cast<const uint8_t *>(feature),512);return true;
}
inline bool open(const Record &record,const uint8_t *key,float *feature) {
    return !crypto_aead_unlock(reinterpret_cast<uint8_t *>(feature),record.tag.data(),key,record.nonce.data(),reinterpret_cast<const uint8_t *>(&record),offsetof(Record,nonce),record.encrypted.data(),512)&&valid(feature);
}
inline bool recovery(Record &record,const char *code) {
    if(strlen(code)!=32)return false;
    std::array<uint8_t,32> hash{},zero{};factor_recovery_hash(code,record.salt.data(),hash.data());bool found=false;
    for(auto &entry:record.recovery)if(!factor_same(entry.data(),zero.data(),32)&&factor_same(entry.data(),hash.data(),32)) {entry.fill(0);found=true;}
    auth_wipe(hash.data(),hash.size());return found;
}
}
